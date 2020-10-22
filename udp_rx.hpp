#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

static const size_t rx_buffer_bytes          = 262144; // maxumum size of a VITA49 data packet
static const size_t rx_queue_length          = 256;    // rx_buffers
static const size_t rx_write_buffer_size     = 16;     // rx_buffers
//static const int    rx_write_thread_sleep_us = 1600;   // (currently unused)

static std::atomic<bool> rx_stop_flag;

void set_rx_stop_flag() {
  rx_stop_flag = true;
}

typedef std::array<char, rx_buffer_bytes> rx_buffer;

typedef  struct {
  size_t bytes_used;
  rx_buffer data;
} rx_buffer_element;

static boost::lockfree::spsc_queue<rx_buffer_element, boost::lockfree::capacity<rx_queue_length> > rx_queue;

void writer(const std::string &outfilename, const size_t max_packets) {

  BOOST_LOG_TRIVIAL(info) << "writer thread started";

  std::array<rx_buffer_element, rx_write_buffer_size> buffer;
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);
  size_t packets = 0;

  while(packets < max_packets and !rx_stop_flag) {
    while(rx_queue.read_available() > rx_write_buffer_size) {
    	size_t nwrite = rx_queue.pop(&buffer.front(), rx_write_buffer_size);
      for(size_t i = 0; i < nwrite; i++)
        outfile.write((const char*)&buffer[i].data, buffer[i].bytes_used);
      packets += nwrite;
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }

  while(size_t nwrite = rx_queue.pop(&buffer.front(), rx_write_buffer_size))
    outfile.write((const char*)&buffer.front(), nwrite * sizeof(rx_buffer_element));

  outfile.close();
  if(!rx_stop_flag)
    set_rx_stop_flag();
  BOOST_LOG_TRIVIAL(info) << "writer thread finished";
}

void sequence_checker(const size_t max_packets, const size_t packet_size) {
  BOOST_LOG_TRIVIAL(info) << "sequence checker thread started";
  const unsigned buffer_max_packets = 256;
  static std::array<rx_buffer_element, buffer_max_packets> buffer;
  size_t packets = 0, errors = 0;
  unsigned seq, last_seq = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets and !rx_stop_flag) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
    while(rx_queue.read_available()) {
      size_t n_popped = rx_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        memcpy(&seq, &buffer[i].data, sizeof(seq));
        if(seq != last_seq + 1)
          errors++;
        packets++;
        last_seq = seq;
      }
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }
  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  BOOST_LOG_TRIVIAL(info) << packets << " packets recieved; " << errors << " out of sequence";
  BOOST_LOG_TRIVIAL(info) << (packets * packet_size / dt.count()) << " bytes/s, " << (8 * packets * packet_size / dt.count()) << " bits/s";
  if(!rx_stop_flag)
    set_rx_stop_flag();
  BOOST_LOG_TRIVIAL(info) << "sequence checker thread finished";
}

void rx_printer(const size_t max_packets, const size_t packet_size) {
  BOOST_LOG_TRIVIAL(info) << "rx printer thread started (" << max_packets <<" packets)";
  static const size_t buffer_max_packets = 16;
  std::array<rx_buffer_element, buffer_max_packets> buffer;
  size_t packets = 0;

  while(packets < max_packets and !rx_stop_flag) {
    while(rx_queue.read_available()) {
      size_t n_popped = rx_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        BOOST_LOG_TRIVIAL(info) << "rx_printer recieved packet with " << buffer[i].bytes_used << " bytes";
        packets++;
      }
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }
  if(!rx_stop_flag)
    set_rx_stop_flag();
  BOOST_LOG_TRIVIAL(info) << "rx_printer thread finished";
}

class udp_rx {
  public:
    udp_rx(boost::asio::io_service &io_service, boost::asio::ip::udp::endpoint &local_endpoint)
      : socket_(io_service, local_endpoint) {
      local_endpoint_ = local_endpoint;
      if(!rx_stop_flag)
        start_receive();
    }
    udp_rx(boost::asio::io_service &io_service, boost::asio::ip::udp::endpoint &local_endpoint,
               const boost::asio::ip::address_v4 multicast_ip)
      : socket_(io_service, local_endpoint.protocol()) {
      socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket_.bind(local_endpoint);
      socket_.set_option(boost::asio::ip::multicast::join_group(multicast_ip));
      local_endpoint_ = local_endpoint;
      if(!rx_stop_flag)
        start_receive();
    }
  private:
    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint local_endpoint_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    rx_buffer_element recv_buffer_;

    void start_receive() {
      auto hdlr_ = boost::bind(&udp_rx::handle_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred);
      socket_.async_receive_from(boost::asio::buffer(recv_buffer_.data, recv_buffer_.data.size()), remote_endpoint_, hdlr_);
    }
    void handle_receive(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (!error || error == boost::asio::error::message_size) {
        recv_buffer_.bytes_used = bytes_transferred;
        rx_queue.push(recv_buffer_);
      }
      else
        BOOST_LOG_TRIVIAL(error) << "receive error: " << error.message();
      if(!rx_stop_flag)
        start_receive();
    }
};