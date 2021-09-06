#include <complex>
#include <vector>
#include <cstring>
#include <future>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/use_future.hpp>

#include "logging.hpp"
#include "thread_utils.h"
#include "utility.hpp"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

#ifndef VXSDR_HPP
#define VXSDR_HPP

class vxsdr {

 private:
  // socket_receive_timeout setting
  static constexpr unsigned receiver_socket_timeout_ms    =      100;

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
  static constexpr size_t command_queue_length           =       64;    // slots in command msg queue
  static constexpr size_t response_queue_length          =      256;    // slots in response msg queue

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
  std::atomic<unsigned> tx_buffer_size = 0, tx_buffer_used = 0;
  std::atomic<unsigned> rx_buffer_size = 0, rx_buffer_used = 0;
  std::atomic<unsigned> tx_buffer_fill_percent = 0, rx_buffer_fill_percent = 0;

  using data_queue_element = largest_data_packet;
  using command_queue_element = largest_cmd_or_rsp_packet;
  using response_queue_element = largest_cmd_or_rsp_packet;

  // data queues
  boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
  boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;
  // command and response queues
  boost::lockfree::spsc_queue<command_queue_element, boost::lockfree::capacity<command_queue_length> > command_queue;
  boost::lockfree::spsc_queue<response_queue_element, boost::lockfree::capacity<response_queue_length> > response_queue;

  // Boost context
  boost::asio::io_context context;

  // threads used for sending and receiving
  std::thread sender_thread;
  std::thread receiver_thread;
  std::thread context_thread;

  boost::asio::ip::udp::endpoint destination_endpoint;

  // flags used for orderly shutdown
  std::atomic<bool> sender_thread_stop_flag = false, receiver_thread_stop_flag = false;

  const bool forward_acks = false;

  uint64_t send_errors = 0, packets_sent = 0, bytes_sent = 0;
  std::array<uint64_t, UINT8_MAX + 1> packet_types_sent = { 0 };

  uint64_t packets_received = 0, sequence_errors = 0;
  std::array<uint64_t, UINT8_MAX + 1> packet_types_received = { 0 };

  // FIXME: diagnostic info gathering: remove when unneeded
#ifdef SEQUENCE_ERROR_DIAGNOSTICS
  std::vector<std::chrono::time_point<std::chrono::system_clock> > seq_error_times;
  std::vector<uint8_t> seq_error_packet_type;
  std::vector<uint16_t> seq_error_counter;
  std::vector<uint16_t> seq_error_expected;
#endif // SEQUENCE_ERROR_DIAGNOSTICS

