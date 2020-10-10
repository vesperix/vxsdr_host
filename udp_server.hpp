#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

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

      }
    }

    void handle_send(boost::shared_ptr<std::string> message, const boost::system::error_code& error, std::size_t bytes_transferred) {

    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    boost::array<char, 65536> recv_buffer_;
};