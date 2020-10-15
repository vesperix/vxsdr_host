#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

static const size_t tx_buffer_max_bytes      = 262144;
static const size_t tx_queue_length          = 256;   // buffers

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

void generator(const size_t max_packets, const size_t requested_packet_size, const unsigned packet_wait_us) {
  tx_buffer_element buf;
  BOOST_LOG_TRIVIAL(info) << "generator thread started";

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
  BOOST_LOG_TRIVIAL(info) << (max_packets * packet_size / dt.count()) << " bytes/s, " << (8 * max_packets * packet_size / dt.count()) << " bits/s";

  if(!tx_stop_flag)
    set_tx_stop_flag();
  BOOST_LOG_TRIVIAL(info) << "generator thread finished";
}

class udp_tx {
  public:
    udp_tx(boost::asio::io_service &io_service, boost::asio::ip::udp::endpoint &local_endpoint,
      boost::asio::ip::udp::endpoint &destination_endpoint) : socket_(io_service, local_endpoint) {
      local_endpoint_ = local_endpoint;
      destination_endpoint_ = destination_endpoint;
      send_errors_ = 0;
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
      if (error && error != boost::asio::error::message_size) {
        send_errors_++;
        //BOOST_LOG_TRIVIAL(error) << "async send error " << error;
      }
    }
};