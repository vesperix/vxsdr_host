#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

//#define SINGLE_CHANNEL

// packet type ids
static constexpr uint16_t TX_DATA_ID               = 0xDACD;
static constexpr uint16_t RX_DATA_ID               = 0xADCD;
static constexpr uint16_t TX_DATA_ACK_ID           = 0xDACA;

// parameters of the internal ring buffers
static const size_t msg_buffer_max_bytes           =    256;    // size of largest command/status message packet
static const size_t data_buffer_max_bytes          =   8200;    // size of largest data packet
static const size_t rx_data_queue_length           =  65536;    // slots in rx data queue
static const size_t tx_data_queue_length           =   1024;    // slots in tx data queue
static const size_t rx_msg_queue_length            =    256;    // slots in rx msg queue
static const size_t tx_msg_queue_length            =    256;    // slots in tx msg queue

// parameters of the writer thread
static const size_t rx_write_buffer_size           =     16;     // rx_buffers
static const int    rx_write_thread_sleep_us       =      0;
static const int    rx_data_queue_clear_sleep_us   =  10000;

// parameters for the send thread
static const int    tx_send_thread_sleep_us        =      1;

// parameters for the generator thread
static const int    tx_generator_thread_sleep_us   =      0;

// parameters used to set the OS's network buffer
static const unsigned packets_per_network_buffer   =   1024;

static std::atomic<bool> rx_stop_flag, tx_stop_flag;

void set_rx_stop_flag() {
  rx_stop_flag = true;
}

void set_tx_stop_flag() {
  tx_stop_flag = true;
}

