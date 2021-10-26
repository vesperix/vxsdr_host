#include <complex>
#include <vector>
#include <cstring>
#include <tuple>
#include <cstdint>
#include <optional>
#include <future>
#include <fstream>

#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/udp.hpp>
#ifdef VXSDR_ASYNC_RX
#include <boost/asio/use_future.hpp>
#endif // VXSDR_ASYNC_RX

#include "logging.hpp"
#include "thread_utils.h"
#include "utility.hpp"

#include "vxsdr_header.hpp"
#include "packet_proc.hpp"

#ifndef VXSDR_HPP
#define VXSDR_HPP

class vxsdr {

 private:
#ifdef VXSDR_ASYNC_RX
  // socket_receive_timeout setting for async rx
  static constexpr unsigned receiver_socket_timeout_ms    =      100;
#endif // VXSDR_ASYNC_RX

  // thread realtime priority settings
  static constexpr bool thread_set_priority = true;
  static constexpr int send_thread_priority = 20;
  static constexpr int receive_thread_priority = 20;

  // thread cpu affinity settings
  static constexpr bool thread_set_affinity = true;
  static constexpr int send_thread_cpu = 0;
  static constexpr int receive_thread_cpu = 2;

  // parameters of the internal spsc queues (ring buffers)
  static constexpr size_t rx_data_queue_length           =    65536;    // slots in rx data queue; 65536 * 2048 / 160e6 = 840 ms at 160 MSPS
  static constexpr size_t tx_data_queue_length           =     1024;    // slots in tx data queue;  1024 * 2048 / 160e6 =  13 ms at 160 MSPS
  static constexpr size_t command_queue_length           =       64;    // slots in command msg queue
  static constexpr size_t response_queue_length          =      256;    // slots in response msg queue
  static constexpr size_t error_queue_length             =      256;    // slots in error queue
  static constexpr size_t cmd_ack_queue_length           =      256;    // slots in command ack queue
  static constexpr size_t async_msg_queue_length         =      256;    // slots in async msg queue

  // timeouts and waits between tries for queue push/pop

  static constexpr unsigned tx_data_queue_timeout_us     =   100000;
  static constexpr unsigned tx_data_queue_wait_us        =       10;

  static constexpr unsigned rx_data_queue_timeout_us     =    40000;
  static constexpr unsigned rx_data_queue_wait_us        =       10;

  static constexpr unsigned command_queue_timeout_us     =   100000;
  static constexpr unsigned command_queue_wait_us        =       10;

  static constexpr unsigned response_queue_timeout_us    =   100000;
  static constexpr unsigned response_queue_wait_us       =       10;

  // timeouts and waits for responses from device
  static constexpr unsigned device_response_timeout_ms   =      100;
  static constexpr unsigned device_response_wait_ms      =        1;

  // the UDP tx waits for the rx to reach RUNNING state before entering RUNNING itself
  static constexpr unsigned udp_tx_ready_timeout_ms      =     1000;
  static constexpr unsigned udp_tx_ready_wait_ms         =        1;

  // parameters used to set the OS's network buffer
  static constexpr unsigned network_send_buffer_bytes    =   4194304;   // ~6.6 ms at 160 MSPS
  static constexpr unsigned network_receive_buffer_bytes =  16777216;   // ~26 ms at 160 MSPS


  // parameters used to monitor status of the radio's internal buffers (on the other end of the network connection)
  std::atomic<unsigned> tx_buffer_size_bytes = 0, tx_buffer_used_bytes = 0;
  std::atomic<unsigned> rx_buffer_size_bytes = 0, rx_buffer_used_bytes = 0;
  std::atomic<unsigned> tx_buffer_fill_percent = 0, rx_buffer_fill_percent = 0;

  using data_queue_element = largest_data_packet;
  using command_queue_element = largest_cmd_or_rsp_packet;
  using response_queue_element = largest_cmd_or_rsp_packet;
  using error_queue_element = largest_cmd_or_rsp_packet;
  using cmd_ack_queue_element = largest_cmd_or_rsp_packet;
  using async_msg_queue_element = largest_cmd_or_rsp_packet;

  // data queues
  boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<rx_data_queue_length> > rx_data_queue;
  boost::lockfree::spsc_queue<data_queue_element, boost::lockfree::capacity<tx_data_queue_length> > tx_data_queue;
  // command and response queues
  boost::lockfree::spsc_queue<command_queue_element, boost::lockfree::capacity<command_queue_length> > command_queue;
  boost::lockfree::spsc_queue<response_queue_element, boost::lockfree::capacity<response_queue_length> > response_queue;
  boost::lockfree::spsc_queue<error_queue_element, boost::lockfree::capacity<error_queue_length> > error_queue;
  boost::lockfree::spsc_queue<cmd_ack_queue_element, boost::lockfree::capacity<cmd_ack_queue_length> > cmd_ack_queue;
  boost::lockfree::spsc_queue<async_msg_queue_element, boost::lockfree::capacity<async_msg_queue_length> > async_msg_queue;

  // Boost context
  boost::asio::io_context context;

  // threads used for sending and receiving
  std::thread sender_thread;
  std::thread receiver_thread;
  std::thread context_thread;

  boost::asio::ip::udp::endpoint destination_endpoint;

  // flags used for startup
  enum interface_state { UNINITIALIZED, STARTING, WAITING, RUNNING, SHUTTING_DOWN, SHUTDOWN };
  std::atomic<interface_state> udp_tx_state { UNINITIALIZED }, udp_rx_state { UNINITIALIZED };

  // flags used for orderly shutdown
  std::atomic<bool> sender_thread_stop_flag = false, receiver_thread_stop_flag = false;

  uint64_t send_errors = 0, packets_sent = 0, samples_sent = 0, bytes_sent = 0;
  std::array<uint64_t, UINT8_MAX + 1> packet_types_sent = { 0 };

  uint64_t sequence_errors = 0, packets_received = 0, samples_received = 0, bytes_received = 0;
  std::array<uint64_t, UINT8_MAX + 1> packet_types_received = { 0 };

