#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "thread_utils.h"

// parameters of the internal ring buffer
static const size_t tx_buffer_max_bytes          = 262144;  // maxumum size of a VITA49 data packet
static const size_t tx_queue_length              = 256;     // tx_buffers in queue

// parameters used to set the OS's network buffer
static const unsigned packets_per_network_buffer = 16;

static std::atomic<bool> tx_stop_flag;

void set_tx_stop_flag() {
  tx_stop_flag = true;
}

typedef boost::array<char, tx_buffer_max_bytes> tx_buffer;

typedef  struct {
  size_t bytes_used;
  tx_buffer data;
} tx_buffer_element;

static boost::lockfree::spsc_queue<tx_buffer_element, boost::lockfree::capacity<tx_queue_length> > tx_queue;

class udp_tx {
  public:
    udp_tx(boost::asio::io_service &io_service, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast) : socket_(io_service, local_endpoint) {
      local_endpoint_ = local_endpoint;
      destination_endpoint_ = destination_endpoint;
      send_errors_ = 0;
      socket_.set_option(boost::asio::socket_base::broadcast(broadcast));
      boost::asio::socket_base::send_buffer_size opt(packets_per_network_buffer * tx_buffer_max_bytes);
      socket_.set_option(opt);
      start_send();
    }
  private:
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint local_endpoint_;
    boost::asio::ip::udp::endpoint destination_endpoint_;
    static const size_t tx_buffer_size_ = 16;
    std::array<tx_buffer_element, tx_buffer_size_> send_buffer_;
    unsigned send_errors_;

    void start_send() {
      BOOST_LOG_TRIVIAL(info) << "send started";
      auto hdlr = boost::bind(&udp_tx::handle_send, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred);
      while(!tx_stop_flag) {
        if(tx_queue.read_available()) {
          size_t n_popped = tx_queue.pop(&send_buffer_.front(), tx_buffer_size_);
          for(size_t i = 0; i < n_popped; i++)
            socket_.async_send_to(boost::asio::buffer(&send_buffer_[i].data, send_buffer_[i].bytes_used), destination_endpoint_, hdlr);
        }
      }
    }
    void handle_send(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (error) {
        send_errors_++;
        BOOST_LOG_TRIVIAL(error) << "async send error: " << error.message();
      }
    }
};

void generator(const size_t max_packets, const size_t requested_packet_size, const unsigned packet_wait_us) {
  tx_buffer_element buf;
  BOOST_LOG_TRIVIAL(info) << "generator thread started";

  if(set_current_thread_priority_realtime(50))
    BOOST_LOG_TRIVIAL(error) << "unable to set realtime thread priority";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > tx_buffer_max_bytes) {
    BOOST_LOG_TRIVIAL(error) << "requested packet size is greater than maximum (" << tx_buffer_max_bytes <<"); setting to maximum";
    packet_size = tx_buffer_max_bytes;
  }

  auto t0 = std::chrono::steady_clock::now();

  for(unsigned i = 1; i <= max_packets and !tx_stop_flag; i++) {
    memset(&buf.data, 0, packet_size);
    memcpy(&buf.data, &i, sizeof(i));
    buf.bytes_used = packet_size;
    while(!tx_queue.write_available()) {}

    tx_queue.push(buf);
    if(packet_wait_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(packet_wait_us));
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  BOOST_LOG_TRIVIAL(info) << max_packets << " packets (" << (max_packets * packet_size) << " bytes) sent in " << dt.count() << " seconds";

  if(!tx_stop_flag)
    set_tx_stop_flag();
  BOOST_LOG_TRIVIAL(info) << "generator thread finished";
}

void sender(const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast)
{
  BOOST_LOG_TRIVIAL(info) << "sender thread started";

  if(set_current_thread_priority_realtime(50))
    BOOST_LOG_TRIVIAL(error) << "unable to set realtime thread priority";

  boost::asio::io_service io_service;
  udp_tx server(io_service, local_endpoint, destination_endpoint, broadcast);
  io_service.run();
  BOOST_LOG_TRIVIAL(info) << "sender thread finished";
}
