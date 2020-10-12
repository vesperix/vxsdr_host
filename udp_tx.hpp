#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

static const size_t tx_buffer_bytes          = 4096;
static const size_t tx_queue_length          = 4096;   // buffers
static const size_t tx_write_buffer_size     = 16;     // buffers
static const int    tx_write_thread_sleep_us = 100;    // (buffer_bytes / 4 bytes per sample) * write_buffer_size / f_s

static volatile sig_atomic_t tx_stop_signal_called = 0;

void call_tx_stop_signal() { tx_stop_signal_called = 1; }

typedef  boost::array<char, tx_buffer_bytes> tx_buffer_element;

static boost::lockfree::spsc_queue<tx_buffer_element, boost::lockfree::capacity<tx_queue_length> > tx_queue;


class udp_tx {
  public:
    udp_tx(boost::asio::io_service& io_service, const boost::asio::ip::address_v4 local_ip, const unsigned local_port)
      : socket_(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), local_port)) {

      socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      local_endpoint_ = boost::asio::ip::udp::endpoint(local_ip, local_port);
      socket_.bind(local_endpoint_);
    }
    udp_tx(boost::asio::io_service& io_service, const boost::asio::ip::address_v4 local_ip, const unsigned local_port,
               const boost::asio::ip::address_v4 multicast_ip)
      : socket_(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), local_port)) {

      socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      local_endpoint_ = boost::asio::ip::udp::endpoint(local_ip, local_port);
      socket_.bind(local_endpoint_);

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
    boost::asio::ip::udp::endpoint remote_endpoint_;
    tx_buffer_element send_buffer_;
};