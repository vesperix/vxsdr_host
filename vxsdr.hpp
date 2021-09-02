#include <complex>
#include <vector>
#include <cstring>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "logging.hpp"
#include "thread_utils.h"
#include "utility.hpp"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

#ifndef VXSDR_HPP
#define VXSDR_HPP

class vxsdr {

  private:
    // thread realtime priority settings
    static constexpr bool thread_set_priority = true;
    static constexpr int send_thread_priority = 20;
    static constexpr int receive_thread_priority = 20;

    // thread cpu affinity settings
    static constexpr bool thread_set_affinity = true;
    static constexpr int send_thread_cpu = 0;
    static constexpr int receive_thread_cpu = 2;

    // parameters of the internal spsc queues (ring buffers)
    static constexpr size_t rx_data_queue_length           =     4096;    // slots in rx data queue
    static constexpr size_t tx_data_queue_length           =     4096;    // slots in tx data queue
    static constexpr size_t cmd_queue_length               =       64;    // slots in command msg queue
    static constexpr size_t rsp_queue_length               =      256;    // slots in response msg queue

    // timeouts and waits between tries for queue push/pop

    static constexpr unsigned tx_data_queue_timeout_us     =   100000;
    static constexpr unsigned tx_data_queue_wait_us        =       10;

    static constexpr unsigned rx_data_queue_timeout_us     =   100000;
    static constexpr unsigned rx_data_queue_wait_us        =       10;

    static constexpr unsigned command_queue_timeout_us     =   100000;
    static constexpr unsigned command_queue_wait_us        =       10;

    static constexpr unsigned response_queue_timeout_us    =   100000;
    static constexpr unsigned response_queue_wait_us       =       10;

    // parameters used to set the OS's network buffer
    static constexpr unsigned network_send_buffer_bytes    =   4194304;   // ~6 ms at 160 MSPS
    static constexpr unsigned network_receive_buffer_bytes =   4194304;   // ~6 ms at 160 MSPS

    // parameters used to monitor status of the radio's internal buffers (on the other end of the network connection)
    std::atomic<unsigned> tx_buffer_size, tx_buffer_used;
    std::atomic<unsigned> rx_buffer_size, rx_buffer_used;
    std::atomic<unsigned> tx_buffer_fill_percent, rx_buffer_fill_percent;

    // data queues
    boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
    boost::lockfree::spsc_queue<largest_data_packet, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;
    // command and response queues
    boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<cmd_queue_length> > command_queue;
    boost::lockfree::spsc_queue<largest_cmd_or_rsp_packet, boost::lockfree::capacity<rsp_queue_length> > response_queue;

    // Boost context
    boost::asio::io_context io_context;

    // threads used for sending and receiving
    std::thread sender_thread;
    std::thread receiver_thread;

    // flags used for orderly shutdown
    std::atomic<bool> sender_thread_stop_flag, receiver_thread_stop_flag;

  public:
    // constructor
    vxsdr(const boost::asio::ip::udp::endpoint &local_receive_endpoint,
          const boost::asio::ip::udp::endpoint &local_send_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint) {
      LOGGER(debug) << "vxsdr constructor entered";
      tx_data_queue.reset();
      rx_data_queue.reset();
      command_queue.reset();
      response_queue.reset();
      sender_thread_stop_flag   = false;
      receiver_thread_stop_flag = false;
      tx_buffer_size = 0;
      tx_buffer_used = 0;
      rx_buffer_size = 0;
      rx_buffer_used = 0;
      tx_buffer_fill_percent = 0.0;
      rx_buffer_fill_percent = 0.0;
      //sender_thread = std::thread(&vxsdr::sender, this, std::ref(io_context), std::cref(local_send_endpoint), std::cref(destination_endpoint));
      sender_thread = std::thread([=] { sender(io_context, local_send_endpoint, destination_endpoint); });
      //receiver_thread = std::thread(&vxsdr::receiver, this, std::ref(io_context), std::cref(local_receive_endpoint));
      receiver_thread = std::thread([=] { receiver(io_context, local_receive_endpoint); });
      set_tx_enable(true);
      set_rx_enable(true);
      LOGGER(debug) << "vxsdr constructor complete";
    };
    // destructor
    ~vxsdr() {
      LOGGER(debug) << "vxsdr destructor entered";
      set_tx_enable(false);
      set_rx_enable(false);
      LOGGER(debug) << "joining receiver_thread";
      receiver_thread_stop_flag = true;
      if(receiver_thread.joinable()) {
        receiver_thread.join();
      }
      LOGGER(debug) << "joining sender thread";
      sender_thread_stop_flag = true;
      if(sender_thread.joinable()) {
        sender_thread.join();
      }
      LOGGER(debug) << "vxsdr destructor complete";
    };
    // copy constructor deleted
    vxsdr(const vxsdr &) = delete;
    // copy assignment deleted
    vxsdr& operator=(const vxsdr &) = delete;
    // move constructor deleted
    vxsdr(vxsdr &&) = delete;
    // move assignment deleted
    vxsdr& operator=(vxsdr &&) = delete;