  // FIXME: diagnostic info gathering: remove when unneeded
#ifdef VXSDR_SEQ_ERR_DIAG
  std::vector<std::chrono::time_point<std::chrono::system_clock> > seq_error_times;
  std::vector<uint8_t> seq_error_packet_type;
  std::vector<uint16_t> seq_error_counter;
  std::vector<uint16_t> seq_error_expected;
#endif // VXSDR_SEQ_ERR_DIAG

 public:
  // constructor
  vxsdr(const boost::asio::ip::udp::endpoint &local_send_endpoint,
        const boost::asio::ip::udp::endpoint &local_receive_endpoint,
        const boost::asio::ip::udp::endpoint &radio_endpoint) :
    sender_socket(context, local_send_endpoint),
    receiver_socket(context, local_receive_endpoint) {
    LOG_DEBUG("vxsdr constructor entered");
    destination_endpoint = radio_endpoint;
#ifdef VXSDR_ASYNC_RX
    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/futures/daytime_client.cpp
    // We run the io_context off in its own thread so that it operates
    // completely asynchronously with respect to the rest of the program.
    //
    // boost::asio::execution needs Boost 1.76 or higher; use make_work_guard for backwards compatibility
    //auto work = boost::asio::require(context.get_executor(), boost::asio::execution::outstanding_work.tracked);
    auto work = boost::asio::make_work_guard(context);
    context_thread = std::thread([&] { context.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif // VXSDR_ASYNC_RX
    udp_rx_state = STARTING;
    receiver_thread = std::thread([&] { receiver(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    udp_tx_state = STARTING;
    sender_thread = std::thread([&] { sender(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    set_tx_enable(true);
    set_rx_enable(true);
    LOG_DEBUG("vxsdr constructor complete");
  };
  // destructor
  ~vxsdr() {
    LOG_DEBUG("vxsdr destructor entered");
    set_tx_enable(false);
    set_rx_enable(false);
    LOG_DEBUG("joining receiver_thread");
    receiver_thread_stop_flag = true;
    boost::system::error_code err;
#ifndef ASYNCH__RX
    // use shutdown() to terminate the blocking read
    receiver_socket.shutdown(boost::asio::ip::udp::socket::shutdown_receive, err);
    if(err != boost::system::errc::success and err != boost::system::errc::not_connected) {
      // the not connected error is expected since it's a UDP socket
      LOG_ERROR(fmt::format("receiver socket shutdown: {:s}", err.message()));
    }
#endif // VXSDR_ASYNC_RX
    if(receiver_thread.joinable()) {
      receiver_thread.join();
    }
    udp_rx_state = SHUTDOWN;
    receiver_socket.close(err);
    if(err != boost::system::errc::success) {
      LOG_ERROR(fmt::format("receiver socket close: {:s}", err.message()));
    }
    LOG_DEBUG("joining sender thread");
    sender_thread_stop_flag = true;
    if(sender_thread.joinable()) {
      sender_thread.join();
    }
    udp_tx_state = SHUTDOWN;
    sender_socket.close(err);
    if(err != boost::system::errc::success) {
      LOG_ERROR(fmt::format("sender socket close: {:s}", err.message()));
    }
#ifdef VXSDR_ASYNC_RX
    LOG_DEBUG("joining context thread");
    context_thread.join();
#endif // VXSDR_ASYNC_RX
    LOG_DEBUG("vxsdr destructor complete");
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
    LOG_DEBUG("get_rx_data entered");
    size_t n_received = 0;
    data_queue_element q;

    if(n_desired == 0) {
      n_desired = data.size();
    }
    data.clear();
    data.reserve(n_desired);

    LOG_INFO(fmt::format("receiving {:d} samples", n_desired));

    while(n_received < n_desired) {
      unsigned wait_time_us = 0;
      // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(rx_data_queue.read_available() == 0 and wait_time_us < rx_data_queue_timeout_us) {
        if(rx_data_queue_wait_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
          wait_time_us += rx_data_queue_wait_us;
        }
      }
      if(rx_data_queue.pop(q)) {
        bool has_time = (bool)(q.hdr.flags & FLAGS_TIME_PRESENT);
        bool has_stream_id = (bool)(q.hdr.flags & FLAGS_STREAM_ID_PRESENT);
        if(not has_time and not has_stream_id) {
          auto *p = reinterpret_cast<data_packet *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(data));
          n_received += data_samples;
        } else if(has_time and not has_stream_id) {
          auto *p = reinterpret_cast<data_packet_time *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(data));
          n_received += data_samples;
        } else if(has_stream_id and not has_time) {
          auto *p = reinterpret_cast<data_packet_stream *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header) - sizeof(stream_spec_t);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(data));
          n_received += data_samples;
        } else {
          auto *p = reinterpret_cast<data_packet_time_stream *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t) - sizeof(stream_spec_t);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(data));
          n_received += data_samples;
        }
      } else {
        LOG_ERROR(fmt::format("timeout popping from rx data queue ({:d} of {:d} samples)", n_received, n_desired));
      }
    }
    LOG_DEBUG(fmt::format("get_rx_data complete ({:d} samples)", data.size()));
    return n_received;
  }

  size_t write_rx_data(std::ofstream &out_stream, size_t n_desired, size_t write_size_samples = 65536) {
    LOG_DEBUG("write_rx_data entered");
    size_t n_received = 0, n_written = 0;
    data_queue_element q;

    if(write_size_samples < MAX_DATA_LENGTH_SAMPLES) {
      LOG_WARN(fmt::format("requested buffered write size too small; increasing to {:d} samples", MAX_DATA_LENGTH_SAMPLES));
      write_size_samples = MAX_DATA_LENGTH_SAMPLES;
    }

    std::vector<std::complex<int16_t>> buffer(2 * write_size_samples);

    LOG_INFO(fmt::format("receiving {:d} samples", n_desired));

    while(n_written < n_desired) {
      unsigned wait_time_us = 0;
      // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(rx_data_queue.read_available() == 0 and wait_time_us < rx_data_queue_timeout_us) {
        if(rx_data_queue_wait_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
          wait_time_us += rx_data_queue_wait_us;
        }
      }
      if(rx_data_queue.pop(q)) {
        bool has_time = (bool)(q.hdr.flags & FLAGS_TIME_PRESENT);
        bool has_stream_id = (bool)(q.hdr.flags & FLAGS_STREAM_ID_PRESENT);
        if(not has_time and not has_stream_id) {
          auto *p = reinterpret_cast<data_packet *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
          n_received += data_samples;
        } else if(has_time and not has_stream_id) {
          auto *p = reinterpret_cast<data_packet_time *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
          n_received += data_samples;
        } else if(has_stream_id and not has_time) {
          auto *p = reinterpret_cast<data_packet_stream *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header) - sizeof(stream_spec_t);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
          n_received += data_samples;
        } else {
          auto *p = reinterpret_cast<data_packet_time_stream *>(&q);
          size_t data_bytes = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t) - sizeof(stream_spec_t);
          size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
          std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
          n_received += data_samples;
        }
        if(buffer.size() >= write_size_samples or n_received >= n_desired) {
          size_t n_to_write = std::min(n_desired - n_written, write_size_samples);
          out_stream.write((char *)buffer.data(), sizeof(std::complex<int16_t>) * n_to_write);
          if(out_stream.fail()) {
            LOG_ERROR(fmt::format("error writing to output file ({:d}, {:d})", n_received, n_desired));
            break;
          }
          n_written += n_to_write;
          // copy anything that hasn't been written to the front of the buffer
          size_t n_to_copy = std::min(n_desired - n_written, buffer.size() - n_to_write);
          for(size_t i = 0; i < n_to_copy; i++) {
            buffer[i] = buffer[n_to_write + i];
          }
          buffer.resize(n_to_copy);
        }

      } else {
        LOG_ERROR(fmt::format("timeout popping from rx data queue ({:d} of {:d} samples)", n_written, n_desired));
      }
    }
    LOG_DEBUG(fmt::format("write_rx_data complete ({:d} samples)", n_written));
    return n_written;
  }

  size_t check_rx_data_sequence_only(size_t n_desired) {
    // FIXME: diagnostic routine for data transfer only;
    // drops data after sequence check and byte/sample counts
    LOG_DEBUG("check_rx_data_sequence_only started");
    size_t n_received = 0;
    data_queue_element q;

    LOG_INFO(fmt::format("checking sequence for {:d} samples", n_desired));

    while(n_received < n_desired) {
      unsigned wait_time_us = 0;
      // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(rx_data_queue.read_available() == 0 and wait_time_us < rx_data_queue_timeout_us) {
        if(rx_data_queue_wait_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
          wait_time_us += rx_data_queue_wait_us;
        }
      }
      if(rx_data_queue.pop(q)) {
        auto *p = reinterpret_cast<packet_header *>(&q);
        size_t data_bytes = p->packet_size - sizeof(packet_header);
        if((bool)(p->flags & FLAGS_TIME_PRESENT)) {
          data_bytes -= sizeof(time_spec_t);
        }
        if((bool)(p->flags & FLAGS_STREAM_ID_PRESENT)) {
          data_bytes -= sizeof(stream_spec_t);
        }

        size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
        n_received += data_samples;
      } else {
        LOG_ERROR(fmt::format("timeout popping from rx data queue ({:d} of {:d} samples)", n_received, n_desired));
      }
    }
    LOG_DEBUG(fmt::format("check_rx_data_sequence_only complete ({:d} samples)", n_received));
    return n_received;
  }

  size_t put_tx_data(std::vector<std::complex<int16_t> > data, const uint8_t subdev = 0) {
    LOG_DEBUG("put_tx_data started");
    // puts plain data_packets (no time, no stream)
    size_t n_put = 0;
    for(size_t i = 0; i < data.size(); i += MAX_DATA_LENGTH_SAMPLES) {
      data_queue_element q;
      auto *p = reinterpret_cast<data_packet *>(&q);
      unsigned n_samples = std::min(MAX_DATA_LENGTH_SAMPLES, data.size() - i);
      unsigned n_data_bytes = n_samples * sizeof(std::complex<int16_t>);
      uint16_t packet_size = sizeof(packet_header) + n_data_bytes;
      p->hdr = { PACKET_TYPE_TX_SIGNAL_DATA, 0, subdev, 0, packet_size, 0 };
      for(unsigned j = 0; j < n_samples; j++) {
        p->data[j] = data[i + j];
      }

      unsigned wait_time_us = 0;
      // setting tx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(tx_data_queue.write_available() == 0 and wait_time_us < tx_data_queue_timeout_us) {
        if(tx_data_queue_wait_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(tx_data_queue_wait_us));
          wait_time_us += tx_data_queue_wait_us;
        }
      }

      if(tx_data_queue.push(q)) {
        n_put += n_samples;
      } else {
        LOG_ERROR("timeout pushing to tx data queue");
        break;
      }
    }
    LOG_DEBUG(fmt::format("put_tx_data complete ({:d} samples)", n_put));
    return n_put;
  }

  void time_point_to_time_spec_t(const std::chrono::time_point<std::chrono::system_clock> &t, time_spec_t &ts) {
    auto secs = std::chrono::time_point_cast<std::chrono::seconds>(t);
    auto nsecs = std::chrono::time_point_cast<std::chrono::nanoseconds>(t) - std::chrono::time_point_cast<std::chrono::nanoseconds>(secs);
    ts = { (uint32_t)secs.time_since_epoch().count(), (uint32_t)nsecs.count() };
  }

  std::optional<std::tuple<uint32_t, uint32_t>> device_hello(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_HELLO, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<hello_rsp_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP and r->hdr.command == p.hdr.command) {
        return std::tuple<uint32_t, uint32_t>(r->fpga_fw_version, r->mcu_sw_version);
      } else {
        LOG_ERROR("invalid response in device_hello()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in device_hello()");
      return std::nullopt;
    }
  }

  bool device_reset(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_RESET, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool device_open_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_OPEN_SESSION, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool device_close_session(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_CLOSE_SESSION, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool device_clear_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_CLEAR_STATUS, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  std::optional<std::tuple<uint16_t, uint16_t, uint32_t, uint32_t, uint32_t, uint32_t>> device_get_status(const uint8_t subdev = 0, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_STATUS, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<status_rsp_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP and r->hdr.command == p.hdr.command) {
        return std::tuple<uint16_t, uint16_t, uint32_t, uint32_t, uint32_t, uint32_t>(r->fpga_temperature, r->dac_temperature,
                                    r->sequence_error_count, r->data_flow_error_flags, r->ddr_arbiter_status, r->ethernet_status);
      } else {
        LOG_ERROR("invalid response in device_get_status()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in device_get_status()");
      return std::nullopt;
    }
  }

  bool device_set_time_now(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0) {
    device_time_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NOW, subdev, flags, sizeof(p), 0 };
    time_point_to_time_spec_t(t, p.time);
    return cmd_queue_push_check(p);
  }

  bool device_set_time_next_pps(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0) {
    device_time_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NEXT_PPS, subdev, flags, sizeof(p), 0 };
    time_point_to_time_spec_t(t, p.time);
    return cmd_queue_push_check(p);
  }

  bool device_set_time_now(const int64_t sec, const int32_t nsec, const uint8_t subdev = 0, const uint8_t flags = 0) {
    device_time_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NOW, subdev, flags, sizeof(p), 0 };
    if(sec >= 0 and sec <= UINT32_MAX and nsec >= 0 and nsec < 1'000'000'000) {
      p.time.seconds = sec;
      p.time.nanoseconds = nsec;
      return cmd_queue_push_check(p);
    } else {
      LOG_ERROR("time out of range in device_set_time_now()");
    }
    return false;
  }

  bool device_set_time_next_pps(const int64_t sec, const int32_t nsec, const uint8_t subdev = 0, const uint8_t flags = 0) {
    device_time_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NEXT_PPS, subdev, flags, sizeof(p), 0 };
    if(sec >= 0 and sec <= UINT32_MAX and nsec >= 0 and nsec < 1'000'000'000) {
      p.time.seconds = sec;
      p.time.nanoseconds = nsec;
      return cmd_queue_push_check(p);
    } else {
      LOG_ERROR("time out of range in device_set_time_next_pps()");
    }
    return false;
  }

  bool device_get_time_now(uint32_t &sec, uint32_t &nsec, const uint8_t subdev = 0, const uint8_t flags = 0) {
    device_time_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIME, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return false;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<device_time_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP and r->hdr.command == p.hdr.command) {
        sec = r->time.seconds;
        nsec = r->time.nanoseconds;
        return true;
      } else {
        LOG_ERROR("invalid response in device_get_time_now()");
        sec = 0;
        nsec = 0;
      }
    } else {
      LOG_ERROR("response timeout in device_get_time_now()");
      sec = 0;
      nsec = 0;
    }
    return false;
  }

  std::optional<std::chrono::time_point<std::chrono::system_clock>> device_get_time_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
    device_time_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIME, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<device_time_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP and r->hdr.command == p.hdr.command) {
        return std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(r->time.seconds) +
                                                                  std::chrono::nanoseconds(r->time.nanoseconds));
      } else {
        LOG_ERROR("invalid response in device_get_time_now()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("response timeout in device_get_time_now()");
      return std::nullopt;
    }
  }

  bool tx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool rx_start_now_until_stop(const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet_samples p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
    p.n_samples = n;
    return cmd_queue_push_check(p);
  }

  bool rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet_samples p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
    p.n_samples = n;
    return cmd_queue_push_check(p);
  }

  bool tx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock> &t, const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet_time_samples p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
    time_point_to_time_spec_t(t, p.time);
    p.n_samples = n;
    return cmd_queue_push_check(p);
  }

  bool rx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock> &t,const uint64_t n, const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet_time_samples p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
    time_point_to_time_spec_t(t, p.time);
    p.n_samples = n;
    return cmd_queue_push_check(p);
  }

  bool tx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet_time p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_AT_TIME_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
    time_point_to_time_spec_t(t, p.time);
    return cmd_queue_push_check(p);
  }

  bool rx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet_time p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_AT_TIME_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
    time_point_to_time_spec_t(t, p.time);
    return cmd_queue_push_check(p);
  }

  bool tx_set_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_iq_bias_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    p.i_bias = i_bias;
    p.q_bias = q_bias;
    return cmd_queue_push_check(p);
  }

  bool rx_set_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_iq_bias_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    p.i_bias = i_bias;
    p.q_bias = q_bias;
    return cmd_queue_push_check(p);
  }

  bool tx_set_iq_corr(const double ii_corr, const double iq_corr, const double qi_corr, const double qq_corr,
      const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_iq_corr_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_IQ_CORR, subdev, flags, sizeof(p), 0 };
    p.ii_corr = ii_corr;
    p.iq_corr = iq_corr;
    p.qi_corr = qi_corr;
    p.qq_corr = qq_corr;
    return cmd_queue_push_check(p);
  }

  bool rx_set_iq_corr(const double ii_corr, const double iq_corr, const double qi_corr, const double qq_corr,
      const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_iq_corr_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_IQ_CORR, subdev, flags, sizeof(p), 0 };
    p.ii_corr = ii_corr;
    p.iq_corr = iq_corr;
    p.qi_corr = qi_corr;
    p.qq_corr = qq_corr;
    return cmd_queue_push_check(p);
  }

  bool tx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool rx_stop_now(const uint8_t subdev = 0, const uint8_t flags = 0) {
    stream_ctrl_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool set_tx_freq(const double freq, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    p.freq = freq;
    p.rtol = 1e-9;
    return cmd_queue_push_check(p);
  }

  bool set_rx_freq(const double freq, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    p.freq = freq;
    p.rtol = 1e-9;
    return cmd_queue_push_check(p);
  }

  std::optional<double> get_tx_freq(const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<radio_rf_freq_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_RSP and r->hdr.command == p.hdr.command) {
        return r->freq;
      } else {
        LOG_ERROR("invalid response in get_tx_freq()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in get_tx_freq()");
      return std::nullopt;
    }
  }

  std::optional<double> get_rx_freq(const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<radio_rf_freq_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_RSP and r->hdr.command == p.hdr.command) {
        return r->freq;
      } else {
        LOG_ERROR("invalid response in get_rx_freq()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in get_rx_freq()");
      return std::nullopt;
    }
  }

  bool set_tx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    p.gain = gain_db;
    return cmd_queue_push_check(p);
  }

  bool set_rx_gain(const double gain_db, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    p.gain = gain_db;
    return cmd_queue_push_check(p);
  }

  std::optional<double> get_tx_gain(const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<radio_rf_gain_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_RSP and r->hdr.command == p.hdr.command) {
        return r->gain;
      } else {
        LOG_ERROR("invalid response in get_tx_gain()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in get_tx_gain()");
      return std::nullopt;
    }
  }

  std::optional<double> get_rx_gain(const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<radio_rf_gain_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_RSP and r->hdr.command == p.hdr.command) {
        return r->gain;
      } else {
        LOG_ERROR("invalid response in get_rx_gain()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in get_rx_gain()");
      return std::nullopt;
    }
  }

  bool set_tx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    p.rate = rate_samples_sec;
    return cmd_queue_push_check(p);
  }

  bool set_rx_rate(const double rate_samples_sec, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    p.rate = rate_samples_sec;
    return cmd_queue_push_check(p);
  }

  std::optional<double> get_tx_rate(const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<radio_sample_rate_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_RSP and r->hdr.command == p.hdr.command) {
        return r->rate;
      } else {
        LOG_ERROR("invalid response in get_tx_rate()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in get_tx_rate()");
      return std::nullopt;
    }
  }

  std::optional<double> get_rx_rate(const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    if(not cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
      wait_time_ms += device_response_wait_ms;
    }
    if(got_response) {
      auto *r = reinterpret_cast<radio_sample_rate_packet *>(&q);
      if(r->hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_RSP and r->hdr.command == p.hdr.command) {
        return r->rate;
      } else {
        LOG_ERROR("invalid response in get_rx_rate()");
        return std::nullopt;
      }
    } else {
      LOG_ERROR("timeout in get_rx_rate()");
      return std::nullopt;
    }
  }

  bool set_tx_antenna(const std::string &ant, const uint8_t subdev = 0, const uint8_t flags = 0) {
    //FIXME: implement
    return false;
  }

  bool set_rx_antenna(const std::string &ant, const uint8_t subdev = 0, const uint8_t flags = 0) {
    //FIXME: implement
    return false;
  }

  std::optional<std::string> get_tx_antenna(const uint8_t subdev = 0, const uint8_t flags = 0) {
    //FIXME: implement
    return "not implemented";
  }

  std::optional<std::string> get_rx_antenna(const uint8_t subdev = 0, const uint8_t flags = 0) {
    //FIXME: implement
    return "not implemented";
  }

  bool set_tx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_enable_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
    if(enabled) {
      p.enabled = 1;
    } else {
      p.enabled = 0;
    }
    return cmd_queue_push_check(p);
  }

  bool set_rx_enable(const bool enabled, const uint8_t subdev = 0, const uint8_t flags = 0) {
    radio_rf_enable_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
    if(enabled) {
      p.enabled = 1;
    } else {
      p.enabled = 0;
    }
    return cmd_queue_push_check(p);
  }

 private:

  [[nodiscard]] bool cmd_queue_push_check(packet &p) {
    command_queue_element q;
    std::memcpy(&q, &p, std::min((size_t)p.hdr.packet_size, sizeof(q)));
    unsigned wait_time_us = 0;
    // setting command_queue_wait_us = 0 results in a busy wait with no timeout
    while(command_queue.write_available() == 0 and wait_time_us < command_queue_timeout_us) {
      if(command_queue_wait_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(command_queue_wait_us));
        wait_time_us += command_queue_wait_us;
      }
    }

    if(not command_queue.push(q)) {
      LOG_ERROR("timeout pushing to command queue");
      return false;
    }
    if((q.hdr.flags & FLAGS_REQUEST_ACK) != 0) {
      cmd_ack_queue_element a;
      bool got_response = false;
      unsigned wait_time_ms = 0;
      while(not (got_response = cmd_ack_queue.pop(a)) and wait_time_ms < device_response_timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
        wait_time_ms += device_response_wait_ms;
      }
      if(got_response) {
        auto *r = reinterpret_cast<ack_packet *>(&a);
        bool packet_types_match = false;
        if((q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD   and r->hdr.packet_type == PACKET_TYPE_DEVICE_CMD_ACK)   or
           (q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and r->hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_ACK) or
           (q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and r->hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_ACK)) {
          packet_types_match = true;
        }
        // for an ack to match, packet types, commands, and subdevices must all match
        if(packet_types_match and r->hdr.command == q.hdr.command and r->hdr.target_subdevice == q.hdr.target_subdevice) {
          return true;
        } else {
          LOG_ERROR("invalid ack received");
          return false;
        }
      } else {
        LOG_ERROR("timeout waiting for ack");
        return false;
      }
    }
    return true;
  }

  bool device_get_buffer_info(const uint8_t subdev, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_INFO, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  bool device_get_buffer_use(const uint8_t subdev, const uint8_t flags = 0) {
    cmd_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_USE, subdev, flags, sizeof(p), 0 };
    return cmd_queue_push_check(p);
  }

  static int device_cmd_has_response(const uint8_t cmd) {
    switch(cmd) {
      case DEVICE_CMD_HELLO:
      case DEVICE_CMD_GET_TIME:
      case DEVICE_CMD_GET_STATUS:
      case DEVICE_CMD_GET_BUFFER_INFO:
      case DEVICE_CMD_GET_BUFFER_USE:
      case DEVICE_CMD_GET_TRANSPORT_INFO:
      case DEVICE_CMD_GET_TRANSPORT_ADDR:
      case DEVICE_CMD_GET_MAX_PAYLOAD:
      case DEVICE_CMD_GET_CHANNEL_INFO:
      case DEVICE_CMD_GET_TIMING_INFO:
      case DEVICE_CMD_GET_TIMING_STATUS:
      case DEVICE_CMD_GET_TIMING_REF:
  //    case DEVICE_CMD_READ_DAC_REG:
  //    case DEVICE_CMD_READ_ADC_REG:
        return 1;
      case DEVICE_CMD_CLEAR_STATUS:
      case DEVICE_CMD_OPEN_SESSION:
      case DEVICE_CMD_CLOSE_SESSION:
      case DEVICE_CMD_SET_TRANSPORT_ADDR:
      case DEVICE_CMD_SET_MAX_PAYLOAD:
      case DEVICE_CMD_SAVE_TRANSPORT_ADDR:
      case DEVICE_CMD_SET_TIMING_REF:
      case DEVICE_CMD_SET_TIME_NOW:
      case DEVICE_CMD_SET_TIME_NEXT_PPS:
  //    case DEVICE_CMD_WRITE_DAC_REG:
  //    case DEVICE_CMD_WRITE_ADC_REG:
      case DEVICE_CMD_STOP:
      case DEVICE_CMD_RESET:
      default:
        return 0;
    }
  }

  static int radio_cmd_has_response(const uint8_t cmd) {
    switch(cmd) {
      case RADIO_CMD_GET_RF_INFO:
      case RADIO_CMD_GET_RF_FREQ:
      case RADIO_CMD_GET_RF_GAIN:
      case RADIO_CMD_GET_SAMPLE_RATE:
      case RADIO_CMD_GET_RF_BW:
      case RADIO_CMD_GET_RF_ENABLED:
      case RADIO_CMD_GET_RF_PORT:
      case RADIO_CMD_GET_NUM_SUBDEVS:
      case RADIO_CMD_GET_IQ_BIAS:
      case RADIO_CMD_GET_IQ_CORR:
        return 1;
      case RADIO_CMD_START_NOW_UNTIL_STOP:
      case RADIO_CMD_START_AT_TIME_UNTIL_STOP:
      case RADIO_CMD_STOP_NOW:
      case RADIO_CMD_START_NOW_FOR_N_SAMPLES:
      case RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES:
      case RADIO_CMD_SET_RF_FREQ:
      case RADIO_CMD_SET_RF_GAIN:
      case RADIO_CMD_SET_SAMPLE_RATE:
      case RADIO_CMD_SET_RF_BW:
      case RADIO_CMD_SET_RF_ENABLED:
      case RADIO_CMD_SET_RF_PORT:
      case RADIO_CMD_SET_IQ_BIAS:
      case RADIO_CMD_SET_IQ_CORR:
      default:
        return 0;
    }
  }

  void receiver() {
    LOG_DEBUG("receiver thread started");

    if(thread_set_priority) {
      if(set_current_thread_priority_realtime(receive_thread_priority) != 0) {
        LOG_ERROR("unable to set receiver thread realtime priority");
      }
    }

    if(thread_set_affinity) {
      if(set_current_thread_affinity(receive_thread_cpu) != 0) {
        LOG_ERROR("unable to set receiver thread CPU affinity");
      }
    }

    udp_receiver();
    context.run();
    LOG_DEBUG("receiver thread finished");
  }

  void sender() {
    LOG_DEBUG("sender thread started");

    if(thread_set_priority) {
      if(set_current_thread_priority_realtime(send_thread_priority) != 0) {
        LOG_ERROR("unable to set sender thread realtime priority");
      }
    }

    if(thread_set_affinity) {
      if(set_current_thread_affinity(send_thread_cpu) != 0) {
        LOG_ERROR("unable to set sender thread CPU affinity");
      }
    }

    udp_sender();
    context.run();
    LOG_DEBUG("sender thread finished");
  }

  boost::asio::ip::udp::socket sender_socket;
  boost::asio::ip::udp::socket receiver_socket;

  void udp_receiver() {

    receiver_socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    receiver_socket.set_option(boost::asio::socket_base::receive_buffer_size(network_receive_buffer_bytes));
    boost::asio::socket_base::receive_buffer_size option;
    receiver_socket.get_option(option);
    unsigned network_buffer_size = option.value();
    if(network_buffer_size != network_receive_buffer_bytes) {
      LOG_ERROR(fmt::format("cannot set network receive buffer size to {:d} (got {:d})",
                    network_receive_buffer_bytes, network_buffer_size));
    } else {
      LOG_INFO(fmt::format("network receive buffer size set to {:d}", network_buffer_size));
    }

    start_receive();
  }

  void start_receive() {
    LOG_DEBUG("start_receive started");
    boost::asio::ip::udp::endpoint remote_endpoint;
    uint16_t last_seq = 0;

    bytes_received = 0;
    samples_received = 0;
    packets_received = 0;
    sequence_errors = 0;

    udp_rx_state = RUNNING;

    while(not receiver_thread_stop_flag) {
      static data_queue_element recv_buffer;
      size_t bytes_in_packet = 0;
#ifndef VXSDR_ASYNC_RX
      bytes_in_packet = receiver_socket.receive_from(boost::asio::buffer(&recv_buffer, sizeof(recv_buffer)), remote_endpoint);
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
        bytes_in_packet = 0;
        //LOG_DEBUG("timeout");
      } else {
        bytes_in_packet = result.get();
      }
#endif // VXSDR_ASYNC_RX

      if(bytes_in_packet >= sizeof(packet_header) and remote_endpoint == destination_endpoint) {
        packets_received++;
        packet_types_received[recv_buffer.hdr.packet_type]++;
        bytes_received += bytes_in_packet;
        if(recv_buffer.hdr.packet_type == PACKET_TYPE_RX_SIGNAL_DATA) {
          uint16_t header_size = sizeof(packet_header);
          if((bool)(recv_buffer.hdr.flags & FLAGS_TIME_PRESENT)) {
            header_size += sizeof(time_spec_t);
          }
          if((bool)(recv_buffer.hdr.flags & FLAGS_STREAM_ID_PRESENT)) {
            header_size += sizeof(stream_spec_t);
          }
          samples_received += (recv_buffer.hdr.packet_size - header_size) / sizeof(std::complex<int16_t>);
        }
        if(packets_received > 1 and recv_buffer.hdr.sequence_counter != (uint16_t)(last_seq + 1)) {
          sequence_errors++;
#ifdef VXSDR_SEQ_ERR_DIAG
          seq_error_times.push_back(std::chrono::system_clock::now());
          seq_error_packet_type.push_back(recv_buffer.hdr.packet_type);
          seq_error_counter.push_back(recv_buffer.hdr.sequence_counter);
          seq_error_expected.push_back((uint16_t)(last_seq + 1));
#endif // VXSDR_SEQ_ERR_DIAG
        }

        last_seq = recv_buffer.hdr.sequence_counter;
        switch(recv_buffer.hdr.packet_type) {
          // receive buffers go into the rx data queue
          case PACKET_TYPE_RX_SIGNAL_DATA: {
              if(not rx_data_queue.push(recv_buffer)) {
                unsigned wait_time_us = 0;
                while(rx_data_queue.write_available() == 0 and wait_time_us < rx_data_queue_timeout_us) {
                  if(rx_data_queue_wait_us > 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
                    wait_time_us += rx_data_queue_wait_us;
                  }
                  else {
                    break; // avoid an endless loop if rx_data_queue_wait_us = 0
                  }
                }
                if(not rx_data_queue.push(recv_buffer)) {
                  LOG_ERROR(fmt::format("timeout pushing to rx data queue: {:d}", samples_received));
                }
              }
            }
            break;
          // tx sginal data acks special cased since they contain throttling information
          case PACKET_TYPE_TX_SIGNAL_DATA_ACK: {
              auto *r = reinterpret_cast<tx_signal_data_ack_packet *>(&recv_buffer);
              tx_buffer_used_bytes = r->tx_buffer_used_bytes;
              tx_buffer_fill_percent = std::min(100U, (100U * tx_buffer_used_bytes) / tx_buffer_size_bytes);
              // DO NOT PUSH DATA ACKS TO COMMAND ACK QUEUE!!
            }
            break;
          // the buffer info commands are special cased since they can also be used for TX throttling
          case PACKET_TYPE_DEVICE_CMD_RSP:
            switch(recv_buffer.hdr.command) {
              case DEVICE_CMD_GET_BUFFER_USE: {
                  auto *r = reinterpret_cast<get_buffer_use_rsp_packet *>(&recv_buffer);
                  tx_buffer_used_bytes = r->tx_buffer_used_bytes;
                  rx_buffer_used_bytes = r->rx_buffer_used_bytes;
                  tx_buffer_fill_percent = std::min(100U, (100U * tx_buffer_used_bytes) / tx_buffer_size_bytes);
                  rx_buffer_fill_percent = std::min(100U, (100U * rx_buffer_used_bytes) / rx_buffer_size_bytes);
                  //auto *p = reinterpret_cast<response_queue_element *>(&recv_buffer);
                  //response_queue.push(*p);
                }
                break;
              case DEVICE_CMD_GET_BUFFER_INFO: {
                  auto *r = reinterpret_cast<get_buffer_info_rsp_packet *>(&recv_buffer);
                  tx_buffer_size_bytes = r->tx_buffer_size_bytes;
                  rx_buffer_size_bytes = r->rx_buffer_size_bytes;
                  //auto *p = reinterpret_cast<response_queue_element *>(&recv_buffer);
                  //response_queue.push(*p);
                }
                break;
              default: {
                  auto *p = reinterpret_cast<response_queue_element *>(&recv_buffer);
                  response_queue.push(*p);
                }
                break;
            }
            break;
          case PACKET_TYPE_DEVICE_CMD_ACK:
          case PACKET_TYPE_RX_RADIO_CMD_ACK:
          case PACKET_TYPE_TX_RADIO_CMD_ACK: {
              auto *p = reinterpret_cast<cmd_ack_queue_element *>(&recv_buffer);
              cmd_ack_queue.push(*p);
            }
            break;
          // DEVICE_CMD_RSP handled above
          case PACKET_TYPE_ASYNC_MSG: {
              // FIXME: do whatever is needed to notify or shut down!!!
              auto *p = reinterpret_cast<async_msg_queue_element *>(&recv_buffer);
              async_msg_queue.push(*p);
            }
            break;
          case PACKET_TYPE_RX_RADIO_CMD_RSP:
          case PACKET_TYPE_TX_RADIO_CMD_RSP: {
              auto *p = reinterpret_cast<response_queue_element *>(&recv_buffer);
              response_queue.push(*p);
            }
            break;
          case PACKET_TYPE_DEVICE_CMD_ERR:
          case PACKET_TYPE_TX_RADIO_CMD_ERR:
          case PACKET_TYPE_RX_RADIO_CMD_ERR: {
              auto *p = reinterpret_cast<error_queue_element *>(&recv_buffer);
              error_queue.push(*p);
            }
            break;
          // packet types that should never show up at the host are sent to the error queue
          case PACKET_TYPE_TX_SIGNAL_DATA:
          case PACKET_TYPE_DEVICE_CMD:
          case PACKET_TYPE_RX_RADIO_CMD:
          case PACKET_TYPE_TX_RADIO_CMD:
          default: { // unrecognized packet types
              auto *p = reinterpret_cast<error_queue_element *>(&recv_buffer);
              error_queue.push(*p);
            }
            break;
        }
      }
    }

    udp_rx_state = SHUTTING_DOWN;

    LOG_INFO("start_receive exiting:");
    LOG_INFO(fmt::format("   {:12d} packets received", packets_received));
    for(unsigned i = 0; i < packet_types_received.size(); i++) {
      if(packet_types_received[i] > 0) {
        LOG_INFO(fmt::format("   {:12d} {:20s} ({:d})", packet_types_received[i], packet_type_to_name(i), i));
      }
    }
    LOG_INFO(fmt::format("   {:12d} samples received", samples_received));
    LOG_INFO(fmt::format("   {:12d} bytes received", bytes_received));
    LOG_INFO(fmt::format("   {:12d} sequence errors", sequence_errors));
