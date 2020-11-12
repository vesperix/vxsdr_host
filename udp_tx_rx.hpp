#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

//#define SINGLE_CHANNEL

// packet type ids
static constexpr uint16_t TX_DATA_ID               = 0xDACD;
static constexpr uint16_t RX_DATA_ID               = 0xADCD;
static constexpr uint16_t TX_DATA_ACK_ID           = 0xDACA;

// parameters of the internal ring buffers
static const size_t msg_buffer_max_bytes           =    256;    // size of largest command/status message packet
static const size_t data_buffer_max_bytes          =   4104;    // size of largest data packet
static const size_t rx_data_queue_length           =  65536;    // slots in rx data queue
static const size_t tx_data_queue_length           =   1024;    // slots in tx data queue
static const size_t rx_msg_queue_length            =    256;    // slots in rx msg queue
static const size_t tx_msg_queue_length            =    256;    // slots in tx msg queue

// parameters of the writer thread
static const size_t rx_write_buffer_size           =     16;     // rx_buffers
static const int    rx_write_thread_sleep_us       =      1;
static const int    rx_data_queue_clear_sleep_us   =  10000;

// parameters for the send thread
static const int    tx_send_thread_sleep_us        =      1;

// parameters for the generator thread
static const int    tx_generator_thread_sleep_us   =      1;

// parameters used to set the OS's network buffer
static const unsigned packets_per_network_buffer   =   1024;

static std::atomic<bool> rx_stop_flag, tx_stop_flag;
static std::atomic<unsigned> radio_buffer_fill_percent;

void set_rx_stop_flag() {
  rx_stop_flag = true;
}

void set_tx_stop_flag() {
  tx_stop_flag = true;
}

typedef struct {
  uint16_t seq_lo;
  uint16_t seq_hi;
  uint16_t id;
  uint16_t pad;
  std::array<std::complex<int16_t>, 2048> data;
} data_buffer_element;

typedef struct {
  uint16_t seq_lo;
  uint16_t seq_hi;
  uint16_t id;
  uint16_t pad;
  std::array<int16_t, 124> data;
} msg_buffer_element;

typedef struct {
  uint16_t seq_lo;
  uint16_t seq_hi;
  uint16_t id;
  uint16_t pad;
  uint32_t dac_ddr_used;
  uint32_t dac_ddr_size;
} ack_buffer_element;

static boost::lockfree::spsc_queue<data_buffer_element, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
static boost::lockfree::spsc_queue<data_buffer_element, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;

static boost::lockfree::spsc_queue<msg_buffer_element, boost::lockfree::capacity<rx_msg_queue_length> > rx_msg_queue;
static boost::lockfree::spsc_queue<msg_buffer_element, boost::lockfree::capacity<tx_msg_queue_length> > tx_msg_queue;

class udp_rx {
  public:
    udp_rx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
      : socket(io_context, local_endpoint) {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket.set_option(boost::asio::socket_base::receive_buffer_size(packets_per_network_buffer * data_buffer_max_bytes));
      if(!rx_stop_flag)
        start_receive();
    }
    udp_rx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
               const boost::asio::ip::address_v4 &multicast_ip)
      : socket(io_context, local_endpoint.protocol()) {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket.bind(local_endpoint);
      socket.set_option(boost::asio::ip::multicast::join_group(multicast_ip));
      socket.set_option(boost::asio::socket_base::receive_buffer_size(packets_per_network_buffer * data_buffer_max_bytes));
      if(!rx_stop_flag)
        start_receive();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint remote;
    data_buffer_element recv_buffer;
    ack_buffer_element ack_buffer;
    void start_receive() {
      // FIXME: need to add timeout if no packets are coming in
      while(!rx_stop_flag) {
        size_t bytes_received = socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
        if(recv_buffer.id == RX_DATA_ID and bytes_received == sizeof(data_buffer_element))
          rx_data_queue.push(recv_buffer);
        else if(recv_buffer.id == TX_DATA_ACK_ID and bytes_received == sizeof(ack_buffer_element)) {
          memcpy(&ack_buffer, &recv_buffer, sizeof(ack_buffer));
          radio_buffer_fill_percent = (100 * ack_buffer.dac_ddr_used) / ack_buffer.dac_ddr_size;
        }
      }
    }
};

