#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <threads.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "thread_utils.h"

// parameters of the internal ring buffer
#define BUFFER_ELEMENT_SIZE         9000
#define RING_BUFFER_SIZE            1024

// parameters used to set the OS's network buffer
#define PACKETS_PER_NETWORK_BUFFER   256

#define USAGE_STRING "Usage: %s [options]\n" \
                     "   -A <local_address> [required]\n" \
                     "   -L <local_port>\n" \
                     "   -S <packet_size>\n" \
                     "   -N <num_packets>\n" \

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
} handler_args;

static bool done;
static ring_buffer rx_ring;

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

int handler(void *args) {
  static buffer_element b;
  size_t packets = 0, errors = 0, bytes = 0;
  unsigned seq, last_seq = 0;
  struct timespec t0, t1;

  init_buffer_element(&b);
  clock_gettime(CLOCK_MONOTONIC, &t0);

  while(!done) {
    if(packets == 0)
      clock_gettime(CLOCK_MONOTONIC, &t0);
    pop(&rx_ring, &b);
    bytes += b.bytes_used;
    memcpy(&seq, &b.data, sizeof(seq));
    if(seq != last_seq + 1)
      errors++;
    packets++;
    last_seq = seq;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);

  double dt = (t1.tv_sec - t0.tv_sec) + 1e-9 * (t1.tv_nsec - t0.tv_nsec);
  printf("%ld packets received; %ld out of sequence\n", packets, errors);
  printf("%g bytes/s, %g bits/s\n", bytes / dt, 8.0 * bytes / dt);
  return 0;
}

int main(int argc, char *argv[])
{
  struct in_addr local_ip;
  unsigned short local_port = 1030;

  size_t packet_size = 1024, n_packets = 1024;

  int opt;
  extern char *optarg;
  extern int optind, opterr, optopt;

  done = false;

  local_ip.s_addr = 0;

  while((opt = getopt(argc, argv, "A:L:S:N:")) != -1) {
    switch (opt) {
      case 'A':
        if(inet_pton(AF_INET, optarg, &local_ip) != 1)
          fatal_error("Invalid local IP address");
        break;
      case 'L':
        local_port = strtoul(optarg, NULL, 0);
        break;
      case 'S':
        packet_size = strtoul(optarg, NULL, 0);
        break;
      case 'N':
        n_packets = strtoul(optarg, NULL, 0);
        break;
      case '?':
        fprintf (stderr, "Unknown option `-%c'.\n", optopt);
      default:
        fprintf(stderr, USAGE_STRING, argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  if (optind < argc) {
    fprintf(stderr, USAGE_STRING, argv[0]);
    fatal_error("Command line option syntax error\n");
  }

  if(local_ip.s_addr == 0) {
    fprintf(stderr, USAGE_STRING, argv[0]);
    fatal_error("Must specify local address\n");
  }


  int rx_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if(rx_sock < 0)
    fatal_error("Cannot create socket (%s)\n", strerror(errno));

  int rx_buf_size = PACKETS_PER_NETWORK_BUFFER * packet_size;
  if(setsockopt(rx_sock, SOL_SOCKET, SO_RCVBUF, &rx_buf_size, sizeof(int)))
    fatal_error("Cannot set socket RX buffer size to %d (%s)\n", rx_buf_size, strerror(errno));

  struct sockaddr_in local_addr;
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons(local_port);
  local_addr.sin_addr = local_ip; // already in network order from inet_pton

  if(bind(rx_sock, (struct sockaddr *)&local_addr, sizeof(struct sockaddr_in)))
    fatal_error("Cannot bind to local address %s port %i (%s)\n", inet_ntoa(local_ip), local_port, strerror(errno));

  init_ring_buffer(&rx_ring);

  /* create buffer filling thread */

  thrd_t handler_thread;
  handler_args ha;
  ha.packet_size = packet_size;
  ha.n_packets = n_packets;

  if(thrd_create(&handler_thread, &handler, (void *)&ha) != thrd_success)
    fatal_error("Cannot create handler thread");

  /* receive and push to buffer */

  size_t ntry = 0, nrec = 0, nerr = 0, nover = 0;
  static buffer_element b;
  init_buffer_element(&b);

  if(set_current_thread_priority_realtime(50))
    fprintf(stderr, "unable to set realtime thread priority\n");

  while(!done) {
    while(has_space(&rx_ring) && !done) {
      ssize_t ns = recvfrom(rx_sock, b.data, BUFFER_ELEMENT_SIZE, 0, NULL, NULL);
      ntry++;
      if(ns > 0) {
        b.bytes_used = ns;
        push(&rx_ring, &b);
        nrec++;
      }
      else {
        nerr++;
        fprintf(stderr, "Receive error (%s)\n", strerror(errno));
      }

      if(ntry >= n_packets)
        done = true;
    }
    if(!done) {
      nover++;
      fprintf(stderr, "Receive ring buffer overflow\n");
    }
  }

  thrd_join(handler_thread, NULL);
  close(rx_sock);

  printf("ntry = %10ld, nrec = %10ld, nerr = %10ld\n", ntry, nrec, nerr);

  return 0;
}
