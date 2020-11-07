#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

// parameters of the internal ring buffers
static const size_t rx_buffer_max_bytes          =  4104;    // size of largest packet
static const size_t rx_queue_length              = 65536;    // rx_buffers in queue
static const size_t tx_buffer_max_bytes          =  4104;    // max mtu
static const size_t tx_queue_length              =  1024;    // tx_buffers in queue

// parameters of the writer thread's buffers
static const size_t rx_write_buffer_size         =    16;     // rx_buffers
static const int    rx_write_thread_sleep_us     =   100;
static const int     rx_queue_clear_sleep_us     = 10000;

// parameters used to set the OS's network buffer
static const unsigned packets_per_network_buffer =  1024;

static std::atomic<bool> rx_stop_flag, tx_stop_flag;

void set_rx_stop_flag() {
  rx_stop_flag = true;
}

void set_tx_stop_flag() {
  tx_stop_flag = true;
}

typedef struct {
  uint16_t seq_lo;
  uint16_t seq_hi;
  uint16_t data_id;
  uint16_t pad;
  std::array<int16_t, 2048> data;
} buffer_element;

static boost::lockfree::spsc_queue<buffer_element, boost::lockfree::capacity<rx_queue_length> > rx_queue;
static boost::lockfree::spsc_queue<buffer_element, boost::lockfree::capacity<tx_queue_length> > tx_queue;

class udp_rx {
  public:
    udp_rx(boost::asio::io_service &io_service, const boost::asio::ip::udp::endpoint &local_endpoint)
      : socket(io_service, local_endpoint) {
      socket.set_option(boost::asio::socket_base::receive_buffer_size(packets_per_network_buffer * rx_buffer_max_bytes));
      if(!rx_stop_flag)
        start_receive();
    }
    udp_rx(boost::asio::io_service &io_service, const boost::asio::ip::udp::endpoint &local_endpoint,
               const boost::asio::ip::address_v4 &multicast_ip)
      : socket(io_service, local_endpoint.protocol()) {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket.bind(local_endpoint);
      socket.set_option(boost::asio::ip::multicast::join_group(multicast_ip));
      socket.set_option(boost::asio::socket_base::receive_buffer_size(packets_per_network_buffer * rx_buffer_max_bytes));
      if(!rx_stop_flag)
        start_receive();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint remote;
    buffer_element recv_buffer;

    void start_receive() {
      auto hdlr = boost::bind(&udp_rx::handle_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred);
      socket.async_receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote, hdlr);
    }
    void handle_receive(const boost::system::error_code& error, std::size_t bytes_transferred) {
      static size_t packets = 0;
      if (!error) {
        packets++;
        rx_queue.push(recv_buffer);
      }
      else
        LOGGER(error) << "async receive error: " << error.message();
      if(!rx_stop_flag)
        start_receive();
    }
};

class udp_tx {
  public:
    udp_tx(boost::asio::io_service &io_service, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast) : socket(io_service, local_endpoint) {
      destination = destination_endpoint;
      socket.set_option(boost::asio::socket_base::broadcast(broadcast));
      socket.set_option(boost::asio::socket_base::send_buffer_size(packets_per_network_buffer * tx_buffer_max_bytes));
      start_send();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint destination;
    static constexpr size_t tx_buffer_size_ = 256;
    std::array<buffer_element, tx_buffer_size_> send_buffer_;
    unsigned send_errors_ = 0;

    void start_send() {
      LOGGER(info) << "send started";
      auto hdlr = boost::bind(&udp_tx::handle_send, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred);
      while(!tx_stop_flag) {
        if(tx_queue.read_available()) {
          size_t n_popped = tx_queue.pop(&send_buffer_.front(), send_buffer_.size());
          for(size_t i = 0; i < n_popped; i++) {
            socket.async_send_to(boost::asio::buffer(&send_buffer_[i], sizeof(buffer_element)), destination, hdlr);
          }
        }
      }
    }
    void handle_send(const boost::system::error_code& error, std::size_t bytes_transferred) {
      if (error) {
        send_errors_++;
        LOGGER(error) << "async send error: " << error.message();
      }
    }
};

void writer(const std::string &outfilename, const size_t max_packets) {
  LOGGER(info) << "writer thread started";

  std::array<buffer_element, rx_write_buffer_size> buffer;
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);
  size_t packets = 0, seq_errors = 0, write_errors = 0;
  size_t last_seq = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets and !rx_stop_flag) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
    while(rx_queue.read_available() >= rx_write_buffer_size) {
    	size_t nwrite = rx_queue.pop(&buffer.front(), rx_write_buffer_size);
      if(nwrite > max_packets - packets)
        nwrite = max_packets - packets;
      for(size_t i = 0; i < nwrite; i++) {
        size_t seq = (((size_t)buffer[i].seq_hi) << 16) + buffer[i].seq_lo;
        if(packets > 0 and seq != last_seq + 1 and last_seq != UINT32_MAX)
          seq_errors++;
        packets++;
        last_seq = seq;
        outfile.write((const char*)&buffer[i].data.front(), sizeof(buffer[i].data));
        if(outfile.bad())
          write_errors++;
      }
      packets += nwrite;
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }

  outfile.close();

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec";
  LOGGER(info) << seq_errors << " sequence errors, " << write_errors << " write errors";
  LOGGER(info) << (packets * sizeof(buffer_element) / dt.count()) << " bytes/s, " << (8 * packets * sizeof(buffer_element) / dt.count()) << " bits/s";

  // set stop flag so pushing stops
  if(!rx_stop_flag)
    set_rx_stop_flag();
  std::this_thread::sleep_for(std::chrono::microseconds(rx_queue_clear_sleep_us));
  // empty the queue
  while(rx_queue.read_available())
    rx_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "writer thread finished";
}

