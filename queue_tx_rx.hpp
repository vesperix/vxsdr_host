#include <vector>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

// parameters of the internal ring buffers
 // size of largest command/status message packet
static constexpr size_t msg_buffer_max_bytes           = sizeof(packet_header) + MAX_PAYLOAD_LENGTH_BYTES;
// size of largest data packet
static constexpr size_t data_buffer_max_bytes          = sizeof(packet_header) + sizeof(std::complex<int16_t>) * MAX_DATA_LENGTH_SAMPLES;

static constexpr size_t rx_data_queue_length           =     4096;    // slots in rx data queue
static constexpr size_t tx_data_queue_length           =     4096;    // slots in tx data queue
static constexpr size_t rx_msg_queue_length            =     1024;    // slots in rx msg queue
static constexpr size_t tx_msg_queue_length            =     1024;    // slots in tx msg queue

// parameters of the writer thread
static constexpr size_t rx_write_buffer_size           =       16;     // rx_buffers
static constexpr int    rx_write_thread_sleep_us       =        0;
static constexpr int    rx_data_queue_clear_sleep_us   =    10000;

// parameters for the send thread
static constexpr int    send_thread_sleep_us           =       10;

// parameters for the receive thread
static constexpr int    receive_thread_sleep_us        =        0;

// parameters for the generator thread
static constexpr int    tx_generator_thread_sleep_us   =        1;

// parameters for the command response processing thread
static constexpr int    response_thread_sleep_us       =      100;

// parameters used to set the OS's network buffer
static constexpr unsigned network_send_buffer_bytes    =   4194304;
static constexpr unsigned network_receive_buffer_bytes =   4194304;

static std::atomic<bool> rx_stop_flag, tx_stop_flag, complete_flag;
static std::atomic<unsigned> tx_buffer_size, tx_buffer_used;
static std::atomic<unsigned> rx_buffer_size, rx_buffer_used;
static std::atomic<float> tx_buffer_fill_percent, rx_buffer_fill_percent;

void set_rx_stop_flag() {
  rx_stop_flag = true;
}

void set_tx_stop_flag() {
  tx_stop_flag = true;
}

void set_complete_flag() {
  complete_flag = true;
}

static boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
static boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;

static boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<rx_msg_queue_length> > command_queue;
static boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<tx_msg_queue_length> > response_queue;

std::string packet_type_to_name(const uint8_t number) {
  switch(number) {
    case PACKET_TYPES::TX_SIGNAL_DATA:
      return "TX_SIGNAL_DATA";
    case PACKET_TYPES::RX_SIGNAL_DATA:
      return "RX_SIGNAL_DATA";
    case PACKET_TYPES::DEVICE_CMD:
      return "DEVICE_CMD";
    case PACKET_TYPES::TX_RADIO_CMD:
      return "TX_RADIO_CMD";
    case PACKET_TYPES::RX_RADIO_CMD:
      return "RX_RADIO_CMD";
    case PACKET_TYPES::DEVICE_CMD_ERR:
      return "DEVICE_CMD_ERR";
    case PACKET_TYPES::TX_RADIO_CMD_ERR:
      return "TX_RADIO_CMD_ERR";
    case PACKET_TYPES::RX_RADIO_CMD_ERR:
      return "RX_RADIO_CMD_ERR";
    case PACKET_TYPES::DEVICE_CMD_RSP:
      return "DEVICE_CMD_RSP";
    case PACKET_TYPES::TX_RADIO_CMD_RSP:
      return "TX_RADIO_CMD_RSP";
    case PACKET_TYPES::RX_RADIO_CMD_RSP:
      return "RX_RADIO_CMD_RSP";
    case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
      return "TX_SIGNAL_DATA_ACK";
    case PACKET_TYPES::RX_SIGNAL_DATA_ACK:
      return "RX_SIGNAL_DATA_ACK";
    case PACKET_TYPES::DEVICE_CMD_ACK:
      return "DEVICE_CMD_ACK";
    case PACKET_TYPES::TX_RADIO_CMD_ACK:
      return "TX_RADIO_CMD_ACK";
    case PACKET_TYPES::RX_RADIO_CMD_ACK:
      return "RX_RADIO_CMD_ACK";
  }
  return "UNDEFINED";
}

