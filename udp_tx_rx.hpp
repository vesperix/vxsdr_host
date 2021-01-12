#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"

#include "vxsdr_header.hpp"

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
static std::atomic<unsigned> tx_buffer_size, tx_buffer_used;
static std::atomic<unsigned> rx_buffer_size, rx_buffer_used;
static std::atomic<unsigned> tx_buffer_fill_percent, rx_buffer_fill_percent;

void set_rx_stop_flag() {
  rx_stop_flag = true;
}

void set_tx_stop_flag() {
  tx_stop_flag = true;
}

static boost::lockfree::spsc_queue<data_packet, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
static boost::lockfree::spsc_queue<data_packet, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;

static boost::lockfree::spsc_queue<generic_cmd_or_rsp_packet, boost::lockfree::capacity<rx_msg_queue_length> > rx_msg_queue;
static boost::lockfree::spsc_queue<generic_cmd_or_rsp_packet, boost::lockfree::capacity<tx_msg_queue_length> > tx_msg_queue;

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
    data_packet recv_buffer;
    generic_cmd_or_rsp_packet cmd_buffer;
    size_t packets_received = 0, packet_types_received[16] = { 0 };
    size_t seq = 0, last_seq = 0, seq_errors = 0;
    unsigned percent, min_buffer_percent = 100, max_buffer_percent = 0;
    void start_receive() {
      // FIXME: need to add timeout if no packets are coming in
      while(!rx_stop_flag) {
        socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
        packets_received++;
        if(recv_buffer.hdr.packet_type < 16)
          packet_types_received[recv_buffer.hdr.packet_type]++;
        if(packets_received > 1 and recv_buffer.hdr.sequence_counter != last_seq + 1 and last_seq != UINT16_MAX)
          seq_errors++;
        last_seq = recv_buffer.hdr.sequence_counter;
        switch(recv_buffer.hdr.packet_type) {
          case PACKET_TYPES::RX_SIGNAL_DATA:
            rx_data_queue.push(recv_buffer);
            break;
          case PACKET_TYPES::TX_SIGNAL_DATA:
            LOGGER(error) << "Received a TX signal data packet";
            break;
          case PACKET_TYPES::DEVICE_CMD_RSP:
            switch(recv_buffer.hdr.command) {
              case DEVICE_CMD_RSP::GET_BUFFER_USE_RSP:
                {
                  get_buffer_use_rsp_packet *r = reinterpret_cast<get_buffer_use_rsp_packet *>(&recv_buffer);
                  tx_buffer_used = r->TX_buffer_used_bytes;
                  rx_buffer_used = r->RX_buffer_used_bytes;
                  tx_buffer_fill_percent = std::min(100., floor((100. * tx_buffer_used) / tx_buffer_size));
                  rx_buffer_fill_percent = std::min(100., floor((100. * rx_buffer_used) / rx_buffer_size));
                }
                break;
              case DEVICE_CMD_RSP::GET_BUFFER_INFO_RSP:
                {
                  get_buffer_info_rsp_packet *r = reinterpret_cast<get_buffer_info_rsp_packet *>(&recv_buffer);
                  tx_buffer_size = r->TX_buffer_size_bytes;
                  rx_buffer_size = r->RX_buffer_size_bytes;
                }
                break;
              case DEVICE_CMD::GET_STATUS:
                {
                  LOGGER(info) << "DEVICE_CMD_RSP: GET_STATUS";
                  status_rsp_packet *sr = reinterpret_cast<status_rsp_packet *>(&recv_buffer);
                  LOGGER(info) << "   fpga_temp                 = " << sr->fpga_temp;
                  LOGGER(info) << "   dac_temp                  = " << sr->dac_temp;
                  LOGGER(info) << "   oos_err_count             = " << sr->oos_err_count;
                  LOGGER(info) << "   datapath_fifo_flow_status = " << sr->datapath_fifo_flow_status;
                  LOGGER(info) << "   ddr_arbiter_status        = " << sr->ddr_arbiter_status;
                  LOGGER(info) << "   tgbe_status               = " << sr->tgbe_status;
                }
                break;
              default:
                LOGGER(info) << "DEVICE_CMD_RSP: " << (int)recv_buffer.hdr.command;
                break;
            }
            break;
          case PACKET_TYPES::RX_RADIO_CMD_RSP:
            LOGGER(info) << "RX_RADIO_CMD_RSP: " << (int)recv_buffer.hdr.command;
            break;
          case PACKET_TYPES::RX_RADIO_CMD_ERR:
            LOGGER(info) << "RX_RADIO_CMD_ERR: " << (int)recv_buffer.hdr.command;
            break;
          case PACKET_TYPES::RX_RADIO_CMD_ACK:
            LOGGER(info) << "RX_RADIO_CMD_ACK: " << (int)recv_buffer.hdr.command;
            break;
          case PACKET_TYPES::TX_RADIO_CMD_RSP:
            LOGGER(info) << "RX_RADIO_CMD_RSP: " << (int)recv_buffer.hdr.command;
            break;
          case PACKET_TYPES::TX_RADIO_CMD_ERR:
            LOGGER(info) << "TX_RADIO_CMD_ERR: " << (int)recv_buffer.hdr.command;
            break;
          case PACKET_TYPES::TX_RADIO_CMD_ACK:
            LOGGER(info) << "TX_RADIO_CMD_ACK: " << (int)recv_buffer.hdr.command;
            break;
          default:
            LOGGER(info) << "PACKET_TYPE: " << (int)recv_buffer.hdr.packet_type << " COMMAND " << (int)recv_buffer.hdr.command;
            break;
        }
      }
      LOGGER(info) << "start_receive exiting; " << packets_received << " packets received:";
      for(unsigned i = 0; i < 16; i++)
        LOGGER(info) << (boost::format("   %12d %s") % packet_types_received[i] % PACKET_TYPES::NAMES[i]);
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
    static constexpr size_t send_buffer_size = 16;
    std::array<data_packet, send_buffer_size> send_buffer;
    unsigned send_errors = 0, cmds_sent = 0, buffers_sent = 0, packets_sent = 0;

    void start_send() {
      LOGGER(info) << "send started";
      unsigned tx_throttle_wait_us = 0;
      constexpr unsigned throttle_start_percent = 75;
      constexpr unsigned throttle_stop_percent  = 50;
      constexpr unsigned throttle_amount_us =     10;

      packet_header hdr;
      // get TX buffer size (necessary for throttling)
      hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      hdr.target_subdevice = 0;
      hdr.command = DEVICE_CMD::GET_BUFFER_INFO;
      hdr.flags = 0;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = (cmds_sent + buffers_sent) % UINT16_MAX;
      size_t bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes_sent != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;

      tx_buffer_fill_percent = 0;

      // clear the error status
      hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      hdr.target_subdevice = 0;
      hdr.command = DEVICE_CMD::CLEAR_STATUS;
      hdr.flags = 0;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = buffers_sent % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes_sent != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;

      // get the status to make sure it's clear
      hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      hdr.target_subdevice = 0;
      hdr.command = DEVICE_CMD::GET_STATUS;
      hdr.flags = 0;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = buffers_sent % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes_sent != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;

      // send a START_NOW_UNTIL_STOP
      hdr.packet_type = PACKET_TYPES::TX_RADIO_CMD;
      hdr.target_subdevice = 0;
      hdr.command = RADIO_CMD::START_NOW_UNTIL_STOP;
      hdr.flags = 0;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = (cmds_sent + buffers_sent) % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes_sent != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;

      size_t last_buffer_use_sent = 0;

      while(!tx_stop_flag) {
        if(tx_data_queue.read_available()) {
          size_t n_popped = tx_data_queue.pop(&send_buffer.front(), send_buffer.size());
          for(size_t i = 0; i < n_popped; i++) {
            send_buffer[i].hdr.sequence_counter = (cmds_sent + buffers_sent) % UINT16_MAX;
            bytes_sent = socket.send_to(boost::asio::buffer(&send_buffer[i], send_buffer[i].hdr.packet_size), destination);
            if(bytes_sent != send_buffer[i].hdr.packet_size)
              send_errors++;
            else
              buffers_sent++;
            if(tx_throttle_wait_us > 0)
              std::this_thread::sleep_for(std::chrono::microseconds(tx_throttle_wait_us));
          }
          // ask for buffer use every every 1000 data packets
          if(buffers_sent - last_buffer_use_sent > 100) {
            last_buffer_use_sent = buffers_sent;
            hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
            hdr.target_subdevice = 0;
            hdr.command = DEVICE_CMD::GET_BUFFER_USE;
            hdr.flags = 0;
            hdr.packet_size = sizeof(hdr);
            hdr.sequence_counter = (cmds_sent + buffers_sent) % UINT16_MAX;
            bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
            if(bytes_sent != hdr.packet_size)
              send_errors++;
            else
              cmds_sent++;
          }
          // update throttling
          if(tx_buffer_fill_percent > throttle_start_percent)
            tx_throttle_wait_us = throttle_amount_us;
          else if (tx_buffer_fill_percent < throttle_stop_percent)
            tx_throttle_wait_us = 0;
        }
        else {
          if(tx_send_thread_sleep_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(tx_send_thread_sleep_us));
        }
      }
      // send a stop
      hdr.packet_type = PACKET_TYPES::TX_RADIO_CMD;
      hdr.target_subdevice = 0;
      hdr.command = RADIO_CMD::STOP_NOW;
      hdr.flags = 0;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = buffers_sent % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes_sent != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      // wait a bit for things to finish up
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      // get the error status after TX completed
      hdr.packet_type = PACKET_TYPES::DEVICE_CMD;
      hdr.target_subdevice = 0;
      hdr.command = DEVICE_CMD::GET_STATUS;
      hdr.flags = 0;
      hdr.packet_size = sizeof(hdr);
      hdr.sequence_counter = buffers_sent % UINT16_MAX;
      bytes_sent = socket.send_to(boost::asio::buffer(&hdr, hdr.packet_size), destination);
      if(bytes_sent != hdr.packet_size)
        send_errors++;
      else
        cmds_sent++;
      LOGGER(info) << "send finished: " << buffers_sent << " buffers, " << cmds_sent << " commands, " << send_errors << " send errors";
    }
};

