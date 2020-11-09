#define _GNU_SOURCE  // needed for setting thread affinity
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <threads.h>

#include "thread_utils.h"

// parameters of the internal ring buffer
#define BUFFER_ELEMENT_SIZE         9000
#define BUFFER_DATA_SIZE            2048
#define RING_BUFFER_SIZE            1024

// parameters used to set the OS's network buffer
#define PACKETS_PER_NETWORK_BUFFER   256

#define USAGE_STRING "Usage: %s [options]\n" \
                     "   -A <local_address> [required]\n" \
                     "   -L <local_port>\n" \
                     "   -D <destination address> [required]\n" \
                     "   -B (enable broadcast; required when destination is a broadcast address)\n" \
                     "   -P <destination_port>\n" \
                     "   -S <packet_size>\n" \
                     "   -N <num_packets>\n" \
                     "   -W <wait_us>\n"

typedef struct {
  uint16_t seq_lo;
  uint16_t seq_hi;
  uint16_t data_id;
  uint16_t pad;
  int16_t data[BUFFER_DATA_SIZE];
} buffer_element;

typedef struct {
  buffer_element ring[RING_BUFFER_SIZE];
  atomic_uint head;
  atomic_uint tail;
  atomic_uint n_items;
} ring_buffer;

typedef struct {
  size_t n_packets;
  size_t packet_size;
} filler_args;

static bool done;
static ring_buffer tx_ring;

void init_buffer_element(buffer_element *b) {
  memset(b, 0, sizeof(buffer_element));
}

inline void copy_buffer_element(buffer_element *copy, buffer_element *orig) {
  memcpy(copy, orig, sizeof(buffer_element));
}

void init_ring_buffer(ring_buffer *r) {
  for(unsigned i = 0; i < RING_BUFFER_SIZE; i++)
    init_buffer_element(r->ring + i);
  r->head = 0;
  r->tail = 0;
  r->n_items = 0;
}

inline bool has_data(ring_buffer *r) {
  return r->n_items > 0;
}

inline bool has_space(ring_buffer *r) {
  return r->n_items < (RING_BUFFER_SIZE - 1);
}

int push(ring_buffer *r, buffer_element *b) {
  unsigned new_head = (r->head + 1) % (RING_BUFFER_SIZE - 1);
  if(new_head == r->tail)
    return -1; // full
  copy_buffer_element(r->ring + r->head, b);
  r->head = new_head;
  r->n_items++;
  return 0;
}

int pop(ring_buffer *r, buffer_element *b) {
  if(r->n_items == 0)
    return -1; // empty
  copy_buffer_element(b, r->ring + r->tail);
  r->tail =  (r->tail + 1) % (RING_BUFFER_SIZE - 1);
  r->n_items--;
  return 0;
}

void fatal_error(const char *s, ...) {
  va_list args;
  va_start(args, s);
  vfprintf(stderr, s, args);
  va_end(args);
  fflush(stderr);
  exit(EXIT_FAILURE);
}

int filler(void *args) {
  filler_args *a = (filler_args *)args;
  size_t n_pushed = 0;

  static buffer_element b;

  init_buffer_element(&b);

  const double amp = 800;
  const int ncycles = 200;
  const double w = (2 * M_PI * ncycles) / BUFFER_DATA_SIZE; 

  b.data_id = 0xDACD;
  for(unsigned i = 0; i < BUFFER_DATA_SIZE; i++)
    b.data[i] = round(amp * sin(w * i));

  while(n_pushed < a->n_packets && !done) {
    while(n_pushed < a->n_packets && has_space(&tx_ring) && !done) {
      b.seq_lo = n_pushed % 65536;
      b.seq_hi = n_pushed >> 16;
      push(&tx_ring, &b);
      n_pushed++;
    }
  }
  return 0;
}