class udp_rx {
  public:
    udp_rx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
      : socket(io_context, local_endpoint) {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));

      boost::asio::socket_base::receive_buffer_size option;
      socket.get_option(option);
      int network_buffer_size = option.value();
      LOGGER(info) << "network receive buffer size set to " << network_buffer_size;

      if(not rx_stop_flag)
        start_receive();
    }
    udp_rx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
               const boost::asio::ip::address_v4 &multicast_ip)
      : socket(io_context, local_endpoint.protocol()) {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket.bind(local_endpoint);
      socket.set_option(boost::asio::ip::multicast::join_group(multicast_ip));
      socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));

      boost::asio::socket_base::receive_buffer_size option;
      socket.get_option(option);
      int network_buffer_size = option.value();
      LOGGER(info) << "network receive buffer size set to " << network_buffer_size;

      if(not rx_stop_flag)
        start_receive();
    }
  private:
    const bool forward_acks = false;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint remote;
    largest_data_packet recv_buffer;
    largest_cmd_or_rsp_packet cmd_buffer;
    uint64_t packets_received = 0, seq_errors = 0;
    std::array<uint64_t, UINT8_MAX> packet_types_received = { 0 };
    uint16_t seq = 0, last_seq = 0;
    unsigned percent, min_buffer_percent = 100, max_buffer_percent = 0;
    void start_receive() {
      LOGGER(info) << "start_receive started";
      // FIXME: need to add timeout if no packets are coming in
      while(not rx_stop_flag) {
        socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
        packets_received++;
        packet_types_received[recv_buffer.hdr.packet_type]++;
        if(packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1))
          seq_errors++;
        last_seq = recv_buffer.hdr.sequence_counter;
        switch(recv_buffer.hdr.packet_type) {
          // receive buffers go into the rx data queue
          case PACKET_TYPES::RX_SIGNAL_DATA:
            rx_data_queue.push(recv_buffer);
            break;
          // tx sginal data acks special cased since they contain throttling information
          case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
            {
              tx_signal_data_ack_packet *r = reinterpret_cast<tx_signal_data_ack_packet *>(&recv_buffer);
              tx_buffer_used = r->tx_buffer_used_bytes;
              tx_buffer_fill_percent = std::min(100., (100. * tx_buffer_used) / tx_buffer_size);
              if(forward_acks) {
                largest_cmd_or_rsp_packet p;
                memcpy(&p, r, std::min((size_t)r->hdr.packet_size, sizeof(p)));
                response_queue.push(p);
              }
            }
            break;
          // the buffer info commands are special cased since they can also be used for TX throttling
          case PACKET_TYPES::DEVICE_CMD_RSP:
            switch(recv_buffer.hdr.command) {
              case DEVICE_CMD_RSP::GET_BUFFER_USE_RSP:
                {
                  get_buffer_use_rsp_packet *r = reinterpret_cast<get_buffer_use_rsp_packet *>(&recv_buffer);
                  tx_buffer_used = r->tx_buffer_used_bytes;
                  rx_buffer_used = r->rx_buffer_used_bytes;
                  tx_buffer_fill_percent = std::min(100., (100. * tx_buffer_used) / tx_buffer_size);
                  rx_buffer_fill_percent = std::min(100., (100. * rx_buffer_used) / rx_buffer_size);
                  //largest_cmd_or_rsp_packet p;
                  //memcpy(&p, r, std::min((size_t)r->hdr.packet_size, sizeof(p)));
                  //response_queue.push(p);
                }
                break;
              case DEVICE_CMD_RSP::GET_BUFFER_INFO_RSP:
                {
                  get_buffer_info_rsp_packet *r = reinterpret_cast<get_buffer_info_rsp_packet *>(&recv_buffer);
                  tx_buffer_size = r->tx_buffer_size_bytes;
                  rx_buffer_size = r->rx_buffer_size_bytes;
                  largest_cmd_or_rsp_packet p;
                  memcpy(&p, r, std::min((size_t)r->hdr.packet_size, sizeof(p)));
                  response_queue.push(p);
                }
                break;
              default:
                {
                  largest_cmd_or_rsp_packet p;
                  memcpy(&p, &recv_buffer, std::min((size_t)recv_buffer.hdr.packet_size, sizeof(p)));
                  response_queue.push(p);
                }
                break;
            }
            break;
          case PACKET_TYPES::DEVICE_CMD_ACK:
          case PACKET_TYPES::RX_RADIO_CMD_ACK:
          case PACKET_TYPES::TX_RADIO_CMD_ACK:
            if(forward_acks) {
              largest_cmd_or_rsp_packet p;
              memcpy(&p, &recv_buffer, std::min((size_t)recv_buffer.hdr.packet_size, sizeof(p)));
              response_queue.push(p);
            }
            break;
          // DEVICE_CMD_RSP handled above
          case PACKET_TYPES::DEVICE_CMD_ERR:
          case PACKET_TYPES::RX_RADIO_CMD_RSP:
          case PACKET_TYPES::RX_RADIO_CMD_ERR:
          case PACKET_TYPES::TX_RADIO_CMD_RSP:
          case PACKET_TYPES::TX_RADIO_CMD_ERR:
            {
              largest_cmd_or_rsp_packet p;
              memcpy(&p, &recv_buffer, std::min((size_t)recv_buffer.hdr.packet_size, sizeof(p)));
              response_queue.push(p);
            }
            break;
          // packet types that should never show up at the host are dropped
          case PACKET_TYPES::TX_SIGNAL_DATA:
          case PACKET_TYPES::DEVICE_CMD:
          case PACKET_TYPES::RX_RADIO_CMD:
          case PACKET_TYPES::TX_RADIO_CMD:
          default: // unrecognized packet types
            break;
        }
        if(receive_thread_sleep_us > 0)
          std::this_thread::sleep_for(std::chrono::microseconds(receive_thread_sleep_us));
      }

      LOGGER(info) << "start_receive exiting; " << packets_received << " packets received, " << seq_errors << " sequence errors";
      for(unsigned i = 0; i < packet_types_received.size(); i++)
        if(packet_types_received[i] > 0)
          LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_received[i] % packet_type_to_name(i) % i);
    }
};

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

      if(not tx_stop_flag)
        start_send();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint destination;
    std::array<uint64_t, UINT8_MAX> packet_types_processed = { 0 };
    uint64_t send_errors = 0, packets_sent = 0, bytes_sent = 0;

    void send_device_hello_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::HELLO, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_device_reset_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::RESET, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_device_open_session_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::OPEN_SESSION, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_device_close_session_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLOSE_SESSION, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_device_get_buffer_info_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_INFO, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_device_get_buffer_use_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_USE, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_device_clear_status_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLEAR_STATUS, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_device_get_status_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_STATUS, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_tx_start_now_until_stop_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_rx_start_now_until_stop_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_tx_start_now_for_n_samples_cmd(const uint64_t n, const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet_samples p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      p.n_samples = n;
      send_packet(p);
    }

    void send_rx_start_now_for_n_samples_cmd(const uint64_t n, const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet_samples p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(packet_header), 0 };
      p.n_samples = n;
      send_packet(p);
    }

    void send_tx_stop_now_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_rx_stop_now_cmd(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(packet_header), 0 };
      send_packet(p);
    }

    void send_packet(packet &packet) {
      packet_types_processed[packet.hdr.packet_type]++;
      packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (1u << 16));
      size_t bytes = socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination);
      if(bytes != packet.hdr.packet_size)
        send_errors++;
      bytes_sent += bytes;
    }

    void start_send() {
      LOGGER(info) << "start_send started";
      static constexpr size_t data_buffer_size = 16, cmd_buffer_size = 16;
      std::array<largest_data_packet, data_buffer_size> data_buffer;
      std::array<largest_cmd_or_rsp_packet, cmd_buffer_size> cmd_buffer;

      unsigned tx_throttle_wait_us = 0;
      constexpr unsigned throttle_start_percent = 75;
      constexpr unsigned throttle_stop_percent  = 50;
      constexpr unsigned throttle_amount_us =     10; // send time for 8200 bytes over 10 GbE is about 6.7 us

      const uint8_t device_subdev = 0, tx_subdev = 0, rx_subdev = 0;

      send_tx_stop_now_cmd(tx_subdev);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      // get device buffer sizes
      send_device_get_buffer_info_cmd(device_subdev);
      // wait for the response come back and be processed
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      // clear the status registers
      send_device_clear_status_cmd(device_subdev);
      send_device_get_status_cmd(device_subdev);

      uint64_t last_check = 0;
      constexpr uint64_t buffer_check_interval = 100; // current buffer size is about 1000 data packets

      while(not tx_stop_flag) {
        if(command_queue.read_available()) {
          size_t n_popped = command_queue.pop(&cmd_buffer.front(), cmd_buffer.size());
          for(size_t i = 0; i < n_popped; i++) {
            send_packet(cmd_buffer[i]);
          }
        }
        else if(tx_data_queue.read_available()) {
          size_t n_popped = tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
          for(size_t i = 0; i < n_popped; i++) {
            if(packets_sent - last_check > buffer_check_interval) {
              // request ack to update buffer use
              data_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
              send_packet(send_buffer[i]);
              last_check = packets_sent;
            }
            else {
              send_packet(data_buffer[i]);
            }
            // update throttling
            if(tx_buffer_fill_percent > throttle_start_percent)
              tx_throttle_wait_us = throttle_amount_us;
            else if (tx_buffer_fill_percent < throttle_stop_percent)
              tx_throttle_wait_us = 0;

            if(tx_throttle_wait_us > 0)
              std::this_thread::sleep_for(std::chrono::microseconds(tx_throttle_wait_us));
          }
        }
        else {
          if(send_thread_sleep_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(send_thread_sleep_us));
        }
      }
      // get the status at the end
      send_device_get_status_cmd(device_subdev);
      // send a stop
      //send_tx_stop_now_cmd(tx_subdev);
      //send_rx_stop_now_cmd(rx_subdev);
      // wait a bit for things to finish up
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      LOGGER(info) << "start_send exiting; " << packets_sent << " packets sent, " << send_errors << " send errors";
      for(unsigned i = 0; i < packet_types_processed.size(); i++)
        if(packet_types_processed[i] > 0)
          LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_processed[i] % packet_type_to_name(i) % i);

    }
};

