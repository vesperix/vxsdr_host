#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

static const size_t buffer_bytes          = 4096;
static const size_t queue_length          = 4096;   // buffers
static const size_t write_buffer_size     = 16;     // buffers
static const int    write_thread_sleep_us = 100;

typedef  boost::array<char, buffer_bytes> buffer_element;

boost::lockfree::spsc_queue<buffer_element, boost::lockfree::capacity<queue_length> > outqueue;

void writer(const std::string &outfilename) {
  std::vector<buffer_element> buffer(write_buffer_size);
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);

  while(!stop_signal_called) {
    while(outqueue.read_available() > write_buffer_size) {
    	size_t nwrite = outqueue.pop(&buffer.front(), write_buffer_size);
      outfile.write((const char*)&buffer.front(), nwrite * sizeof(buffer_element));
      }
    std::this_thread::sleep_for(std::chrono::microseconds(write_thread_sleep_us));
  }

  while(size_t nwrite = outqueue.pop(&buffer.front(), write_buffer_size))
    outfile.write((const char*)&buffer.front(), nwrite * sizeof(buffer_element));

  outfile.close();
}

class udp_server {
  public:
    udp_server(boost::asio::io_service& io_service, const unsigned port)
      : socket_(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)) {
      start_receive();
    }

  private:
    void start_receive() {
      socket_.async_receive_from(boost::asio::buffer(recv_buffer_), remote_endpoint_,
          boost::bind(&udp_server::handle_receive, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }

    void handle_receive(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (!error || error == boost::asio::error::message_size) {
        outqueue.push(recv_buffer_);
      }
    }

    void handle_send(boost::shared_ptr<std::string> data, const boost::system::error_code& error) {

    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    boost::array<char, buffer_bytes> recv_buffer_;
};