#ifdef VXSDR_SEQ_ERR_DIAG
    for(unsigned i =0; i < seq_error_times.size(); i++) {
      LOG_INFO(fmt::format(" {:30s} {:20s} {:8d} {:8d}", format_time(seq_error_times[i]), packet_type_to_name(seq_error_packet_type[i]),
                   seq_error_counter[i], seq_error_expected[i]));
    }
#endif // VXSDR_SEQ_ERR_DIAG
  }

  void udp_sender() {

    sender_socket.set_option(boost::asio::socket_base::send_buffer_size(network_send_buffer_bytes));

    boost::asio::socket_base::send_buffer_size option;
    sender_socket.get_option(option);
    int network_buffer_size = option.value();

    if(network_buffer_size != network_send_buffer_bytes) {
      LOG_ERROR(fmt::format("cannot set network send buffer size to {:d} (got {:d})",
                    network_send_buffer_bytes, network_buffer_size));
    } else {
      LOG_INFO(fmt::format("network send buffer size set to {:d}", network_buffer_size));
    }

    start_send();
  }

  void send_packet(packet &packet) {
    packet_types_sent[packet.hdr.packet_type]++;
    packet.hdr.sequence_counter = (uint16_t)(packets_sent++ % (UINT16_MAX + 1));
    size_t bytes = sender_socket.send_to(boost::asio::buffer(&packet, packet.hdr.packet_size), destination_endpoint);
    if(bytes != packet.hdr.packet_size) {
      send_errors++;
    }
    if(packet.hdr.packet_type == PACKET_TYPE_TX_SIGNAL_DATA) {
      uint16_t header_size = sizeof(packet_header);
      if((bool)(packet.hdr.flags & FLAGS_TIME_PRESENT)) {
        header_size += sizeof(time_spec_t);
      }
      if((bool)(packet.hdr.flags & FLAGS_STREAM_ID_PRESENT)) {
        header_size += sizeof(stream_spec_t);
      }
      samples_sent += (packet.hdr.packet_size - header_size) / sizeof(std::complex<int16_t>);
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
    //device_get_status(device_subdev);
  }

  void stop_sdr(const uint8_t device_subdev, const uint8_t tx_subdev, const uint8_t rx_subdev) {
    // NOTE: this is normally called after the command queue loop has stopped,
    // so packets are set up and sent directly to the socket
    cmd_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_STOP_NOW, tx_subdev, 0, sizeof(p), 0 };
    send_packet(p);
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_STOP_NOW, rx_subdev, 0, sizeof(p), 0 };
    send_packet(p);
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_CLEAR_STATUS, device_subdev, 0, sizeof(p), 0 };    \
    send_packet(p);
  }

  void start_send() {
    LOG_DEBUG("start_send started");

    packets_sent = 0;
    samples_sent = 0;
    bytes_sent = 0;

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

    constexpr uint64_t initial_check_interval_samples = 9999; // a value that's unlikely to be equal to the updated interval
    uint64_t last_check = 0;
    uint64_t buffer_check_interval_samples = initial_check_interval_samples;

    udp_tx_state = WAITING;

    // wait for the rx to reach RUNNING
    unsigned total_wait_ms = 0;
    LOG_INFO("waiting for udp rx to reach RUNNING state");
    while(udp_rx_state != RUNNING and total_wait_ms < udp_tx_ready_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(udp_tx_ready_wait_ms));
      total_wait_ms += udp_tx_ready_wait_ms;
    }

    if(total_wait_ms >= udp_tx_ready_timeout_ms) {
      LOG_ERROR("udp tx timed out waiting for udp rx to reach RUNNING state; shutting down");
      receiver_thread_stop_flag = true;
      sender_thread_stop_flag = true;
    } else {
      LOG_INFO("udp rx in RUNNING state; udp tx starting");
      udp_tx_state = RUNNING;
    }

    while(not sender_thread_stop_flag) {
      if(command_queue.read_available() > 0) {
        size_t n_popped = command_queue.pop(&cmd_buffer.front(), cmd_buffer.size());
        for(size_t i = 0; i < n_popped; i++) {
          send_packet(cmd_buffer[i]);
        }
      }
      if(buffer_check_interval_samples == initial_check_interval_samples and tx_buffer_size_bytes > 0) {
        // once tx_buffer_size_bytes has been set, update buffer_check_interval_samples
        buffer_check_interval_samples = tx_buffer_size_bytes / sizeof(std::complex<int16_t>) / 32; // check every 1/32 of a buffer
      }
      else if(tx_data_queue.read_available() > 0) {
        size_t n_popped = tx_data_queue.pop(&data_buffer.front(), data_buffer.size());
        for(size_t i = 0; i < n_popped; i++) {
          if(samples_sent - last_check >= buffer_check_interval_samples) {
            // request ack to update buffer use
            data_buffer[i].hdr.flags |= FLAGS_REQUEST_ACK;
            send_packet(data_buffer[i]);
            last_check = samples_sent;
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
        if(send_thread_sleep_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(send_thread_sleep_us));
        }
      }
    }
    stop_sdr(device_subdev, tx_subdev, rx_subdev);
    udp_tx_state = SHUTTING_DOWN;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("start_send exiting:");
    LOG_INFO(fmt::format("   {:12d} packets sent", packets_sent));
    for(unsigned i = 0; i < packet_types_sent.size(); i++) {
      if(packet_types_sent[i] > 0) {
        LOG_INFO(fmt::format("   {:12d} {:20s} ({:d})", packet_types_sent[i], packet_type_to_name(i), i));
      }
    }
    LOG_INFO(fmt::format("   {:12d} samples sent", samples_sent));
    LOG_INFO(fmt::format("   {:12d} bytes sent", bytes_sent));
    LOG_INFO(fmt::format("   {:12d} send errors", send_errors));
  }
};

#endif // VXSDR_HPP