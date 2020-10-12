#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

static const size_t rx_buffer_bytes          = 4096;
static const size_t rx_queue_length          = 4096;   // buffers
static const size_t rx_write_buffer_size     = 16;     // buffers
static const int    rx_write_thread_sleep_us = 100;    // (buffer_bytes / 4 bytes per sample) * write_buffer_size / f_s

typedef  boost::array<char, rx_buffer_bytes> rx_buffer_element;

static boost::lockfree::spsc_queue<rx_buffer_element, boost::lockfree::capacity<rx_queue_length> > rx_queue;

void writer(const std::string &outfilename, const size_t max_packets) {

  BOOST_LOG_TRIVIAL(info) << "writer thread started";

  std::vector<rx_buffer_element> buffer(rx_write_buffer_size);
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);
  size_t packets = 0;

  while(packets < max_packets) {
    while(rx_queue.read_available() > rx_write_buffer_size) {
    	size_t nwrite = rx_queue.pop(&buffer.front(), rx_write_buffer_size);
      outfile.write((const char*)&buffer.front(), nwrite * sizeof(rx_buffer_element));
      packets += nwrite;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }

  while(size_t nwrite = rx_queue.pop(&buffer.front(), rx_write_buffer_size))
    outfile.write((const char*)&buffer.front(), nwrite * sizeof(rx_buffer_element));

  outfile.close();
  BOOST_LOG_TRIVIAL(info) << "writer thread finished";
}

class udp_rx {
  public:
    udp_rx(boost::asio::io_service &io_service, boost::asio::ip::udp::endpoint &local_endpoint)
      : socket_(io_service, local_endpoint) {
      local_endpoint_ = local_endpoint;
      start_receive();
    }
    udp_rx(boost::asio::io_service &io_service, boost::asio::ip::udp::endpoint &local_endpoint,
               const boost::asio::ip::address_v4 multicast_ip)
      : socket_(io_service, local_endpoint) {
      socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket_.set_option(boost::asio::ip::multicast::join_group(multicast_ip));
      local_endpoint_ = local_endpoint;
      start_receive();
    }
  private:
    void start_receive() {
      BOOST_LOG_TRIVIAL(info) << "receive started";
      socket_.async_receive_from(boost::asio::buffer(recv_buffer_), remote_endpoint_,
          boost::bind(&udp_rx::handle_receive, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }

    void handle_receive(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (!error || error == boost::asio::error::message_size) {
        rx_queue.push(recv_buffer_);
      }
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint local_endpoint_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    rx_buffer_element recv_buffer_;
};