void writer(const std::string &outfilename, const uint64_t max_packets) {
  LOGGER(info) << "writer thread started";

  std::array<largest_data_packet, rx_write_buffer_size> buffer;
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);
  uint64_t packets = 0, write_errors = 0, bytes_written = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets) {
    while(rx_data_queue.read_available() >= rx_write_buffer_size) {
    	size_t nwrite = rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);
      if(nwrite > max_packets - packets)
        nwrite = max_packets - packets;
      for(size_t i = 0; i < nwrite; i++) {
        packets++;
        size_t data_bytes = (buffer[i].hdr.packet_size - sizeof(packet_header));
        outfile.write(reinterpret_cast<const char*>(&buffer[i].data[0]), data_bytes);
        if(outfile.bad())
          write_errors++;
        else
          bytes_written += data_bytes;
      }
      packets += nwrite;
    }
    if(rx_write_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));
    if(rx_stop_flag) {
      break;
    }
  }

  outfile.close();

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " data packets recieved in " << dt.count() << " sec";
  LOGGER(info) << write_errors << " write errors";
  LOGGER(info) << (bytes_written / dt.count()) << " bytes/s";

  std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_clear_sleep_us));
  // empty the queue
  while(rx_data_queue.read_available())
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "writer thread finished";
}

void data_packet_counter(const uint64_t max_packets, const size_t packet_size) {
  LOGGER(info) << "data_packet counter thread started";

  const unsigned buffer_max_packets = 256;
  static std::array<largest_data_packet, buffer_max_packets> buffer;
  uint64_t packets = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets) {
    while(rx_data_queue.read_available()) {
      size_t n_popped = rx_data_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        packets++;
      }
    }
    if(rx_write_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));

    if(rx_stop_flag) {
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " data packets recieved in " << dt.count() << " sec";
  LOGGER(info) << (packets * packet_size / dt.count()) << " bytes/s, " << (8 * packets * packet_size / dt.count()) << " bits/s";

  std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_clear_sleep_us));
  // empty the queue
  while(rx_data_queue.read_available())
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "data packet counter thread finished";
}

void response_packet_printer() {
  LOGGER(info) << "response packet printer thread started";

  const unsigned buffer_max_packets = 256;
  static std::array<largest_cmd_or_rsp_packet, buffer_max_packets> buffer;
  uint64_t packets = 0;

  while(not complete_flag) {
    if(rx_data_queue.read_available()) {
      size_t n_popped = response_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        packets++;
        log_packet_info(buffer[i]);
      }
    }
    if(response_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(response_thread_sleep_us));
  }

  LOGGER(info) << "response packet printer thread finished";
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

void generator(const uint64_t max_packets, const size_t requested_packet_size, const unsigned packet_wait_us) {
  std::array<largest_data_packet, 17> buf;
  LOGGER(info) << "generator thread started";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > data_buffer_max_bytes) {
    LOGGER(error) << "requested packet size is greater than maximum (" << data_buffer_max_bytes <<"); setting to maximum";
    packet_size = data_buffer_max_bytes;
  }

  const double a = 4000;
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

  uint64_t nsent = 0;

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