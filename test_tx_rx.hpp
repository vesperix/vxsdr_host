#include <vector>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "logging.hpp"
#include "cycle_counter.h"
#include "thread_utils.h"
#include "utility.hpp"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

#ifndef TEST_TX_RX_HPP
#define TEST_TX_RX_HPP

//FIXME!!! Disgusting temporary kludge...

double global_kludge_rx_freq = 6.0e9;
double global_kludge_rx_gain =  30.0;
double global_kludge_rx_rate = 160e6;

double global_kludge_tx_freq = 6.0e9;
double global_kludge_tx_gain =  30.0;
double global_kludge_tx_rate = 160e6;

// parameters of the internal ring buffers

static constexpr size_t rx_data_queue_length           =     4096;    // slots in rx data queue
static constexpr size_t tx_data_queue_length           =     4096;    // slots in tx data queue
static constexpr size_t msg_queue_length               =     1024;    // slots in command and response msg queues

// parameters of the writer thread
static constexpr size_t rx_write_buffer_size           =       16;     // rx_buffers
static constexpr int    rx_write_thread_sleep_us       =      100;
static constexpr int    rx_data_queue_clear_sleep_us   =    10000;

// parameters for the send thread
static constexpr int    send_thread_sleep_us           =       10;

// parameters for the receive thread
static constexpr int    receive_thread_sleep_us        =        1;

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

inline void set_rx_stop_flag() {
  rx_stop_flag = true;
}

inline void set_tx_stop_flag() {
  tx_stop_flag = true;
}

inline void set_complete_flag() {
  complete_flag = true;
}

static boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
static boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;

static boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<msg_queue_length> > command_queue;
static boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<msg_queue_length> > response_queue;


class udp_receiver {
  public:
    udp_receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
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
    udp_receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
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
    uint64_t packets_received = 0, seq_errors = 0;
    std::array<uint64_t, UINT8_MAX + 1> packet_types_received = { 0 };
    uint16_t last_seq = 0;
    // FIXME: diagnostic info gathering: remove when unneeded
    std::vector<std::chrono::time_point<std::chrono::system_clock> > seq_error_times;
    std::vector<uint8_t> seq_error_packet_type;
    std::vector<uint16_t> seq_error_counter;
    std::vector<uint16_t> seq_error_expected;

    void start_receive() {
      LOGGER(debug) << "start_receive started";
      //boost::asio::socket_base::bytes_readable check_for_bytes(true);
      // FIXME: need to use async read, handler, and deadline timer to implement timeout
      while(not rx_stop_flag) {
        socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
        packets_received++;
        packet_types_received[recv_buffer.hdr.packet_type]++;
        if(packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1)) {
          seq_errors++;
          seq_error_times.push_back(std::chrono::system_clock::now());
          seq_error_packet_type.push_back(recv_buffer.hdr.packet_type);
          seq_error_counter.push_back(recv_buffer.hdr.sequence_counter);
          seq_error_expected.push_back((uint16_t)(last_seq + 1));
        }
        last_seq = recv_buffer.hdr.sequence_counter;
        switch(recv_buffer.hdr.packet_type) {
          // receive buffers go into the rx data queue
          case PACKET_TYPE_RX_SIGNAL_DATA:
            rx_data_queue.push(recv_buffer);
            break;
          // tx sginal data acks special cased since they contain throttling information
          case PACKET_TYPE_TX_SIGNAL_DATA_ACK:
            {
              auto *r = reinterpret_cast<tx_signal_data_ack_packet *>(&recv_buffer);
              tx_buffer_used = r->tx_buffer_used_bytes;
              tx_buffer_fill_percent = std::min(100., (100. * tx_buffer_used) / tx_buffer_size);
              if(forward_acks) {
                auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                response_queue.push(*p);
              }
            }
            break;
          // the buffer info commands are special cased since they can also be used for TX throttling
          case PACKET_TYPE_DEVICE_CMD_RSP:
            switch(recv_buffer.hdr.command) {
              case DEVICE_CMD_GET_BUFFER_USE:
                {
                  auto *r = reinterpret_cast<get_buffer_use_rsp_packet *>(&recv_buffer);
                  tx_buffer_used = r->tx_buffer_used_bytes;
                  rx_buffer_used = r->rx_buffer_used_bytes;
                  tx_buffer_fill_percent = std::min(100., (100. * tx_buffer_used) / tx_buffer_size);
                  rx_buffer_fill_percent = std::min(100., (100. * rx_buffer_used) / rx_buffer_size);
                  //auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                  //response_queue.push(*p);
                }
                break;
              case DEVICE_CMD_GET_BUFFER_INFO:
                {
                  auto *r = reinterpret_cast<get_buffer_info_rsp_packet *>(&recv_buffer);
                  tx_buffer_size = r->tx_buffer_size_bytes;
                  rx_buffer_size = r->rx_buffer_size_bytes;
                  auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                  response_queue.push(*p);
                }
                break;
              default:
                {
                  auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                  response_queue.push(*p);
                }
                break;
            }
            break;
          case PACKET_TYPE_DEVICE_CMD_ACK:
          case PACKET_TYPE_RX_RADIO_CMD_ACK:
          case PACKET_TYPE_TX_RADIO_CMD_ACK:
            if(forward_acks) {
              auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
              response_queue.push(*p);
            }
            break;
          // DEVICE_CMD_RSP handled above
          case PACKET_TYPE_DEVICE_CMD_ERR:
          case PACKET_TYPE_RX_RADIO_CMD_RSP:
          case PACKET_TYPE_RX_RADIO_CMD_ERR:
          case PACKET_TYPE_TX_RADIO_CMD_RSP:
          case PACKET_TYPE_TX_RADIO_CMD_ERR:
            {
              auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
              response_queue.push(*p);
            }
            break;
          // packet types that should never show up at the host are dropped
          case PACKET_TYPE_TX_SIGNAL_DATA:
          case PACKET_TYPE_DEVICE_CMD:
          case PACKET_TYPE_RX_RADIO_CMD:
          case PACKET_TYPE_TX_RADIO_CMD:
          default: // unrecognized packet types
            break;
        }
      }
      //if(receive_thread_sleep_us > 0)
      //  std::this_thread::sleep_for(std::chrono::microseconds(receive_thread_sleep_us));