void sequence_checker(const size_t max_packets, const size_t packet_size) {
  LOGGER(info) << "sequence checker thread started";

  const unsigned buffer_max_packets = 256;
  static std::array<buffer_element, buffer_max_packets> buffer;
  size_t packets = 0, errors = 0;
  size_t last_seq = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets and !rx_stop_flag) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
    while(rx_queue.read_available()) {
      size_t n_popped = rx_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        size_t seq = (((size_t)buffer[i].seq_hi) << 16) + buffer[i].seq_lo;
        if(packets > 0 and seq != last_seq + 1 and last_seq != UINT32_MAX)
          errors++;
        packets++;
        last_seq = seq;
      }
    }
    //std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec; " << errors << " out of sequence";
  LOGGER(info) << (packets * packet_size / dt.count()) << " bytes/s, " << (8 * packets * packet_size / dt.count()) << " bits/s";

  // set stop flag so pushing stops
  if(!rx_stop_flag)
    set_rx_stop_flag();
  std::this_thread::sleep_for(std::chrono::microseconds(rx_queue_clear_sleep_us));
  // empty the queue
  while(rx_queue.read_available())
    rx_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "sequence checker thread finished";
}

void receiver(boost::asio::io_service io_service, const boost::asio::ip::udp::endpoint &local_endpoint)
{
  LOGGER(info) << "receiver thread started";

  if(set_current_thread_priority_realtime(50))
    LOGGER(error) << "unable to set realtime thread priority";

  if(set_current_thread_affinity(0))
    LOGGER(error) << "unable to set thread CPU affinity";

  udp_rx server(io_service, local_endpoint);
  io_service.run();
  LOGGER(info) << "receiver thread finished";
}

void multicast_receiver(boost::asio::io_service io_service, const boost::asio::ip::udp::endpoint &local_endpoint, const boost::asio::ip::address_v4 &multicast_ip)
{
  LOGGER(info) << "multicast receiver thread started";

  if(set_current_thread_priority_realtime(50))
    LOGGER(error) << "unable to set realtime thread priority";

  if(set_current_thread_affinity(0))
    LOGGER(error) << "unable to set thread CPU affinity";

  boost::asio::io_service io_service;
  udp_rx server(io_service, local_endpoint, multicast_ip);
  io_service.run();
  LOGGER(info) << "multicast receiver thread finished";
}

void generator(const size_t max_packets, const size_t requested_packet_size, const unsigned packet_wait_us) {
  buffer_element buf;
  LOGGER(info) << "generator thread started";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > tx_buffer_max_bytes) {
    LOGGER(error) << "requested packet size is greater than maximum (" << tx_buffer_max_bytes <<"); setting to maximum";
    packet_size = tx_buffer_max_bytes;
  }

  auto t0 = std::chrono::steady_clock::now();

  for(size_t i = 1; i <= max_packets and !tx_stop_flag; i++) {
    memset(&buf, 0, sizeof(buf));
    buf.seq_lo = i % 65536;
    buf.seq_hi = i >> 16;

    // wait for room in the tx queue
    while(!tx_queue.write_available()) {}

    tx_queue.push(buf);
    if(packet_wait_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(packet_wait_us));
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << max_packets << " packets (" << (max_packets * packet_size) << " bytes) sent in " << dt.count() << " seconds";

  if(!tx_stop_flag)
    set_tx_stop_flag();
  LOGGER(info) << "generator thread finished";
}

void sender(boost::asio::io_service io_service,  const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast)
{
  LOGGER(info) << "sender thread started";

  //if(set_current_thread_priority_realtime(10))
  //  LOGGER(error) << "unable to set realtime thread priority";

  udp_tx server(io_service, local_endpoint, destination_endpoint, broadcast);
  io_service.run();
  LOGGER(info) << "sender thread finished";
}