class udp_tx {
  public:
    udp_tx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast) : socket(io_context, local_endpoint) {
      destination = destination_endpoint;
      socket.set_option(boost::asio::socket_base::broadcast(broadcast));
      socket.set_option(boost::asio::socket_base::send_buffer_size(packets_per_network_buffer * data_buffer_max_bytes));
      start_send();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint destination;
    static constexpr size_t tx_buffer_size_ = 256;
    std::array<data_buffer_element, tx_buffer_size_> send_buffer_;
    unsigned send_errors_ = 0;

    void start_send() {
      LOGGER(info) << "send started";
      unsigned tx_throttle_wait_us = 0;
      constexpr unsigned throttle_start_percent = 75;
      constexpr unsigned throttle_stop_percent  = 50;
      constexpr unsigned throttle_amount_us =     10;
      radio_buffer_fill_percent = 0;
      while(!tx_stop_flag) {
        if(tx_data_queue.read_available()) {
          size_t n_popped = tx_data_queue.pop(&send_buffer_.front(), send_buffer_.size());
          for(size_t i = 0; i < n_popped; i++) {
            size_t bytes_sent = socket.send_to(boost::asio::buffer(&send_buffer_[i], sizeof(data_buffer_element)), destination);
            if(bytes_sent != sizeof(data_buffer_element))
              send_errors_++;
            if(tx_throttle_wait_us > 0)
              std::this_thread::sleep_for(std::chrono::microseconds(tx_throttle_wait_us));
          }
          // update throttling
          if(radio_buffer_fill_percent > throttle_start_percent)
            tx_throttle_wait_us = throttle_amount_us;
          else if (radio_buffer_fill_percent < throttle_stop_percent)
            tx_throttle_wait_us = 0;
        }
        else {
          if(tx_send_thread_sleep_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(tx_send_thread_sleep_us));
        }
      }
    }
};

void writer(const std::string &outfilename, const size_t max_packets) {
  LOGGER(info) << "writer thread started";

  std::array<data_buffer_element, rx_write_buffer_size> buffer;
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);
  size_t packets = 0, seq_errors = 0, write_errors = 0;
  size_t last_seq = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
    while(rx_data_queue.read_available() >= rx_write_buffer_size) {
    	size_t nwrite = rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);
      if(nwrite > max_packets - packets)
        nwrite = max_packets - packets;
      for(size_t i = 0; i < nwrite; i++) {
        size_t seq = (((size_t)buffer[i].seq_hi) << 16) + buffer[i].seq_lo;
        if(packets > 0 and seq != last_seq + 1 and last_seq != UINT32_MAX)
          seq_errors++;
        packets++;
        last_seq = seq;
        outfile.write((const char*)&buffer[i].data.front(), buffer[i].data.size());
        if(outfile.bad())
          write_errors++;
      }
      packets += nwrite;
    }
    if(rx_write_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
    if(tx_stop_flag or rx_stop_flag) {
      LOGGER(info) << "writer loop exit: " << tx_stop_flag << " " << rx_stop_flag;
      break;
    }
  }

  outfile.close();

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec";
  LOGGER(info) << seq_errors << " sequence errors, " << write_errors << " write errors";
  LOGGER(info) << (packets * sizeof(data_buffer_element) / dt.count()) << " bytes/s, " << (8 * packets * sizeof(data_buffer_element) / dt.count()) << " bits/s";

  // set stop flag so pushing stops
  set_tx_stop_flag();
  set_rx_stop_flag();
  std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_clear_sleep_us));
  // empty the queue
  while(rx_data_queue.read_available())
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "writer thread finished";
}

