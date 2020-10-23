#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <threads.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "thread_utils.h"

// parameters of the internal ring buffer
#define BUFFER_ELEMENT_SIZE       262144
#define RING_BUFFER_SIZE             256

// parameters used to set the OS's network buffer
#define PACKETS_PER_NETWORK_BUFFER    16

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
  char data[BUFFER_ELEMENT_SIZE];
  size_t bytes_used;
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
  unsigned wait_us;
} filler_args;

static bool done;
static ring_buffer tx_ring;

void init_buffer_element(buffer_element *b) {
  memset(b->data, 0, BUFFER_ELEMENT_SIZE);
  b->bytes_used = 0;
}

inline void copy_buffer_element(buffer_element *copy, buffer_element *orig) {
  memcpy(copy->data, orig->data, orig->bytes_used);
  copy->bytes_used = orig->bytes_used;
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
  b.bytes_used = (a->packet_size <= BUFFER_ELEMENT_SIZE ? a->packet_size : BUFFER_ELEMENT_SIZE);

  while(!done) {
    while(has_space(&tx_ring) && !done) {
      memset(&b.data, 0, b.bytes_used);
      unsigned seq = n_pushed % UINT_MAX;
      memcpy(&b.data, &seq, sizeof(seq));
      push(&tx_ring, &b);
      n_pushed++;
      if(n_pushed >= a->n_packets)
        done = true;
      if(a->wait_us > 0)
        usleep(a->wait_us);
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
  fa.wait_us = wait_us;

  if(thrd_create(&filler_thread, &filler, (void *)&fa) != thrd_success)
    fatal_error("Cannot create filler thread");

  /* pop from buffer and send */

  size_t ntry = 0, nsent = 0, nerr = 0, nunder = 0;
  static buffer_element b;
  init_buffer_element(&b);

  while(!done) {
    while(has_data(&tx_ring) && !done) {
      pop(&tx_ring, &b);
      ssize_t ns = sendto(tx_sock, b.data, b.bytes_used, 0,
                      (struct sockaddr *)&destination_addr, sizeof(struct sockaddr_in));
      ntry++;
      if(ns != b.bytes_used) {
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
    if(!done) {
      nunder++;
      fprintf(stderr, "Transmit ring buffer underflow\n");
    }
  }

  thrd_join(filler_thread, NULL);
  close(tx_sock);

  return 0;
}