void writer(const std::string &outfilename, const size_t max_packets) {
  LOGGER(info) << "writer thread started";

  std::array<data_packet, rx_write_buffer_size> buffer;
  std::ofstream outfile(outfilename.c_str(), std::ofstream::binary);
  size_t packets = 0, seq_errors = 0, write_errors = 0, bytes_written = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
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
    if(tx_stop_flag or rx_stop_flag) {
      break;
    }
  }

  outfile.close();

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec";
  LOGGER(info) << seq_errors << " sequence errors, " << write_errors << " write errors";
  LOGGER(info) << (bytes_written / dt.count()) << " bytes/s";

  // set stop flag so pushing stops
  set_tx_stop_flag();
  set_rx_stop_flag();
  std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_clear_sleep_us));
  // empty the queue
  while(rx_data_queue.read_available())
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "writer thread finished";
}

void packet_counter(const size_t max_packets, const size_t packet_size) {
  LOGGER(info) << "packet counter thread started";

  //if(set_current_thread_priority_realtime(10))
  //  LOGGER(error) << "unable to set sequence checker thread realtime priority";

  //if(set_current_thread_affinity(1))
  //  LOGGER(error) << "unable to set sequence checker thread CPU affinity";

  const unsigned buffer_max_packets = 256;
  static std::array<data_packet, buffer_max_packets> buffer;
  size_t packets = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets) {
    if(packets == 0)
      t0 = std::chrono::steady_clock::now();
    while(rx_data_queue.read_available()) {
      size_t n_popped = rx_data_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        packets++;
      }
    }
    if(rx_write_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(rx_write_thread_sleep_us));

    if(tx_stop_flag or rx_stop_flag) {
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << packets << " packets recieved in " << dt.count() << " sec";
  LOGGER(info) << (packets * packet_size / dt.count()) << " bytes/s, " << (8 * packets * packet_size / dt.count()) << " bits/s";

  // set stop flag so pushing stops
  set_tx_stop_flag();
  set_rx_stop_flag();
  std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_clear_sleep_us));
  // empty the queue
  while(rx_data_queue.read_available())
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(info) << "packet counter thread finished";
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

  unsigned max_wait = 1000000;
  if(tx_generator_thread_sleep_us > 0)
    max_wait = 1000000 / tx_generator_thread_sleep_us;

  for(size_t i = 1; i <= max_packets; i++) {
    unsigned idx = i % buf.size();
    unsigned nwait = 0;
    // wait for room in the tx queue
    while(!tx_data_queue.write_available() and nwait < max_wait) {
      nwait++;
      if(tx_generator_thread_sleep_us > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(tx_generator_thread_sleep_us));
    }

    tx_data_queue.push(buf[idx]);
    if(tx_generator_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(tx_generator_thread_sleep_us));
    if(tx_stop_flag or rx_stop_flag) {
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << max_packets << " buffers (" << (max_packets * packet_size) << " bytes) generated in " << dt.count() << " seconds";

  std::this_thread::sleep_for(std::chrono::seconds(5));
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