 public:
  // constructor
  vxsdr(const boost::asio::ip::udp::endpoint &local_send_endpoint,
        const boost::asio::ip::udp::endpoint &local_receive_endpoint,
        const boost::asio::ip::udp::endpoint &radio_endpoint) :
    sender_socket(context, local_send_endpoint),
    receiver_socket(context, local_receive_endpoint) {
    LOGGER(debug) << "vxsdr constructor entered";
    tx_data_queue.reset();
    rx_data_queue.reset();
    command_queue.reset();
    response_queue.reset();
    destination_endpoint = radio_endpoint;

    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/futures/daytime_client.cpp
    // We run the io_context off in its own thread so that it operates
    // completely asynchronously with respect to the rest of the program.
    auto work = boost::asio::require(context.get_executor(), boost::asio::execution::outstanding_work.tracked);
    context_thread = std::thread([=] { context.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    receiver_thread = std::thread([=] { receiver(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sender_thread = std::thread([=] { sender(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    set_tx_enable(true);
    set_rx_enable(true);
    LOGGER(debug) << "vxsdr constructor complete";
  };
  // destructor
  ~vxsdr() {
    LOGGER(debug) << "vxsdr destructor entered";
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
    LOGGER(debug) << "joining context thread";
    context_thread.join();
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
    data_queue_element q;

    data.clear();
    if(n_desired > 0) {
      data.reserve(n_desired);
    } else {
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
          data_packet p;
          std::memcpy(&p, &q, std::min(sizeof(q), sizeof(p)));
          size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                                      (p.hdr.packet_size - sizeof(packet_header)) / sizeof(std::complex<int16_t>));
          std::copy_n(std::begin(p.data), data_size, std::back_inserter(data));
        } else if(has_time and not has_stream_id) {
          data_packet_time p;
          std::memcpy(&p, &q, std::min(sizeof(q), sizeof(p)));
          size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                                      (p.hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t)) / sizeof(std::complex<int16_t>));
          std::copy_n(std::begin(p.data), data_size, std::back_inserter(data));
        } else if(has_stream_id and not has_time) {
          data_packet_stream p;
          std::memcpy(&p, &q, std::min(sizeof(q), sizeof(p)));
          size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                                      (p.hdr.packet_size - sizeof(packet_header) - sizeof(stream_spec_t)) / sizeof(std::complex<int16_t>));
          std::copy_n(std::begin(p.data), data_size, std::back_inserter(data));
        } else {
          data_packet_time_stream p;
          std::memcpy(&p, &q, std::min(sizeof(q), sizeof(p)));
          size_t data_size = std::min(MAX_DATA_LENGTH_SAMPLES,
                                      (p.hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t) - sizeof(stream_spec_t)) / sizeof(std::complex<int16_t>));
          std::copy_n(std::begin(p.data), data_size, std::back_inserter(data));
        }
        n_popped++;
      } else {
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
      data_queue_element q;
      data_packet p;
      unsigned n_samples = std::min(MAX_DATA_LENGTH_SAMPLES, data.size() - i);
      unsigned n_data_bytes = n_samples * sizeof(std::complex<int16_t>);
      uint16_t packet_size = sizeof(packet_header) + n_data_bytes;
      p.hdr = { PACKET_TYPES::TX_SIGNAL_DATA, 0, subdev, 0, packet_size, 0 };
      for(unsigned j = 0; j < n_samples; j++) {
        p.data[j] = data[i + j];
      }
      std::memcpy(&q, &p, std::min(sizeof(q), sizeof(p)));

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
      } else {
        LOGGER(error) << "timeout pushing to tx data queue";
        break;
      }
    }
    return n_put;
  }

  void cmd_queue_push(packet &p) {
    command_queue_element q;
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

  void device_hello(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::HELLO, subdev, flags, sizeof(p), 0 };
    cmd_queue_push(p);
  }

  void device_reset(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::RESET, subdev, flags, sizeof(p), 0 };
    cmd_queue_push(p);
  }

