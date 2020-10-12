#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

static const size_t tx_buffer_bytes          = 65536;
static const size_t tx_queue_length          = 4096;   // buffers
static const size_t tx_write_buffer_size     = 16;     // buffers
static const int    tx_write_thread_sleep_us = 1600;   // (buffer_bytes / 4 bytes per sample) * write_buffer_size / f_s

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
    }
    udp_tx(boost::asio::io_service& io_service,  boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::address_v4 multicast_ip, unsigned multicast_port)
      : socket_(io_service, local_endpoint) {
      local_endpoint_ = local_endpoint;
      multicast_ip_ = multicast_ip;
      multicast_port_ = multicast_port;
      socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket_.set_option(boost::asio::ip::multicast::join_group(multicast_ip));
    }
  private:
    void handle_send(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (!error || error == boost::asio::error::message_size) {
        tx_queue.push(send_buffer_);
      }
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint local_endpoint_;
    boost::asio::ip::udp::endpoint destination_endpoint_;
    boost::asio::ip::address_v4 multicast_ip_;
    unsigned multicast_port_;
    tx_buffer_element send_buffer_;
};