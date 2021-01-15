#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

#include "vxsdr_header.hpp"

// parameters of the internal ring buffers
// size of largest data packet
static constexpr size_t data_buffer_max_bytes        = sizeof(packet_header) + sizeof(std::complex<int16_t>) * MAX_DATA_LENGTH_SAMPLES;
static constexpr size_t tx_data_queue_length         =     1024;    // slots in tx data queue

// parameters for the send thread
static constexpr int    tx_send_thread_sleep_us      =        1;

// parameters for the generator thread
static constexpr int    tx_generator_thread_sleep_us =        1;

// parameters used to set the OS's network buffer
static constexpr unsigned network_send_buffer_bytes  = 16777216;

static std::atomic<bool> tx_stop_flag;

void set_tx_stop_flag() {
  tx_stop_flag = true;
}

static boost::lockfree::spsc_queue<data_packet, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;

class udp_tx {
  public:
    udp_tx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast) : socket(io_context, local_endpoint) {
      destination = destination_endpoint;
      socket.set_option(boost::asio::socket_base::broadcast(broadcast));
      socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));

      boost::asio::socket_base::send_buffer_size option;
      socket.get_option(option);
      int network_buffer_size = option.value();
      LOGGER(info) << "network send buffer size set to " << network_buffer_size;

      start_send();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint destination;
    static constexpr size_t tx_buffer_size = 256;
    std::array<data_packet, tx_buffer_size> send_buffer;
    size_t send_errors = 0, cmds_sent = 0, buffers_sent = 0, packets_sent = 0, bytes_sent = 0;

    void send_device_get_buffer_info_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      packet_header hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_INFO, subdev, flags, sizeof(hdr), (uint16_t)(packets_sent++ % UINT16_MAX) };
      size_t bytes = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      bytes_sent += bytes;
    }

    void send_device_get_buffer_use_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      packet_header hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_USE, subdev, flags, sizeof(hdr), (uint16_t)(packets_sent++ % UINT16_MAX) };
      size_t bytes = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      bytes_sent += bytes;
    }

    void send_device_clear_status_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      packet_header hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLEAR_STATUS, subdev, flags, sizeof(hdr), (uint16_t)(packets_sent++ % UINT16_MAX) };
      size_t bytes = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      bytes_sent += bytes;
    }

    void send_device_get_status_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      packet_header hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_STATUS, subdev, flags, sizeof(hdr), (uint16_t)(packets_sent++ % UINT16_MAX) };
      size_t bytes = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      bytes_sent += bytes;
    }

    void send_tx_start_now_until_stop_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      packet_header hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(hdr), (uint16_t)(packets_sent++ % UINT16_MAX) };
      size_t bytes = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      bytes_sent += bytes;
    }

    void send_tx_stop_now_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      packet_header hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(hdr), (uint16_t)(packets_sent++ % UINT16_MAX) };
      size_t bytes = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      bytes_sent += bytes;
    }
    void send_data_buffer(data_packet &buffer) {
      buffer.hdr.sequence_counter = packets_sent++ % UINT16_MAX;
      size_t bytes = socket.send_to(boost::asio::buffer(&buffer, buffer.hdr.packet_size), destination);
      if(bytes != buffer.hdr.packet_size)
        send_errors++;
      else
        buffers_sent++;
      bytes_sent += bytes;
    }

    void async_send_data_buffer(data_packet &buffer) {
      static auto hdlr = boost::bind(&udp_tx::handle_async_send, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred);
      buffer.hdr.sequence_counter = packets_sent++ % UINT16_MAX;
      socket.async_send_to(boost::asio::buffer(&buffer, buffer.hdr.packet_size), destination, hdlr);
    }

    void handle_async_send(const boost::system::error_code& error, size_t bytes_transferred) {
      if(error) {
        send_errors++;
        LOGGER(error) << "async send error: " << error.message();
      }
      else
        buffers_sent++;
      bytes_sent += bytes_transferred;
    }

    void start_send() {
      LOGGER(info) << "send started";
      //auto hdlr = boost::bind(&udp_tx::handle_async_send, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred);
      while(!tx_stop_flag) {
        if(tx_data_queue.read_available()) {
          size_t n_popped = tx_data_queue.pop(&send_buffer.front(), send_buffer.size());
          for(size_t i = 0; i < n_popped; i++) {
            // cycle count when sent
            //send_buffer_[i].ts[1] = read_cycle_counter();
            async_send_data_buffer(send_buffer[i]);
          }
        }
        //else
        //  LOGGER(error) << "tx queue empty";
      }
    }

};

void generator(const size_t max_packets, const size_t requested_packet_size, const unsigned packet_wait_us) {
  std::array<data_packet, 17> buf;
  LOGGER(info) << "generator thread started";

  //if(set_current_thread_priority_realtime(10))
  //  LOGGER(error) << "unable to set generator thread realtime priority";

  //if(set_current_thread_affinity(3))
  //  LOGGER(error) << "unable to set generator thread CPU affinity";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > data_buffer_max_bytes) {
    LOGGER(error) << "requested packet size is greater than maximum (" << data_buffer_max_bytes <<"); setting to maximum";
    packet_size = data_buffer_max_bytes;
  }

  const double a = 16000;
  const float ncycles = 9117.; // number of cycles in buf.size() buffers; must be integer to avoid skips
  const double w = (2 * M_PI * ncycles) / (buf.size() * MAX_DATA_LENGTH_SAMPLES);

  for(unsigned i = 0; i < buf.size(); i++) {
    buf[i].hdr.packet_type = PACKET_TYPES::TX_SIGNAL_DATA;
    buf[i].hdr.command = 0;
    buf[i].hdr.target_subdevice = 0;
    buf[i].hdr.flags = 0;
    buf[i].hdr.packet_size = packet_size;
    size_t n_samples = (packet_size - sizeof(packet_header)) / sizeof(std::complex<int16_t>);
    for(unsigned j = 0; j < n_samples; j++) {
      buf[i].data[j].real(std::round(a * cos(w * (MAX_DATA_LENGTH_SAMPLES * i + j))));
      buf[i].data[j].imag(std::round(a * sin(w * (MAX_DATA_LENGTH_SAMPLES * i + j))));
    }
  }

  auto t0 = std::chrono::steady_clock::now();

  size_t nsent = 0;

  while(true) {
    // wait for room in the tx queue
    while(!tx_data_queue.write_available()) {
      if(tx_generator_thread_sleep_us > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(tx_generator_thread_sleep_us));
    }
    while(tx_data_queue.write_available() and nsent <= max_packets) {
      tx_data_queue.push(buf[nsent++ % buf.size()]);
    }
    if(tx_stop_flag or nsent > max_packets) {
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << (nsent - 1) << " buffers (" << ((nsent - 1) * packet_size) << " bytes) generated in " << dt.count() << " seconds";

  set_tx_stop_flag();

  LOGGER(info) << "generator thread finished";
}


void sender(const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast)
{
  LOGGER(info) << "sender thread started";

  //if(set_current_thread_priority_realtime(10))
  //  LOGGER(error) << "unable to set realtime thread priority";

  boost::asio::io_context io_context;
  udp_tx server(io_context, local_endpoint, destination_endpoint, broadcast);
  io_context.run();
  LOGGER(info) << "sender thread finished";
}