void sequence_checker(const size_t max_packets, const size_t packet_size) {
  LOGGER(info) << "sequence checker thread started";

  //if(set_current_thread_priority_realtime(10))
  //  LOGGER(error) << "unable to set sequence checker thread realtime priority";

  //if(set_current_thread_affinity(1))
  //  LOGGER(error) << "unable to set sequence checker thread CPU affinity";

  const unsigned buffer_max_packets = 256;
  static std::array<data_buffer_element, buffer_max_packets> buffer;
  size_t packets = 0, errors = 0;
  size_t last_seq = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
    while(rx_data_queue.read_available()) {
      size_t n_popped = rx_data_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        size_t seq = (((size_t)buffer[i].seq_hi) << 16) + buffer[i].seq_lo;
        if(packets > 0 and seq != last_seq + 1 and last_seq != UINT32_MAX)
          errors++;
        packets++;
        last_seq = seq;
      }
    }
    if(rx_write_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));

    if(tx_stop_flag or rx_stop_flag) {
      LOGGER(info) << "sequence checker loop exit: " << tx_stop_flag << " " << rx_stop_flag;
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec; " << errors << " out of sequence";
  LOGGER(info) << (packets * packet_size / dt.count()) << " bytes/s, " << (8 * packets * packet_size / dt.count()) << " bits/s";

  // set stop flag so pushing stops
  set_tx_stop_flag();
  set_rx_stop_flag();
  std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_clear_sleep_us));
  // empty the queue
  while(rx_data_queue.read_available())
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "sequence checker thread finished";
}

void receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
{
  LOGGER(info) << "receiver thread started";

  if(set_current_thread_priority_realtime(20))
    LOGGER(error) << "unable to set receiver thread realtime priority";

  if(set_current_thread_affinity(0))
    LOGGER(error) << "unable to set receiver thread CPU affinity";

  udp_rx server(io_context, local_endpoint);
  io_context.run();
  LOGGER(info) << "receiver thread finished";
}

void multicast_receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint, const boost::asio::ip::address_v4 &multicast_ip)
{
  LOGGER(info) << "multicast receiver thread started";

  if(set_current_thread_priority_realtime(20))
    LOGGER(error) << "unable to set realtime thread priority";

  if(set_current_thread_affinity(0))
    LOGGER(error) << "unable to set thread CPU affinity";

  udp_rx server(io_context, local_endpoint, multicast_ip);
  io_context.run();
  LOGGER(info) << "multicast receiver thread finished";
}

void generator(const size_t max_packets, const size_t requested_packet_size, const unsigned packet_wait_us) {
  std::array<data_buffer_element, 17> buf;
  LOGGER(info) << "generator thread started";

  if(set_current_thread_priority_realtime(10))
    LOGGER(error) << "unable to set generator thread realtime priority";

  if(set_current_thread_affinity(3))
    LOGGER(error) << "unable to set generator thread CPU affinity";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > data_buffer_max_bytes) {
    LOGGER(error) << "requested packet size is greater than maximum (" << data_buffer_max_bytes <<"); setting to maximum";
    packet_size = data_buffer_max_bytes;
  }

  const double a = 800;
  const float ncycles = 9117.; // number of cycles in buf.size() buffers; must be integer to avoid skips
  const double w = (2 * M_PI * ncycles) / (buf.size() * buf[0].data.size()); 

  for(unsigned i = 0; i < buf.size(); i++) {
    buf[i].id = TX_DATA_ID;
    buf[i].pad = 0;
    for(unsigned j = 0; j < buf[i].data.size(); j++) {
      buf[i].data[j].real(std::round(a * cos(w * (buf[i].data.size() * i + j))));
      buf[i].data[j].imag(std::round(a * sin(w * (buf[i].data.size() * i + j))));
    }
  }

  auto t0 = std::chrono::steady_clock::now();

  for(size_t i = 1; i <= max_packets; i++) {
    unsigned idx = i % buf.size();

    buf[idx].seq_lo = i % 65536;
    buf[idx].seq_hi = i >> 16;

    // wait for room in the tx queue
    while(!tx_data_queue.write_available()) {}

    tx_data_queue.push(buf[idx]);
    if(tx_generator_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(tx_generator_thread_sleep_us));

    if(tx_stop_flag or rx_stop_flag) {
      LOGGER(info) << "generator loop exit: " << tx_stop_flag << " " << rx_stop_flag;
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << max_packets << " packets (" << (max_packets * packet_size) << " bytes) sent in " << dt.count() << " seconds";

  set_tx_stop_flag();
  set_rx_stop_flag();
 
  LOGGER(info) << "generator thread finished";
}

void sender(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast)
{
  LOGGER(info) << "sender thread started";

  if(set_current_thread_priority_realtime(20))
    LOGGER(error) << "unable to set sender thread realtime priority";

  if(set_current_thread_affinity(2))
    LOGGER(error) << "unable to set sender thread CPU affinity";

  udp_tx server(io_context, local_endpoint, destination_endpoint, broadcast);
  io_context.run();
  LOGGER(info) << "sender thread finished";
}