class udp_rx {
  public:
    udp_rx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
      : socket(io_context, local_endpoint) {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket.set_option(boost::asio::socket_base::receive_buffer_size(packets_per_network_buffer * data_buffer_max_bytes));
      if(not rx_stop_flag)
        start_receive();
    }
    udp_rx(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
               const boost::asio::ip::address_v4 &multicast_ip)
      : socket(io_context, local_endpoint.protocol()) {
      socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
      socket.bind(local_endpoint);
      socket.set_option(boost::asio::ip::multicast::join_group(multicast_ip));
      socket.set_option(boost::asio::socket_base::receive_buffer_size(packets_per_network_buffer * data_buffer_max_bytes));
      if(not rx_stop_flag)
        start_receive();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint remote;
    data_packet recv_buffer;
    generic_cmd_or_rsp_packet cmd_buffer;
    size_t packets_received = 0;
    std::array<size_t, 21> packet_types_received;
    //packet_types_received.reserve(PACKET_TYPES.NAMES.size());
    size_t seq = 0, last_seq = 0, seq_errors = 0;
    unsigned percent, min_buffer_percent = 100, max_buffer_percent = 0;
    void start_receive() {
      LOGGER(info) << "receive started";
      while(not rx_stop_flag) {
        socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
        packets_received++;
        if(recv_buffer.hdr.packet_type < 16)
          packet_types_received[recv_buffer.hdr.packet_type]++;
        else
          packet_types_received[16 + (recv_buffer.hdr.packet_type % 128)]++;
        if(packets_received > 1 and recv_buffer.hdr.sequence_counter != last_seq + 1 and last_seq != UINT16_MAX)
          seq_errors++;
        last_seq = recv_buffer.hdr.sequence_counter;
        switch(recv_buffer.hdr.packet_type) {
          case PACKET_TYPES::RX_SIGNAL_DATA:
            // ignored
            break;
          case PACKET_TYPES::TX_SIGNAL_DATA:
            LOGGER(error) << "Received a TX signal data packet";
            break;

          case PACKET_TYPES::DEVICE_CMD_RSP:
          case PACKET_TYPES::DEVICE_CMD_ERR:
          case PACKET_TYPES::DEVICE_CMD_ACK:
          case PACKET_TYPES::RX_RADIO_CMD_RSP:
          case PACKET_TYPES::RX_RADIO_CMD_ERR:
          case PACKET_TYPES::RX_RADIO_CMD_ACK:
          case PACKET_TYPES::TX_RADIO_CMD_RSP:
          case PACKET_TYPES::TX_RADIO_CMD_ERR:
          case PACKET_TYPES::TX_RADIO_CMD_ACK:
            {
              generic_cmd_or_rsp_packet p;
              memcpy(&p, &recv_buffer, std::min((size_t)recv_buffer.hdr.packet_size, sizeof(p)));
              log_packet_info(p);
            }
            break;
          case PACKET_TYPES::DEVICE_CMD:
            LOGGER(error) << "Received a device command packet: " << (int)recv_buffer.hdr.command;
            break;
          case PACKET_TYPES::RX_RADIO_CMD:
            LOGGER(error) << "Received a radio receiver command packet: " << (int)recv_buffer.hdr.command;
            break;
          case PACKET_TYPES::TX_RADIO_CMD:
            LOGGER(error) << "Received a radio transmitter command packet: " << (int)recv_buffer.hdr.command;
            break;
          default:
            LOGGER(info) << "Received unknown packet: PACKET_TYPE " << (int)recv_buffer.hdr.packet_type << " COMMAND " << (int)recv_buffer.hdr.command;
            break;
        }
      }
      LOGGER(info) << packets_received << " packets received:";
      for(unsigned i = 0; i < PACKET_TYPES::NAMES.size(); i++)
        LOGGER(info) << (boost::format("   %12d %s") % packet_types_received[i] % PACKET_TYPES::NAMES[i]);

      LOGGER(info) << "receive finished";
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
      if(not tx_stop_flag)
        start_send();
    }
  private:
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint destination;
    static constexpr size_t tx_buffer_size_ = 256;
    std::array<data_packet, tx_buffer_size_> send_buffer;
    unsigned send_errors = 0, packets_sent = 0, total_bytes = 0;

    void start_send() {
      LOGGER(info) << "send started";
      packet_header hdr;
      hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      hdr.target_subdevice = 0;
      hdr.command = DEVICE_CMD::HELLO;
      hdr.flags = FLAGS::REQUEST_ACK;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = packets_sent % UINT16_MAX;
      size_t bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
      hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      hdr.target_subdevice = 0;
      hdr.command = DEVICE_CMD::GET_STATUS;
      hdr.flags = FLAGS::REQUEST_ACK;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = packets_sent % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      hdr.packet_type = PACKET_TYPES::RX_RADIO_CMD;
      hdr.target_subdevice = 0;
      hdr.command = RADIO_CMD::STOP_NOW;
      hdr.flags = FLAGS::REQUEST_ACK;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = packets_sent % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
 
      dev_read_write_reg_packet p;
      p.hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      p.hdr.target_subdevice = 0;
      p.hdr.command = DEVICE_CMD::READ_DAC_REG;
      p.hdr.flags = FLAGS::REQUEST_ACK;
      p.hdr.packet_size = sizeof(hdr) + 8;
      p.hdr.sequence_counter = packets_sent % UINT16_MAX;
      p.reg_num = 0x2;
      p.value = 0;
      bytes_sent = socket.send_to(boost::asio::buffer(&p, sizeof(p)), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      p.hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      p.hdr.target_subdevice = 0;
      p.hdr.command = DEVICE_CMD::READ_DAC_REG;
      p.hdr.flags = FLAGS::REQUEST_ACK;
      p.hdr.packet_size = sizeof(hdr) + 8;
      p.hdr.sequence_counter = packets_sent % UINT16_MAX;
      p.reg_num = 0xD;
      p.value = 0;
      bytes_sent = socket.send_to(boost::asio::buffer(&p, sizeof(p)), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      dev_read_write_reg_packet q;
      q.hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      q.hdr.target_subdevice = 0;
      q.hdr.command = DEVICE_CMD::WRITE_DAC_REG;
      q.hdr.flags = FLAGS::REQUEST_ACK;
      q.hdr.packet_size = sizeof(hdr) + 8;
      q.hdr.sequence_counter = packets_sent % UINT16_MAX;
      q.reg_num = 0x2;
      q.value = 0x2042;
      bytes_sent = socket.send_to(boost::asio::buffer(&q, sizeof(q)), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      q.hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      q.hdr.target_subdevice = 0;
      q.hdr.command = DEVICE_CMD::WRITE_DAC_REG;
      q.hdr.flags = FLAGS::REQUEST_ACK;
      q.hdr.packet_size = sizeof(hdr) + 8;
      q.hdr.sequence_counter = packets_sent % UINT16_MAX;
      q.reg_num = 0xD;
      q.value = 0x4400;
      bytes_sent = socket.send_to(boost::asio::buffer(&q, sizeof(q)), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      p.hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      p.hdr.target_subdevice = 0;
      p.hdr.command = DEVICE_CMD::READ_DAC_REG;
      p.hdr.flags = FLAGS::REQUEST_ACK;
      p.hdr.packet_size = sizeof(hdr) + 8;
      p.hdr.sequence_counter = packets_sent % UINT16_MAX;
      p.reg_num = 0x2;
      p.value = 0;
      bytes_sent = socket.send_to(boost::asio::buffer(&p, sizeof(p)), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      p.hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      p.hdr.target_subdevice = 0;
      p.hdr.command = DEVICE_CMD::READ_DAC_REG;
      p.hdr.flags = FLAGS::REQUEST_ACK;
      p.hdr.packet_size = sizeof(hdr) + 8;
      p.hdr.sequence_counter = packets_sent % UINT16_MAX;
      p.reg_num = 0xD;
      p.value = 0;
      bytes_sent = socket.send_to(boost::asio::buffer(&p, sizeof(p)), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // now set stop flag and then send a HELLO to get the RX to quit
      set_rx_stop_flag();
      std::this_thread::sleep_for(std::chrono::seconds(1));
      hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      hdr.target_subdevice = 0;
      hdr.command = DEVICE_CMD::HELLO;
      hdr.flags = 0;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = packets_sent % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      packets_sent++;
      total_bytes += bytes_sent;
      std::this_thread::sleep_for(std::chrono::seconds(1));

      LOGGER(info) << "send finished: " << packets_sent << " packets, " << total_bytes << " bytes sent";
    }
};

void test_receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
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

void test_sender(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool broadcast)
{
  LOGGER(info) << "sender thread started";

  //if(set_current_thread_priority_realtime(20))
  //  LOGGER(error) << "unable to set sender thread realtime priority";

  if(set_current_thread_affinity(2))
    LOGGER(error) << "unable to set sender thread CPU affinity";

  udp_tx server(io_context, local_endpoint, destination_endpoint, broadcast);
  io_context.run();
  LOGGER(info) << "sender thread finished";
}