    size_t get_rx_data(std::vector<std::complex<int16_t> > &data, size_t n_desired = 0) {
      LOGGER(debug) << "get_rx_data() entered";
      size_t n_popped = 0;
      largest_data_packet q;

      data.clear();
      if(n_desired > 0) {
        data.reserve(n_desired);
      }
      else {
        n_desired = data.size();
      }

      size_t n_packets = std::ceil(n_desired / (double)MAX_DATA_LENGTH_SAMPLES);
      LOGGER(debug) << "n_packets = " << n_packets << " n_popped = " << n_popped;

      while(n_popped < n_packets) {
        unsigned wait_time_us = 0;
        // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
        while(rx_data_queue.read_available() == 0 and wait_time_us < rx_data_queue_timeout_us) {
          if constexpr(rx_data_queue_wait_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
            wait_time_us += rx_data_queue_wait_us;
          }
        }

        if(rx_data_queue.pop(q)) {
          bool has_time = (bool)(q.hdr.flags | FLAGS::TIME_PRESENT);
          bool has_stream_id = (bool)(q.hdr.flags | FLAGS::STREAM_ID_PRESENT);
          if(not has_time and not has_stream_id) {
            auto *p = reinterpret_cast<data_packet *>(&q);
            size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                  (p->hdr.packet_size - sizeof(packet_header)) / sizeof(std::complex<int16_t>));
            std::copy_n(std::begin(p->data), data_size, std::back_inserter(data));
          }
          else if(has_time and not has_stream_id) {
            auto *p = reinterpret_cast<data_packet_time *>(&q);
            size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                  (p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t)) / sizeof(std::complex<int16_t>));
            std::copy_n(std::begin(p->data), data_size, std::back_inserter(data));
          }
          else if(has_stream_id and not has_time) {
            auto *p = reinterpret_cast<data_packet_stream *>(&q);
            size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                  (p->hdr.packet_size - sizeof(packet_header) - sizeof(stream_spec_t)) / sizeof(std::complex<int16_t>));
            std::copy_n(std::begin(p->data), data_size, std::back_inserter(data));
          }
          else {
            auto *p = reinterpret_cast<data_packet_time_stream *>(&q);
            size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                  (p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t) - sizeof(stream_spec_t)) / sizeof(std::complex<int16_t>));
            std::copy_n(std::begin(p->data), data_size, std::back_inserter(data));
          }
          n_popped++;
        }
        else {
          LOGGER(error) << "timeout popping from rx data queue";
          break;
        }
      }
      LOGGER(debug) << "get_rx_data() complete";
      return data.size();
    }

    size_t put_tx_data(std::vector<std::complex<int16_t> > data, const uint8_t subdev = 0) {
      // puts plain data_packets (no time, no stream)
      size_t n_put = 0;
      for(size_t i = 0; i < data.size(); i += MAX_DATA_LENGTH_SAMPLES) {
        largest_data_packet q;
        auto *p = reinterpret_cast<data_packet *>(&q);
        unsigned n_samples = std::min(MAX_DATA_LENGTH_SAMPLES, data.size() - i);
        unsigned n_data_bytes = n_samples * sizeof(std::complex<int16_t>);
        uint16_t packet_size = sizeof(packet_header) + n_data_bytes;
        p->hdr = { PACKET_TYPES::TX_SIGNAL_DATA, 0, subdev, 0, packet_size, 0 };
        for(unsigned j = 0; j < n_samples; j++) {
          p->data[j] = data[i + j];
        }

        unsigned wait_time_us = 0;
        // setting tx_data_queue_wait_us = 0 results in a busy wait with no timeout
        while(tx_data_queue.write_available() == 0 and wait_time_us < tx_data_queue_timeout_us) {
          if constexpr(tx_data_queue_wait_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(tx_data_queue_wait_us));
            wait_time_us += tx_data_queue_wait_us;
          }
        }

        if(tx_data_queue.push(q)) {
          n_put += n_samples;
        }
        else {
          LOGGER(error) << "timeout pushing to tx data queue";
          break;
        }
      }
      return n_put;
    }

    void cmd_queue_push(packet &p) {
      largest_cmd_or_rsp_packet q;
      std::memcpy(&q, &p, std::min((size_t)p.hdr.packet_size, sizeof(q)));
      unsigned wait_time_us = 0;
      // setting command_queue_wait_us = 0 results in a busy wait with no timeout
      while(command_queue.write_available() == 0 and wait_time_us < command_queue_timeout_us) {
        if constexpr(command_queue_wait_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(command_queue_wait_us));
          wait_time_us += command_queue_wait_us;
        }
      }

      if(not command_queue.push(q)) {
        LOGGER(error) << "timeout pushing to command queue";
      }
    }

    void device_hello(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::HELLO, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void device_reset(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::RESET, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void device_open_session(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::OPEN_SESSION, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void device_close_session(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLOSE_SESSION, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void device_clear_status(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLEAR_STATUS, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void device_get_status(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_STATUS, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void tx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void rx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
      stream_ctrl_packet_time_samples p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
      p.n_samples = n;
      cmd_queue_push(p);
    }

    void rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
      stream_ctrl_packet_time_samples p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
      p.n_samples = n;
      cmd_queue_push(p);
    }

    void tx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
      stream_ctrl_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void rx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::STOP_NOW, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

   void set_tx_freq(const double freq, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_freq_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
      p.freq = freq;
      p.rtol = 1e-9;
      cmd_queue_push(p);
    }

    void set_rx_freq(const double freq, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_freq_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
      p.freq = freq;
      p.rtol = 1e-9;
      cmd_queue_push(p);
    }

    void set_tx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_gain_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
      p.gain = gain_db;
      cmd_queue_push(p);
    }

    void set_rx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_gain_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
      p.gain = gain_db;
      cmd_queue_push(p);
    }

    void set_tx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_sample_rate_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
      //FIXME: rate setting needs to be done in MCU from rate_samples_sec, not here
      p.rate = std::min(128, std::max(1, (int)std::lround(160.0e6 / rate_samples_sec)));
      cmd_queue_push(p);
    }

    void set_rx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_sample_rate_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
      //FIXME: rate setting needs to be done in MCU from rate_samples_sec, not here
      p.rate = std::min(128, std::max(1, (int)std::lround(160.0e6 / rate_samples_sec)));
      cmd_queue_push(p);
    }

    void set_tx_antenna(const std::string &ant, const uint8_t subdev = 0, const uint8_t flags = 0) {
      //FIXME: implement
    }

    void set_rx_antenna(const std::string &ant, const uint8_t subdev = 0, const uint8_t flags = 0) {
      //FIXME: implement
    }

    void set_tx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_enable_packet p;
      p.hdr = { PACKET_TYPES::TX_RADIO_CMD, RADIO_CMD::SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
      if(enabled) {
        p.enabled = 1;
      }
      else {
        p.enabled = 0;
      }
      cmd_queue_push(p);
    }

    void set_rx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0) {
      radio_rf_enable_packet p;
      p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
      if(enabled) {
        p.enabled = 1;
      }
      else {
        p.enabled = 0;
      }
      cmd_queue_push(p);
    }

  private:

    void device_get_buffer_info(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_INFO, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void device_get_buffer_use(const uint8_t subdev, const uint8_t flags = 0) {
      cmd_packet p;
      p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::GET_BUFFER_USE, subdev, flags, sizeof(p), 0 };
      cmd_queue_push(p);
    }

    void receiver(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
    {
      LOGGER(debug) << "receiver thread started";

      if(thread_set_priority) {
        if(set_current_thread_priority_realtime(receive_thread_priority) != 0) {
          LOGGER(error) << "unable to set receiver thread realtime priority";
        }
      }

      if(thread_set_affinity) {
        if(set_current_thread_affinity(receive_thread_cpu) != 0) {
          LOGGER(error) << "unable to set receiver thread CPU affinity";
        }
      }

      udp_receiver server(this, io_context, local_endpoint);
      io_context.run();
      LOGGER(debug) << "receiver thread finished";
    }

    void sender(boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint)
    {
      LOGGER(debug) << "sender thread started";

      if(thread_set_priority) {
        if(set_current_thread_priority_realtime(send_thread_priority) != 0) {
          LOGGER(error) << "unable to set sender thread realtime priority";
        }
      }

      if(thread_set_affinity) {
        if(set_current_thread_affinity(send_thread_cpu) != 0) {
          LOGGER(error) << "unable to set sender thread CPU affinity";
        }
      }

      udp_sender server(this, io_context, local_endpoint, destination_endpoint);
      io_context.run();
      LOGGER(debug) << "sender thread finished";
    }

    class udp_receiver {

      public:
        // constructor
        udp_receiver(vxsdr *p, boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint)
          : socket(io_context, local_endpoint) {
          socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
          socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));
          sdrptr = p;
          boost::asio::socket_base::receive_buffer_size option;
          socket.get_option(option);
          int network_buffer_size = option.value();
          LOGGER(info) << "network receive buffer size set to " << network_buffer_size;

          if(not sdrptr->receiver_thread_stop_flag) {
            start_receive();
          }
        }
        // destructor
        ~udp_receiver() = default;
        // disable copy and move
        udp_receiver(const udp_receiver &) = delete;
        udp_receiver& operator=(const udp_receiver &) = delete;
        udp_receiver(udp_receiver &&) = delete;
        udp_receiver& operator=(udp_receiver &&) = delete;

      private:
        const bool forward_acks = false;
        vxsdr *sdrptr;
        boost::asio::ip::udp::socket socket;
        boost::asio::ip::udp::endpoint remote;
        largest_data_packet recv_buffer;
        largest_cmd_or_rsp_packet cmd_buffer;
        uint64_t packets_received = 0, sequence_errors = 0;
        std::array<uint64_t, UINT8_MAX + 1> packet_types_received = { 0 };
        uint16_t last_seq = 0;
        // FIXME: diagnostic info gathering: remove when unneeded
