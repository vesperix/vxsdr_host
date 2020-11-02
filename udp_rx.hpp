#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

// parameters of the internal ring buffer
static const size_t rx_buffer_max_bytes          = 9000;    // max mtu
static const size_t rx_queue_length              = 1024;    // rx_buffers

// parameters of the writer thread's buffers
static const size_t rx_write_buffer_size         =  256;     // rx_buffers
static const int    rx_write_thread_sleep_us     =  100;

// parameters used to set the OS's network buffer
static const unsigned packets_per_network_buffer = 1024;

static std::atomic<bool> rx_stop_flag;

void set_rx_stop_flag() {
  rx_stop_flag = true;
}

typedef struct {
  size_t seq;
  size_t bytes_used;
  cycle_count_t ts[4];
  std::array<char, (rx_buffer_max_bytes - 2 * sizeof(size_t) - 4 * sizeof(cycle_count_t))> data;
} rx_buffer_element;

static boost::lockfree::spsc_queue<rx_buffer_element, boost::lockfree::capacity<rx_queue_length> > rx_queue;

class udp_rx {
  public:
    udp_rx(boost::asio::io_service &io_service, const boost::asio::ip::udp::endpoint &local_endpoint, const bool broadcast)
      : socket_(io_service, local_endpoint) {
      local_endpoint_ = local_endpoint;
      boost::asio::socket_base::broadcast bcast(broadcast);
      socket_.set_option(bcast);
      boost::asio::socket_base::receive_buffer_size bufsiz(packets_per_network_buffer * rx_buffer_max_bytes);
      socket_.set_option(bufsiz);
      if(!rx_stop_flag)
        start_receive();
    }
    udp_rx(boost::asio::io_service &io_service, const boost::asio::ip::udp::endpoint &local_endpoint,
               const boost::asio::ip::address_v4 &multicast_ip)
      : socket_(io_service, local_endpoint.protocol()) {
      socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      boost::asio::socket_base::receive_buffer_size bufsiz(packets_per_network_buffer * rx_buffer_max_bytes);
      socket_.set_option(bufsiz);
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
      socket_.async_receive_from(boost::asio::buffer(&recv_buffer_, sizeof(recv_buffer_)), remote_endpoint_, hdlr_);
    }
    void handle_receive(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (!error) {
        // cycle count when received
        recv_buffer_.ts[2] = read_cycle_counter();
        rx_queue.push(recv_buffer_);
      }
      else
        LOGGER(error) << "async receive error: " << error.message();
      if(!rx_stop_flag)
        start_receive();
    }
};

void writer(const std::string &outfilename, const size_t max_packets) {
  LOGGER(info) << "writer thread started";

  std::array<rx_buffer_element, rx_write_buffer_size> buffer;
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);
  size_t packets = 0;

  while(packets < max_packets and !rx_stop_flag) {
    while(rx_queue.read_available() >= rx_write_buffer_size) {
    	size_t nwrite = rx_queue.pop(&buffer.front(), rx_write_buffer_size);
      for(size_t i = 0; i < nwrite; i++)
        outfile.write((const char*)&buffer[i].data, buffer[i].bytes_used);
      packets += nwrite;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }

  while(size_t nwrite = rx_queue.pop(&buffer.front(), rx_write_buffer_size))
    outfile.write((const char*)&buffer.front(), nwrite * sizeof(rx_buffer_element));

  outfile.close();
  if(!rx_stop_flag)
    set_rx_stop_flag();
  LOGGER(info) << "writer thread finished";
}

void sequence_checker(const size_t max_packets, const size_t packet_size) {
  LOGGER(info) << "sequence checker thread started";

  const unsigned buffer_max_packets = 256;
  static std::array<rx_buffer_element, buffer_max_packets> buffer;
  size_t packets = 0, errors = 0;
  size_t last_seq = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets and !rx_stop_flag) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
    while(rx_queue.read_available()) {
      size_t n_popped = rx_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        if(buffer[i].seq != last_seq + 1)
          errors++;
        packets++;
        last_seq = buffer[i].seq;
      }
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }
  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec; " << errors << " out of sequence";
  LOGGER(info) << (packets * packet_size / dt.count()) << " bytes/s, " << (8 * packets * packet_size / dt.count()) << " bits/s";
  if(!rx_stop_flag)
    set_rx_stop_flag();
  LOGGER(info) << "sequence checker thread finished";
}