      LOGGER(info) << "start_receive exiting; " << packets_received << " packets received, " << seq_errors << " sequence errors";
      for(unsigned i =0; i < seq_error_times.size(); i++) {
        LOGGER(info) << boost::format(" %30s %20s %8d %8d") % format_time(seq_error_times[i]) % packet_type_to_name(seq_error_packet_type[i])
                                                                 % seq_error_counter[i] % seq_error_expected[i];
      }
      for(unsigned i = 0; i < packet_types_received.size(); i++)
        if(packet_types_received[i] > 0)
          LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_received[i] % packet_type_to_name(i) % i);
    }
};

class udp_sender {
  public:
    udp_sender(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint,
      const bool tx_enable, const bool rx_enable) : socket(io_context, local_endpoint) {
      destination = destination_endpoint;
      socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));
      enable_rx = rx_enable;
      enable_tx = tx_enable;
      boost::asio::socket_base::send_buffer_size option;
      socket.get_option(option);
      int network_buffer_size = option.value();
      LOGGER(info) << "network send buffer size set to " << network_buffer_size;

      if(not tx_stop_flag)
        start_send();
    }
  private:
    bool enable_rx;
    bool enable_tx;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint destination;
    static constexpr size_t send_buffer_size = 16;
    std::array<largest_data_packet, send_buffer_size> send_buffer;
    std::array<uint64_t, UINT8_MAX + 1> packet_types_processed = { 0 };
    uint64_t send_errors = 0, packets_sent = 0, bytes_sent = 0;

    void send_device_hello(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_HELLO, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_device_reset(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_RESET, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_device_open_session(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_OPEN_SESSION, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_device_close_session(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_CLOSE_SESSION, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_device_get_buffer_info(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_INFO, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_device_get_buffer_use(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_USE, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_device_clear_status(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_CLEAR_STATUS, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_device_get_status(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_STATUS, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_tx_start_now_until_stop(const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_rx_start_now_until_stop(const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet_time_samples p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
      p.n_samples = n;
      send_packet(p);
    }

    void send_rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet_time_samples p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
      p.n_samples = n;
      send_packet(p);
    }

    void send_tx_stop_now(const uint8_t subdev, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

    void send_rx_stop_now(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0 };
      send_packet(p);
    }

   void set_tx_freq(const double freq, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_freq_packet p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
      p.freq = freq;
      p.rtol = 1e-9;
      send_packet(p);
    }

    void set_rx_freq(const double freq, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_freq_packet p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
      p.freq = freq;
      p.rtol = 1e-9;
      send_packet(p);
    }

    void set_tx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_gain_packet p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
      p.gain = gain_db;
      send_packet(p);
    }

    void set_rx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_gain_packet p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
      p.gain = gain_db;
      send_packet(p);
    }

    void set_tx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_sample_rate_packet p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
      //FIXME: rate setting needs to be done in MCU from rate_samples_sec, not here
      p.rate = std::min(128, std::max(1, (int)std::lround(160.0e6 / rate_samples_sec)));
      send_packet(p);
    }

    void set_rx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_sample_rate_packet p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
      //FIXME: rate setting needs to be done in MCU from rate_samples_sec, not here
      p.rate = std::min(128, std::max(1, (int)std::lround(160.0e6 / rate_samples_sec)));
      send_packet(p);
    }

    void set_tx_antenna(const std::string ant, const uint8_t subdev = 0, const uint8_t flags = 0) {
      //FIXME: implement
    }

    void set_rx_antenna(const std::string ant, const uint8_t subdev = 0, const uint8_t flags = 0) {
      //FIXME: implement
    }

    void set_tx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_enable_packet p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
      if(enabled) {
        p.enabled = 1;
      }
      else {
        p.enabled = 0;
      }
      send_packet(p);
    }

    void set_rx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_enable_packet p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
      if(enabled) {
        p.enabled = 1;
      }
      else {
        p.enabled = 0;
      }
      send_packet(p);
    }

    void send_packet(packet &packet) {
      packet_types_processed[packet.hdr.packet_type]++;
      packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (UINT16_MAX + 1));
      size_t bytes = socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination);
      if(bytes != packet.hdr.packet_size)
        send_errors++;
      bytes_sent += bytes;
    }

    void start_send() {
      LOGGER(debug) << "start_send started";
      //FIXME: handle halt correctly
      constexpr float throttle_halt_percent  = 90;
      constexpr float throttle_on_percent    = 75;
      constexpr float throttle_off_percent   = 50;
      bool throttling = false;
      constexpr unsigned throttle_amount_us =     10; // send time for 8200 bytes over 10 GbE is about 6.7 us

      const uint8_t device_subdev = 0, tx_subdev = 0, rx_subdev = 0;

      send_tx_stop_now(tx_subdev);
      send_rx_stop_now(rx_subdev);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // get device buffer sizes
      send_device_get_buffer_info(device_subdev);
      // wait for the response come back and be processed
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      // clear the status registers
      send_device_clear_status(device_subdev);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      send_device_get_status(device_subdev);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      if(this->enable_rx) {
        set_rx_enable(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_rx_freq(global_kludge_rx_freq);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_rx_gain(global_kludge_rx_gain);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_rx_rate(global_kludge_rx_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if(this->enable_tx) {
        set_tx_enable(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_tx_freq(global_kludge_tx_freq);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_tx_gain(global_kludge_tx_gain);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        set_tx_rate(global_kludge_tx_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      // send a START_NOW_UNTIL_STOP
      if(this->enable_rx)
        send_rx_start_now_until_stop(rx_subdev);
      if(this->enable_tx)
        send_tx_start_now_until_stop(tx_subdev);

      //send_tx_start_now_for_n_samples(1000000 * 2048ull, tx_subdev);
      //send_rx_start_now_for_n_samples(180000 * 2048ull, rx_subdev);

      uint64_t last_check = 0;
      constexpr uint64_t buffer_check_interval = 100; // current buffer size is about 1000 data packets

      while(not tx_stop_flag) {
        if(tx_data_queue.read_available() > 0 and tx_buffer_fill_percent < throttle_halt_percent) {
          size_t n_popped = tx_data_queue.pop(&send_buffer.front(), send_buffer.size());
          for(size_t i = 0; i < n_popped; i++) {

            if(packets_sent - last_check > buffer_check_interval) {
              last_check = packets_sent;
              // request ack to get buffer info
              send_buffer[i].hdr.flags |= FLAGS_REQUEST_ACK;
              send_packet(send_buffer[i]);
              //send_buffer[i].hdr.flags = 0;
            }
            else {
              send_packet(send_buffer[i]);
            }

            // update throttling
            if(tx_buffer_fill_percent > throttle_on_percent)
              throttling = true;
            else if(tx_buffer_fill_percent <= throttle_off_percent)
              throttling = false;
            if(throttling)
              std::this_thread::sleep_for(std::chrono::microseconds(throttle_amount_us));
          }
        }
        else {
          if(send_thread_sleep_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(send_thread_sleep_us));
        }
      }
      // get the status at the end
      send_device_get_status(device_subdev);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      // send a stop
      if(this->enable_tx) {
        send_tx_stop_now(tx_subdev, FLAGS_REQUEST_ACK);
        set_tx_enable(false);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if(this->enable_rx) {
        send_rx_stop_now(rx_subdev, FLAGS_REQUEST_ACK);
        set_rx_enable(false);
      }
      LOGGER(info) << "start_send exiting; " << packets_sent << " packets sent, " << send_errors << " send errors";
      for(unsigned i = 0; i < packet_types_processed.size(); i++)
        if(packet_types_processed[i] > 0)
          LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_processed[i] % packet_type_to_name(i) % i);

    }
};

inline void writer(const std::string &outfilename, const uint64_t max_packets) {
  LOGGER(debug) << "writer thread started";

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
        // identify the data packet type and write the data
        size_t data_bytes;
        if((buffer[i].hdr.flags & FLAGS_TIME_PRESENT) != 0 and (buffer[i].hdr.flags & FLAGS_STREAM_ID_PRESENT) != 0) {
          auto *p = reinterpret_cast<data_packet_time_stream *>(&buffer[i]);
          data_bytes = (p->hdr.packet_size - (sizeof(packet_header) + sizeof(time_spec_t) + sizeof(stream_spec_t)));
          outfile.write(reinterpret_cast<const char*>(&p->data[0]), data_bytes);
        }
        else if((buffer[i].hdr.flags & FLAGS_TIME_PRESENT) != 0) {
          auto *p = reinterpret_cast<data_packet_time *>(&buffer[i]);
          data_bytes = (p->hdr.packet_size - (sizeof(packet_header) + sizeof(time_spec_t)));
          outfile.write(reinterpret_cast<const char*>(&p->data[0]), data_bytes);
        }
        else if((buffer[i].hdr.flags & FLAGS_STREAM_ID_PRESENT) != 0) {
          auto *p = reinterpret_cast<data_packet_stream *>(&buffer[i]);
          data_bytes = (p->hdr.packet_size - (sizeof(packet_header) + sizeof(stream_spec_t)));
          outfile.write(reinterpret_cast<const char*>(&p->data[0]), data_bytes);
        }
        else {
          auto *p = reinterpret_cast<data_packet *>(&buffer[i]);
          data_bytes = (p->hdr.packet_size - sizeof(packet_header));
          outfile.write(reinterpret_cast<const char*>(&p->data[0]), data_bytes);
        }
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
  while(rx_data_queue.read_available() > 0)
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(debug) << "writer thread finished";
}

inline void data_packet_counter(const uint64_t max_packets) {
  LOGGER(debug) << "data_packet counter thread started";

  const unsigned buffer_max_packets = 256;
  static std::array<largest_data_packet, buffer_max_packets> buffer;
  uint64_t packets = 0, bytes = 0;

  auto t0 = std::chrono::steady_clock::now();

  while(packets < max_packets) {
    while(rx_data_queue.read_available() > 0) {
      size_t n_popped = rx_data_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        packets++;
        bytes += buffer[i].hdr.packet_size;
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
  LOGGER(info) << (bytes / dt.count()) << " bytes/s, " << (8 * bytes / dt.count()) << " bits/s";

  std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_clear_sleep_us));
  // empty the queue
  while(rx_data_queue.read_available() > 0)
    rx_data_queue.pop(&buffer.front(), rx_write_buffer_size);

  LOGGER(debug) << "data packet counter thread finished";
}

inline void response_packet_printer() {
  LOGGER(debug) << "response packet printer thread started";

  const unsigned buffer_max_packets = 256;
  static std::array<largest_cmd_or_rsp_packet, buffer_max_packets> buffer;
  uint64_t packets = 0;

  while(not complete_flag) {
    if(response_queue.read_available() > 0) {
      size_t n_popped = response_queue.pop(&buffer.front(), buffer_max_packets);
      for(size_t i = 0; i < n_popped; i++) {
        packets++;
        log_packet_info(buffer[i]);
      }
    }
    if(response_thread_sleep_us > 0)
      std::this_thread::sleep_for(std::chrono::microseconds(response_thread_sleep_us));
  }

  LOGGER(debug) << "response packet printer thread finished";
}

inline void receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
{
  LOGGER(debug) << "receiver thread started";

  if(set_current_thread_priority_realtime(20) != 0)
    LOGGER(error) << "unable to set receiver thread realtime priority";

  if(set_current_thread_affinity(0) != 0)
    LOGGER(error) << "unable to set receiver thread CPU affinity";

  udp_receiver server(io_context, local_endpoint);
  io_context.run();
  LOGGER(debug) << "receiver thread finished";
}

inline void multicast_receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint, const boost::asio::ip::address_v4 &multicast_ip)
{
  LOGGER(debug) << "multicast receiver thread started";

  if(set_current_thread_priority_realtime(20) != 0)
    LOGGER(error) << "unable to set realtime thread priority";

  if(set_current_thread_affinity(0) != 0)
    LOGGER(error) << "unable to set thread CPU affinity";

  udp_receiver server(io_context, local_endpoint, multicast_ip);
  io_context.run();
  LOGGER(debug) << "multicast receiver thread finished";
}

inline void generator(const uint64_t max_packets, const size_t requested_packet_size, const double a, const int ncycles) {
  std::array<largest_data_packet, 625> buf;
  static constexpr size_t data_buffer_max_bytes = sizeof(largest_data_packet);
  LOGGER(debug) << "generator thread started";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > data_buffer_max_bytes) {
    LOGGER(error) << "requested packet size is greater than maximum (" << data_buffer_max_bytes <<"); setting to maximum";
    packet_size = data_buffer_max_bytes;
  }

  const double w = (-2 * M_PI * ncycles) / (buf.size() * MAX_DATA_LENGTH_SAMPLES);

  for(unsigned i = 0; i < buf.size(); i++) {
    buf[i].hdr.packet_type = PACKET_TYPE_TX_SIGNAL_DATA;
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
    while(tx_data_queue.write_available() == 0) {
      if(tx_generator_thread_sleep_us > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(tx_generator_thread_sleep_us));
    }
    while(tx_data_queue.write_available() > 0 and nsent <= max_packets) {
      tx_data_queue.push(buf[nsent++ % buf.size()]);
    }
    if(tx_stop_flag or nsent > max_packets) {
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << (nsent - 1) << " buffers (" << ((nsent - 1) * packet_size) << " bytes) generated in " << dt.count() << " seconds";
  LOGGER(debug) << "generator thread finished";
}

inline void intermod_generator(const uint64_t max_packets, const size_t requested_packet_size, const double a, const int ncycles1, const int ncycles2) {
  std::array<largest_data_packet, 625> buf;
  static constexpr size_t data_buffer_max_bytes = sizeof(largest_data_packet);
  LOGGER(debug) << "intermod_generator thread started";

  size_t packet_size = requested_packet_size;
  if(requested_packet_size > data_buffer_max_bytes) {
    LOGGER(error) << "requested packet size is greater than maximum (" << data_buffer_max_bytes <<"); setting to maximum";
    packet_size = data_buffer_max_bytes;
  }

  const double w1 = (2 * M_PI * ncycles1) / (buf.size() * MAX_DATA_LENGTH_SAMPLES);
  const double w2 = (2 * M_PI * ncycles2) / (buf.size() * MAX_DATA_LENGTH_SAMPLES);

  for(unsigned i = 0; i < buf.size(); i++) {
    buf[i].hdr.packet_type = PACKET_TYPE_TX_SIGNAL_DATA;
    buf[i].hdr.command = 0;
    buf[i].hdr.target_subdevice = 0;
    buf[i].hdr.flags = 0;
    buf[i].hdr.packet_size = packet_size;
    size_t n_samples = (packet_size - sizeof(packet_header)) / sizeof(std::complex<int16_t>);
    for(unsigned j = 0; j < n_samples; j++) {
      unsigned t = MAX_DATA_LENGTH_SAMPLES * i + j;
      buf[i].data[j].real( std::round(a * (cos(w1 * t) + cos(w2 * t))));
      buf[i].data[j].imag(-std::round(a * (sin(w1 * t) + sin(w2 * t))));
    }
  }

  auto t0 = std::chrono::steady_clock::now();

  uint64_t nsent = 0;

  while(true) {
    // wait for room in the tx queue
    while(tx_data_queue.write_available() == 0) {
      if(tx_generator_thread_sleep_us > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(tx_generator_thread_sleep_us));
    }
    while(tx_data_queue.write_available() > 0 and nsent <= max_packets) {
      tx_data_queue.push(buf[nsent++ % buf.size()]);
    }
    if(tx_stop_flag or nsent > max_packets) {
      break;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  std::chrono::duration<double> dt = t1 - t0;
  LOGGER(info) << (nsent - 1) << " buffers (" << ((nsent - 1) * packet_size) << " bytes) generated in " << dt.count() << " seconds";
  LOGGER(debug) << "intermod_generator thread finished";
}

inline void sender(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
      const boost::asio::ip::udp::endpoint &destination_endpoint, const bool tx_enable, const bool rx_enable)
{
  LOGGER(debug) << "sender thread started";

  if(set_current_thread_priority_realtime(20) != 0)
    LOGGER(error) << "unable to set sender thread realtime priority";

  if(set_current_thread_affinity(2) != 0)
    LOGGER(error) << "unable to set sender thread CPU affinity";

  udp_sender server(io_context, local_endpoint, destination_endpoint, tx_enable, rx_enable);
  io_context.run();

  LOGGER(debug) << "sender thread finished";
}

#endif // TEST_TX_RX_HPP