#ifdef SEQUENCE_ERROR_DIAGNOSTICS
        std::vector<std::chrono::time_point<std::chrono::system_clock> > seq_error_times;
        std::vector<uint8_t> seq_error_packet_type;
        std::vector<uint16_t> seq_error_counter;
        std::vector<uint16_t> seq_error_expected;
#endif // SEQUENCE_ERROR_DIAGNOSTICS
        void start_receive() {
          constexpr int    receive_thread_sleep_us        =        0;
          LOGGER(debug) << "start_receive started";
          // FIXME: need to use async read, handler, and deadline timer to implement timeout
          while(not sdrptr->receiver_thread_stop_flag) {
            LOGGER(debug) << "waiting for receive";
            socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote);
            LOGGER(debug) << "packet received";
            packets_received++;
            packet_types_received[recv_buffer.hdr.packet_type]++;

            if(packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1)) {
              sequence_errors++;
#ifdef SEQUENCE_ERROR_DIAGNOSTICS
              seq_error_times.push_back(std::chrono::system_clock::now());
              seq_error_packet_type.push_back(recv_buffer.hdr.packet_type);
              seq_error_counter.push_back(recv_buffer.hdr.sequence_counter);
              seq_error_expected.push_back((uint16_t)(last_seq + 1));
#endif // SEQUENCE_ERROR_DIAGNOSTICS
            }

            last_seq = recv_buffer.hdr.sequence_counter;
            switch(recv_buffer.hdr.packet_type) {
              // receive buffers go into the rx data queue
              case PACKET_TYPES::RX_SIGNAL_DATA:
                if(not sdrptr->rx_data_queue.push(recv_buffer)) {
                  LOGGER(error) << "error pushing to rx_data_queue";
                }
                break;
              // tx sginal data acks special cased since they contain throttling information
              case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
                {
                  auto *r = reinterpret_cast<tx_signal_data_ack_packet *>(&recv_buffer);
                  sdrptr->tx_buffer_used = r->tx_buffer_used_bytes;
                  sdrptr->tx_buffer_fill_percent = std::min(100U, (100U * sdrptr->tx_buffer_used) / sdrptr->tx_buffer_size);
                  if(forward_acks) {
                    auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                    sdrptr->response_queue.push(*p);
                  }
                }
                break;
              // the buffer info commands are special cased since they can also be used for TX throttling
              case PACKET_TYPES::DEVICE_CMD_RSP:
                switch(recv_buffer.hdr.command) {
                  case DEVICE_CMD_RSP::GET_BUFFER_USE_RSP:
                    {
                      auto *r = reinterpret_cast<get_buffer_use_rsp_packet *>(&recv_buffer);
                      sdrptr->tx_buffer_used = r->tx_buffer_used_bytes;
                      sdrptr->rx_buffer_used = r->rx_buffer_used_bytes;
                      sdrptr->tx_buffer_fill_percent = std::min(100U, (100U * sdrptr->tx_buffer_used) / sdrptr->tx_buffer_size);
                      sdrptr->rx_buffer_fill_percent = std::min(100U, (100U *  sdrptr->rx_buffer_used) / sdrptr->rx_buffer_size);
                      auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                      sdrptr->response_queue.push(*p);
                    }
                    break;
                  case DEVICE_CMD_RSP::GET_BUFFER_INFO_RSP:
                    {
                      auto *r = reinterpret_cast<get_buffer_info_rsp_packet *>(&recv_buffer);
                      sdrptr->tx_buffer_size = r->tx_buffer_size_bytes;
                      sdrptr->rx_buffer_size = r->rx_buffer_size_bytes;
                      auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                      sdrptr->response_queue.push(*p);
                    }
                    break;
                  default:
                    {
                      auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                      sdrptr->response_queue.push(*p);
                    }
                    break;
                }
                break;
              case PACKET_TYPES::DEVICE_CMD_ACK:
              case PACKET_TYPES::RX_RADIO_CMD_ACK:
              case PACKET_TYPES::TX_RADIO_CMD_ACK:
                if(forward_acks) {
                  auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                  sdrptr->response_queue.push(*p);
                }
                break;
              // DEVICE_CMD_RSP handled above
              case PACKET_TYPES::DEVICE_CMD_ERR:
              case PACKET_TYPES::RX_RADIO_CMD_RSP:
              case PACKET_TYPES::RX_RADIO_CMD_ERR:
              case PACKET_TYPES::TX_RADIO_CMD_RSP:
              case PACKET_TYPES::TX_RADIO_CMD_ERR:
                {
                  auto *p = reinterpret_cast<largest_cmd_or_rsp_packet *>(&recv_buffer);
                  sdrptr->response_queue.push(*p);
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

            if constexpr(receive_thread_sleep_us > 0) {
              std::this_thread::sleep_for(std::chrono::microseconds(receive_thread_sleep_us));
            }
          }

          LOGGER(info) << "start_receive exiting; " << packets_received << " packets received, " << sequence_errors << " sequence errors";
#ifdef SEQUENCE_ERROR_DIAGNOSTICS
          for(unsigned i =0; i < seq_error_times.size(); i++) {
            LOGGER(info) << boost::format(" %30s %20s %8d %8d") % format_time(seq_error_times[i]) % packet_type_to_name(seq_error_packet_type[i])
                                                                    % seq_error_counter[i] % seq_error_expected[i];
          }
#endif // SEQUENCE_ERROR_DIAGNOSTICS
          for(unsigned i = 0; i < packet_types_received.size(); i++) {
            if(packet_types_received[i] > 0) {
              LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_received[i] % packet_type_to_name(i) % i);
            }
          }
        }
    };

    class udp_sender {

      public:
        // constructor
        udp_sender(vxsdr *p, boost::asio::io_context &io_context, const boost::asio::ip::udp::endpoint &local_endpoint,
          const boost::asio::ip::udp::endpoint &destination_endpoint) : socket(io_context, local_endpoint) {
          sdrptr = p;
          destination = destination_endpoint;
          socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));

          boost::asio::socket_base::send_buffer_size option;
          socket.get_option(option);
          int network_buffer_size = option.value();
          LOGGER(info) << "network send buffer size set to " << network_buffer_size;

          if(not sdrptr->sender_thread_stop_flag) {
            start_send();
          }
        }
        // destructor
        ~udp_sender() = default;
        // disable copy and move
        udp_sender(const udp_sender &) = delete;
        udp_sender& operator=(const udp_sender &) = delete;
        udp_sender(udp_sender &&) = delete;
        udp_sender& operator=(udp_sender &&) = delete;
      private:
        vxsdr *sdrptr;
        boost::asio::ip::udp::socket socket;
        boost::asio::ip::udp::endpoint destination;
        std::array<uint64_t, UINT8_MAX + 1> packet_types_processed = { 0 };
        uint64_t send_errors = 0, packets_sent = 0, bytes_sent = 0;

        void send_packet(packet &packet) {
          packet_types_processed[packet.hdr.packet_type]++;
          packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (UINT16_MAX + 1));
          size_t bytes = socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination);
          if(bytes != packet.hdr.packet_size) {
            send_errors++;
          }
          bytes_sent += bytes;
        }

        void initialize_sdr(const uint8_t device_subdev, const uint8_t tx_subdev, const uint8_t rx_subdev) {
          // FIXME: implement reset    sdrptr->device_reset();
          sdrptr->tx_stop_now(tx_subdev);
          sdrptr->rx_stop_now(rx_subdev);
          // get device buffer sizes
          sdrptr->device_get_buffer_info(device_subdev);
          // wait for the response come back and be processed
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          // clear the status registers
          sdrptr->device_clear_status(device_subdev);
          sdrptr->device_get_status(device_subdev);
        }

        void close_sdr(const uint8_t device_subdev, const uint8_t tx_subdev, const uint8_t rx_subdev) {
          // get the status at the end
          sdrptr->device_get_status(device_subdev);
          // send a stop, requesting an ack so that the rx will see an incoming packet
          sdrptr->tx_stop_now(tx_subdev, FLAGS::REQUEST_ACK);
          sdrptr->rx_stop_now(rx_subdev, FLAGS::REQUEST_ACK);
        }

        void start_send() {
          LOGGER(debug) << "start_send started";
          static constexpr size_t data_buffer_size = 16, cmd_buffer_size = 16;
          std::array<largest_data_packet, data_buffer_size> data_buffer;
          std::array<largest_cmd_or_rsp_packet, cmd_buffer_size> cmd_buffer;

          constexpr float throttle_on_percent     = 75;
          constexpr float throttle_off_percent    = 50;
          bool throttling = false;
          constexpr unsigned throttle_amount_us   = 10; // send time for 8200 bytes over 10 GbE is about 6.7 us
          constexpr unsigned send_thread_sleep_us =  5;

          const uint8_t device_subdev = 0, tx_subdev = 0, rx_subdev = 0;

          initialize_sdr(device_subdev, tx_subdev, rx_subdev);

          uint64_t last_check = 0;
          constexpr uint64_t buffer_check_interval = 100; // current buffer size is about 1000 data packets

          while(not sdrptr->sender_thread_stop_flag) {
            if(sdrptr->command_queue.read_available() > 0) {
              size_t n_popped = sdrptr->command_queue.pop(&cmd_buffer.front(), cmd_buffer.size());
              for(size_t i = 0; i < n_popped; i++) {
                if(packets_sent - last_check > buffer_check_interval) {
                  // request ack to update buffer use
                  cmd_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
                  send_packet(cmd_buffer[i]);
                  last_check = packets_sent;
                }
                else {
                  send_packet(cmd_buffer[i]);
                }
              }
            }
            // FIXME: is a timeout needed?
            else if(sdrptr->tx_data_queue.read_available() > 0) {
              size_t n_popped = sdrptr->tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
              for(size_t i = 0; i < n_popped; i++) {
                if(packets_sent - last_check > buffer_check_interval) {
                  // request ack to update buffer use
                  data_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
                  send_packet(data_buffer[i]);
                  last_check = packets_sent;
                }
                else {
                  send_packet(data_buffer[i]);
                }
                // throttle if needed
                if(sdrptr->tx_buffer_fill_percent > throttle_on_percent) {
                  throttling = true;
                }
                else if(sdrptr->tx_buffer_fill_percent <= throttle_off_percent) {
                  throttling = false;
                }
                if(throttling) {
                  std::this_thread::sleep_for(std::chrono::microseconds(throttle_amount_us));
                }
              }
            }
            else {
              if constexpr(send_thread_sleep_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(send_thread_sleep_us));
              }
            }
          }
          close_sdr(device_subdev, tx_subdev, rx_subdev);
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          LOGGER(info) << "start_send exiting; " << packets_sent << " packets sent, " << send_errors << " send errors";
          for(unsigned i = 0; i < packet_types_processed.size(); i++) {
            if(packet_types_processed[i] > 0) {
              LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_processed[i] % packet_type_to_name(i) % i);
            }
          }
        }
    };
};

#endif // VXSDR_HPP