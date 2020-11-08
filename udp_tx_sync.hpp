#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

// parameters of the internal ring buffer
static const size_t tx_buffer_max_bytes          = 4104;  // max mtu
static const size_t tx_queue_length              = 1024;   // tx_buffers in queue

// parameters used to set the OS's network buffer
static const unsigned packets_per_network_buffer = 1024;

static std::atomic<bool> tx_stop_flag;

void set_tx_stop_flag() {
  tx_stop_flag = true;
}
/*
typedef struct {
  size_t seq;
  size_t bytes_used;
  cycle_count_t ts[4];
  std::array<char, (tx_buffer_max_bytes - 2 * sizeof(size_t) - 4 * sizeof(cycle_count_t))> data;
} tx_buffer_element;
*/

typedef struct {
  uint16_t seq_lo;
  uint16_t seq_hi;
  uint16_t data_id;
  uint16_t pad;
  std::array<int16_t, 2048> data;
} tx_buffer_element;

static boost::lockfree::spsc_queue<tx_buffer_element, boost::lockfree::capacity<tx_queue_length> > tx_queue;

class udp_tx {
  public:
    udp_tx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast) : socket(io_context, local_endpoint) {
      destination = destination_endpoint;
      socket.set_option(boost::asio::socket_base::broadcast(broadcast));
      socket.set_option(boost::asio::socket_base::send_buffer_size(packets_per_network_buffer * tx_buffer_max_bytes));
      start_send();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint destination;
    static constexpr size_t tx_buffer_size_ = 256;
    std::array<tx_buffer_element, tx_buffer_size_> send_buffer_;
    unsigned send_errors_ = 0;

    void start_send() {
      LOGGER(info) << "send started";
      while(!tx_stop_flag) {
        if(tx_queue.read_available()) {
          size_t n_popped = tx_queue.pop(&send_buffer_.front(), send_buffer_.size());
          for(size_t i = 0; i < n_popped; i++) {
            size_t bytes_sent = socket.send_to(boost::asio::buffer(&send_buffer_[i], sizeof(tx_buffer_element)), destination);
            if(bytes_sent != sizeof(tx_buffer_element))
              send_errors_++;
          }
        }
      }
    }
};

void generator(const size_t max_packets, const size_t requested_packet_size, const unsigned packet_wait_us) {
  tx_buffer_element buf;
  LOGGER(info) << "generator thread started";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > tx_buffer_max_bytes) {
    LOGGER(error) << "requested packet size is greater than maximum (" << tx_buffer_max_bytes <<"); setting to maximum";
    packet_size = tx_buffer_max_bytes;
  }

  auto t0 = std::chrono::steady_clock::now();

  double a = 800;
  double f = 200. / 2048.0; 

  memset(&buf, 0, sizeof(buf));
  buf.data_id = 0xDACD;
  for(int i = 0; i < 2048; i++)
    buf.data[i] = std::round(a * sin(2 * M_PI * f * i));

  for(size_t i = 1; i <= max_packets and !tx_stop_flag; i++) {
    buf.seq_lo = i % 65536;
    buf.seq_hi = i >> 16;
    //buf.bytes_used = packet_size;
    // wait for room in the tx queue
    while(!tx_queue.write_available()) {}

    // cycle count when pushed
    //buf.ts[0] = read_cycle_counter();
    tx_queue.push(buf);
    if(packet_wait_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(packet_wait_us));
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << max_packets << " packets (" << (max_packets * packet_size) << " bytes) sent in " << dt.count() << " seconds";

  if(!tx_stop_flag)
    set_tx_stop_flag();
  LOGGER(info) << "generator thread finished";
}

void sender(const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast)
{
  LOGGER(info) << "sender thread started";

  //if(set_current_thread_priority_realtime(10))
  //  LOGGER(error) << "unable to set realtime thread priority";

  boost::asio::io_context io_context;
  udp_tx server(io_context, local_endpoint, destination_endpoint, broadcast);
  io_context.run();
  LOGGER(info) << "sender thread finished";
}
