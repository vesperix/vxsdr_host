#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

static const size_t tx_buffer_bytes          = 65536;
static const size_t tx_queue_length          = 256;   // buffers
static const int    tx_send_thread_sleep_us  = 100;   // (buffer_bytes / 4 bytes per sample) / f_s

typedef boost::array<char, tx_buffer_bytes> tx_buffer;

typedef  struct {
  size_t bytes_used;
  tx_buffer data;
} tx_buffer_element;

static boost::lockfree::spsc_queue<tx_buffer_element, boost::lockfree::capacity<tx_queue_length> > tx_queue;

void generator(const size_t max_packets) {

}

class udp_tx {
  public:
    udp_tx(boost::asio::io_service &io_service, boost::asio::ip::udp::endpoint &local_endpoint,
      boost::asio::ip::udp::endpoint &destination_endpoint) : socket_(io_service, local_endpoint) {
      local_endpoint_ = local_endpoint;
      destination_endpoint_ = destination_endpoint;
      start_send();
    }
  private:
    void start_send() {
      BOOST_LOG_TRIVIAL(info) << "send started";
      while(true) {
        if(tx_queue.read_available()) {
          tx_queue.pop(&send_buffer_);
          socket_.async_send_to(boost::asio::buffer(send_buffer_.data, send_buffer_.bytes_used), destination_endpoint_,
              boost::bind(&udp_tx::handle_send, this, boost::asio::placeholders::error,
              boost::asio::placeholders::bytes_transferred));
        }
        std::this_thread::sleep_for(std::chrono::microseconds(tx_send_thread_sleep_us));
      }
    }
    void handle_send(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (error && error != boost::asio::error::message_size) {
        BOOST_LOG_TRIVIAL(error) << "async send error " << error;
      }
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint local_endpoint_;
    boost::asio::ip::udp::endpoint destination_endpoint_;
    boost::asio::ip::address_v4 multicast_ip_;
    unsigned multicast_port_;
    tx_buffer_element send_buffer_;
};