int main(int argc, char *argv[])
{
  struct in_addr local_ip,  destination_ip;
  unsigned short local_port = 1030, destination_port = 1030;

  size_t packet_size = 1024, n_packets = 1024;
  unsigned wait_us = 1000;

  done = false;

  local_ip.s_addr = 0;
  destination_ip.s_addr = 0;

  bool broadcast = false;

  int opt;
  extern char *optarg;
  extern int optind, opterr, optopt;

  while((opt = getopt(argc, argv, "A:L:D:BP:S:N:W:")) != -1) {
    switch (opt) {
      case 'A':
        if(inet_pton(AF_INET, optarg, &local_ip) != 1)
          fatal_error("Invalid local IP address");
        break;
      case 'L':
        local_port = strtoul(optarg, NULL, 0);
        break;
      case 'D':
        if(inet_pton(AF_INET, optarg, &destination_ip) != 1)
          fatal_error("Invalid destination IP address");
        break;
      case 'B':
        broadcast = true;
        break;
      case 'P':
        destination_port = strtoul(optarg, NULL, 0);
        break;
      case 'S':
        packet_size = strtoul(optarg, NULL, 0);
        break;
      case 'N':
        n_packets = strtoul(optarg, NULL, 0);
        break;
      case 'W':
        wait_us = strtoul(optarg, NULL, 0);
        break;
      case '?':
        fprintf (stderr, "Unknown option `-%c'.\n", optopt);
      default:
        fprintf(stderr, USAGE_STRING, argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  if(optind < argc) {
    fprintf(stderr, USAGE_STRING, argv[0]);
    fatal_error("Command line option syntax error\n");
  }

  if(local_ip.s_addr == 0) {
    fprintf(stderr, USAGE_STRING, argv[0]);
    fatal_error("Must specify local address\n");
  }

  if(destination_ip.s_addr == 0) {
    fprintf(stderr, USAGE_STRING, argv[0]);
    fatal_error("Must specify destination address\n");
  }

  int tx_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(tx_sock < 0)
    fatal_error("Cannot create socket (%s)\n", strerror(errno));

  int tx_buf_size = PACKETS_PER_NETWORK_BUFFER * packet_size;
  if(setsockopt(tx_sock, SOL_SOCKET, SO_SNDBUF, &tx_buf_size, sizeof(int)))
    fatal_error("Cannot set socket TX buffer size to %d (%s)\n", tx_buf_size, strerror(errno));

  struct sockaddr_in local_addr;
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons(local_port);
  local_addr.sin_addr = local_ip; // already in network order from inet_pton

  if(bind(tx_sock, (struct sockaddr *)&local_addr, sizeof(struct sockaddr_in)))
    fatal_error("Cannot bind to local address %s port %i (%s)\n", inet_ntoa(local_ip), local_port, strerror(errno));

  struct sockaddr_in destination_addr;
  destination_addr.sin_family = AF_INET;
  destination_addr.sin_port = htons(destination_port);
  destination_addr.sin_addr = destination_ip; // already in network order from inet_pton

  if(broadcast) {
	  int enable = 1;
    if(setsockopt(tx_sock, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) < 0)
      fatal_error("Cannot enable broadcast on local socket");
  }

  init_ring_buffer(&tx_ring);

  /* create buffer filling thread */

  thrd_t filler_thread;
  filler_args fa;
  fa.packet_size = packet_size;
  fa.n_packets = n_packets;

  if(thrd_create(&filler_thread, &filler, (void *)&fa) != thrd_success)
    fatal_error("Cannot create filler thread");

  /* pop from buffer and send */

  size_t ntry = 0, nsent = 0, nerr = 0;
  static buffer_element b;
  init_buffer_element(&b);

  if(set_current_thread_priority_realtime(50))
    fprintf(stderr, "unable to set realtime thread priority\n");

  while(!done) {
    while(has_data(&tx_ring) && !done) {
      pop(&tx_ring, &b);
      ssize_t ns = sendto(tx_sock, &b, sizeof(b), 0,
                      (struct sockaddr *)&destination_addr, sizeof(struct sockaddr_in));
      ntry++;
      if(ns != sizeof(b)) {
        nerr++;
        if(ns < 0) {
          fprintf(stderr, "Send error (%s)\n", strerror(errno));
        }
      }
      else
        nsent++;
      if(ntry >= n_packets)
        done = true;
    }
    usleep(wait_us);
  }

  thrd_join(filler_thread, NULL);
  close(tx_sock);

  printf("ntry = %ld, nsent = %ld, nerr = %ld\n", ntry, nsent, nerr);

  return 0;
}