  void device_open_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::OPEN_SESSION, subdev, flags, sizeof(p), 0 };
    cmd_queue_push(p);
  }

  void device_close_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLOSE_SESSION, subdev, flags, sizeof(p), 0 };
    cmd_queue_push(p);
  }

  void device_clear_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPES::DEVICE_CMD, DEVICE_CMD::CLEAR_STATUS, subdev, flags, sizeof(p), 0 };
    cmd_queue_push(p);
  }

  void device_get_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
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
    } else {
      p.enabled = 0;
    }
    cmd_queue_push(p);
  }

  void set_rx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_enable_packet p;
    p.hdr = { PACKET_TYPES::RX_RADIO_CMD, RADIO_CMD::SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
    if(enabled) {
      p.enabled = 1;
    } else {
      p.enabled = 0;
    }
    cmd_queue_push(p);
  }

 private:
  boost::asio::ip::udp::socket sender_socket;
  boost::asio::ip::udp::socket receiver_socket;

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

  void receiver() {
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

    udp_receiver();
    context.run();
    LOGGER(debug) << "receiver thread finished";
  }

  void sender() {
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

    udp_sender();
    context.run();
    LOGGER(debug) << "sender thread finished";
  }

  void udp_receiver() {

    receiver_socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    receiver_socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));
    boost::asio::socket_base::receive_buffer_size option;
    receiver_socket.get_option(option);
    unsigned network_buffer_size = option.value();
    if(network_buffer_size != network_receive_buffer_bytes) {
      LOGGER(error) << "cannot set network receive buffer size to "
                    << network_receive_buffer_bytes << " (got " << network_buffer_size << ")";
    } else {
      LOGGER(info) << "network receive buffer size set to " << network_buffer_size;
    }

    start_receive();
    receiver_socket.close();
  }

  void start_receive() {
    LOGGER(debug) << "start_receive started";
    boost::asio::ip::udp::endpoint remote_endpoint;
    uint16_t last_seq = 0;

    while(not receiver_thread_stop_flag) {

      data_queue_element recv_buffer;
      size_t bytes_received = 0;

#ifdef SYNCH_RX
      bytes_received = receiver_socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote_endpoint);
#else
/*
    https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/futures/daytime_client.cpp

    std::array<char, 128> recv_buf;
    udp::endpoint sender_endpoint;
    std::future<std::size_t> recv_length =
      socket.async_receive_from(
          boost::asio::buffer(recv_buf),
          sender_endpoint,
          boost::asio::use_future);

    // Do other things here while the receive completes.

    std::cout.write(
        recv_buf.data(),
        recv_length.get()); // Blocks until receive is complete.
*/
      std::future<size_t> result = receiver_socket.async_receive_from(boost::asio::buffer(&recv_buffer,
                                                                      sizeof(recv_buffer)), remote_endpoint,
                                                                      boost::asio::use_future);
      if(result.wait_for(std::chrono::milliseconds(receiver_socket_timeout_ms)) == std::future_status::timeout) {
        bytes_received = 0;
        //LOGGER(debug) << "timeout";
      }
      else {
        bytes_received = result.get();
      }
#endif


      if(bytes_received >= sizeof(packet) and remote_endpoint == destination_endpoint) {
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
          if(not rx_data_queue.push(recv_buffer)) {
            LOGGER(error) << "error pushing to rx_data_queue";
          }
          break;
        // tx sginal data acks special cased since they contain throttling information
        case PACKET_TYPES::TX_SIGNAL_DATA_ACK: {
          tx_signal_data_ack_packet r;
          std::memcpy(&r, &recv_buffer, std::min(sizeof(r), sizeof(recv_buffer)));
          tx_buffer_used = r.tx_buffer_used_bytes;
          tx_buffer_fill_percent = std::min(100U, (100U * tx_buffer_used) / tx_buffer_size);
          if(forward_acks) {
            response_queue_element p;
            std::memcpy(&p, &r, std::min(sizeof(p), sizeof(r)));
            response_queue.push(p);
          }
        }
        break;
        // the buffer info commands are special cased since they can also be used for TX throttling
        case PACKET_TYPES::DEVICE_CMD_RSP:
          switch(recv_buffer.hdr.command) {
          case DEVICE_CMD_RSP::GET_BUFFER_USE_RSP: {
            get_buffer_use_rsp_packet r;
            std::memcpy(&r, &recv_buffer, std::min(sizeof(r), sizeof(recv_buffer)));
            tx_buffer_used = r.tx_buffer_used_bytes;
            rx_buffer_used = r.rx_buffer_used_bytes;
            tx_buffer_fill_percent = std::min(100U, (100U * tx_buffer_used) / tx_buffer_size);
            rx_buffer_fill_percent = std::min(100U, (100U *  rx_buffer_used) / rx_buffer_size);
            response_queue_element p;
            std::memcpy(&p, &r, std::min(sizeof(p), sizeof(r)));
            response_queue.push(p);
          }
          break;
          case DEVICE_CMD_RSP::GET_BUFFER_INFO_RSP: {
            get_buffer_info_rsp_packet r;
            std::memcpy(&r, &recv_buffer, sizeof(r));
            tx_buffer_size = r.tx_buffer_size_bytes;
            rx_buffer_size = r.rx_buffer_size_bytes;
            response_queue_element p;
            std::memcpy(&p, &r, std::min(sizeof(p), sizeof(r)));
            response_queue.push(p);
          }
          break;
          default: {
            response_queue_element p;
            std::memcpy(&p, &recv_buffer, std::min(sizeof(p), sizeof(recv_buffer)));
            response_queue.push(p);
          }
          break;
          }
          break;
        case PACKET_TYPES::DEVICE_CMD_ACK:
        case PACKET_TYPES::RX_RADIO_CMD_ACK:
        case PACKET_TYPES::TX_RADIO_CMD_ACK:
          if(forward_acks) {
            response_queue_element p;
            std::memcpy(&p, &recv_buffer, std::min(sizeof(p), sizeof(recv_buffer)));
            response_queue.push(p);
          }
          break;
        // DEVICE_CMD_RSP handled above
        case PACKET_TYPES::DEVICE_CMD_ERR:
        case PACKET_TYPES::RX_RADIO_CMD_RSP:
        case PACKET_TYPES::RX_RADIO_CMD_ERR:
        case PACKET_TYPES::TX_RADIO_CMD_RSP:
        case PACKET_TYPES::TX_RADIO_CMD_ERR: {
          response_queue_element p;
          std::memcpy(&p, &recv_buffer, std::min(sizeof(p), sizeof(recv_buffer)));
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

  void udp_sender() {

    sender_socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));

    boost::asio::socket_base::send_buffer_size option;
    sender_socket.get_option(option);
    int network_buffer_size = option.value();

    if(network_buffer_size != network_send_buffer_bytes) {
      LOGGER(error) << "cannot set network send buffer size to "
                    << network_send_buffer_bytes << "(got " << network_buffer_size << ")";
    } else {
      LOGGER(info) << "network send buffer size set to " << network_buffer_size;
    }

    start_send();
    sender_socket.close();
  }

  void send_packet(packet &packet) {
    packet_types_sent[packet.hdr.packet_type]++;
    packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (UINT16_MAX + 1));
    size_t bytes = sender_socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination_endpoint);
    if(bytes != packet.hdr.packet_size) {
      send_errors++;
    }
    bytes_sent += bytes;
  }

  void initialize_sdr(const uint8_t device_subdev, const uint8_t tx_subdev, const uint8_t rx_subdev) {
    // FIXME: implement reset    device_reset();
    tx_stop_now(tx_subdev);
    rx_stop_now(rx_subdev);
    // get device buffer sizes
    device_get_buffer_info(device_subdev);
    // wait for the response come back and be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // clear the status registers
    device_clear_status(device_subdev);
    device_get_status(device_subdev);
  }

  void close_sdr(const uint8_t device_subdev, const uint8_t tx_subdev, const uint8_t rx_subdev) {
    // get the status at the end
    device_get_status(device_subdev);
    // send a stop, requesting an ack so that the rx will see an incoming packet
    tx_stop_now(tx_subdev, FLAGS::REQUEST_ACK);
    rx_stop_now(rx_subdev, FLAGS::REQUEST_ACK);
  }

  void start_send() {
    LOGGER(debug) << "start_send started";
    static constexpr size_t data_buffer_size = 16, cmd_buffer_size = 16;
    std::array<data_queue_element, data_buffer_size> data_buffer;
    std::array<command_queue_element, cmd_buffer_size> cmd_buffer;

    constexpr float throttle_on_percent     = 75;
    constexpr float throttle_off_percent    = 50;
    bool throttling = false;
    constexpr unsigned throttle_amount_us   = 10; // send time for 8200 bytes over 10 GbE is about 6.7 us
    constexpr unsigned send_thread_sleep_us =  5;

    const uint8_t device_subdev = 0, tx_subdev = 0, rx_subdev = 0;

    initialize_sdr(device_subdev, tx_subdev, rx_subdev);

    uint64_t last_check = 0;
    constexpr uint64_t buffer_check_interval = 100; // current buffer size is about 1000 data packets

    while(not sender_thread_stop_flag) {
      if(command_queue.read_available() > 0) {
        size_t n_popped = command_queue.pop(&cmd_buffer.front(), cmd_buffer.size());
        for(size_t i = 0; i < n_popped; i++) {
          if(packets_sent - last_check > buffer_check_interval) {
            // request ack to update buffer use
            cmd_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
            send_packet(cmd_buffer[i]);
            last_check = packets_sent;
          } else {
            send_packet(cmd_buffer[i]);
          }
        }
      }
      // FIXME: is a timeout needed?
      else if(tx_data_queue.read_available() > 0) {
        size_t n_popped = tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
        for(size_t i = 0; i < n_popped; i++) {
          if(packets_sent - last_check > buffer_check_interval) {
            // request ack to update buffer use
            data_buffer[i].hdr.flags |= FLAGS::REQUEST_ACK;
            send_packet(data_buffer[i]);
            last_check = packets_sent;
          } else {
            send_packet(data_buffer[i]);
          }
          // throttle if needed
          if(tx_buffer_fill_percent > throttle_on_percent) {
            throttling = true;
          } else if(tx_buffer_fill_percent <= throttle_off_percent) {
            throttling = false;
          }
          if(throttling) {
            std::this_thread::sleep_for(std::chrono::microseconds(throttle_amount_us));
          }
        }
      } else {
        if constexpr(send_thread_sleep_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(send_thread_sleep_us));
        }
      }
    }
    close_sdr(device_subdev, tx_subdev, rx_subdev);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOGGER(info) << "start_send exiting; " << packets_sent << " packets sent, " << send_errors << " send errors";
    for(unsigned i = 0; i < packet_types_sent.size(); i++) {
      if(packet_types_sent[i] > 0) {
        LOGGER(info) << (boost::format("   %12d %20s (%d)") % packet_types_sent[i] % packet_type_to_name(i) % i);
      }
    }
  }
};

#endif // VXSDR_HPP