void sequence_timer(const size_t max_packets, const size_t packet_size) {
  LOGGER(info) << "sequence timer thread started";

  const unsigned buffer_max_packets = 256;
  static std::array<rx_buffer_element, buffer_max_packets> buffer;
  size_t packets = 0, errors = 0;
  size_t last_seq = 0;

  std::vector<cycle_count_t> count1(max_packets), count2(max_packets), count3(max_packets), count4(max_packets);
  std::vector<unsigned> seqnums(max_packets);

  auto t0 = std::chrono::steady_clock::now();
  cycle_count_t tt0 = read_cycle_counter();

  while(packets < max_packets and !rx_stop_flag) {
    if(packets == 0) {
      t0 = std::chrono::steady_clock::now();
      tt0 = read_cycle_counter();
    }
    while(rx_queue.read_available()) {
      size_t n_popped = rx_queue.pop(&buffer.front(), buffer_max_packets);
      cycle_count_t tpop = read_cycle_counter();
      for(size_t i = 0; i < n_popped; i++) {
        if(buffer[i].seq != last_seq + 1)
          errors++;
        count1[packets] = buffer[i].ts[0];
        count2[packets] = buffer[i].ts[1] - buffer[i].ts[0];
        count3[packets] = buffer[i].ts[2] - buffer[i].ts[0];
        count4[packets] = tpop - buffer[i].ts[0];
        seqnums[packets] = buffer[i].seq;
        packets++;
        last_seq = buffer[i].seq;
      }
    }
  }
  auto t1 = std::chrono::steady_clock::now();
  cycle_count_t tt1 = read_cycle_counter();
  std::chrono::duration<double> dt = t1 - t0;

  double cal = 1e-6 * (tt1 - tt0) / dt.count();
  LOGGER(info) << "cycle counter: " << cal << " counts/us";

  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec; " << errors << " out of sequence";
  LOGGER(info) << (packets * packet_size / dt.count()) << " bytes/s, " << (8 * packets * packet_size / dt.count()) << " bits/s";

  for(size_t i = 0; i < packets; i++)
    std::cout << boost::format(" %12d %12.3f %12.3f %12.3f %12.3f") % seqnums[i] % (count1[i] / cal) % (count2[i] / cal) % (count3[i] / cal) % (count4[i] / cal) << std::endl;
  if(!rx_stop_flag)
    set_rx_stop_flag();
  LOGGER(info) << "sequence timer thread finished";
}

void rx_printer(const size_t max_packets, const size_t packet_size) {
  LOGGER(info) << "rx printer thread started (" << max_packets <<" packets)";

  static const size_t buffer_max_packets = 16;
  std::array<rx_buffer_element, buffer_max_packets> buffer;
  size_t packets = 0;

  while(packets < max_packets and !rx_stop_flag) {
    while(rx_queue.read_available()) {
      size_t n_popped = rx_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        LOGGER(info) << "rx_printer recieved packet with " << buffer[i].bytes_used << " bytes";
        packets++;
      }
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }
  if(!rx_stop_flag)
    set_rx_stop_flag();
  LOGGER(info) << "rx_printer thread finished";
}

void receiver(const boost::asio::ip::udp::endpoint &local_endpoint, const bool broadcast)
{
  LOGGER(info) << "receiver thread started";

  if(set_current_thread_priority_realtime(10))
    LOGGER(error) << "unable to set realtime thread priority";

  boost::asio::io_service io_service;
  udp_rx server(io_service, local_endpoint, broadcast);
  io_service.run();
  LOGGER(info) << "receiver thread finished";
}

void multicast_receiver(const boost::asio::ip::udp::endpoint &local_endpoint, const boost::asio::ip::address_v4 &multicast_ip)
{
  LOGGER(info) << "multicast receiver thread started";

  if(set_current_thread_priority_realtime(10))
    LOGGER(error) << "unable to set realtime thread priority";

  boost::asio::io_service io_service;
  udp_rx server(io_service, local_endpoint, multicast_ip);
  io_service.run();
  LOGGER(info) << "multicast receiver thread finished";
}