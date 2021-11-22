#include "vxsdr.hpp"

/*! @file vxsdr.cpp
    @brief Function definitions for the @p vxsdr class.
*/

  vxsdr::vxsdr(const net::ip::udp::endpoint &local_send_endpoint,
        const net::ip::udp::endpoint &local_receive_endpoint,
        const net::ip::udp::endpoint &device_endpoint,
        const bool nondata_packet_logging,
        const bool set_affinity, const unsigned base_cpu_number,
        const bool set_priority, const unsigned base_priority) :
    sender_socket(context, local_send_endpoint),
    receiver_socket(context, local_receive_endpoint) {
    LOG_DEBUG("vxsdr constructor entered");
    log_nondata_packets = nondata_packet_logging;
    thread_set_affinity = set_affinity;
    send_thread_cpu = base_cpu_number;
    receive_thread_cpu = base_cpu_number + 1;
    thread_set_priority = set_priority;
    send_thread_priority = base_priority;
    receive_thread_priority = base_priority;
    destination_endpoint = device_endpoint;
#ifdef VXSDR_ASYNC_RX
    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/futures/daytime_client.cpp
    // We run the io_context off in its own thread so that it operates
    // completely asynchronously with respect to the rest of the program.
    //
    // net::execution needs Boost 1.76 or higher; use make_work_guard for backwards compatibility
    //auto work = net::require(context.get_executor(), net::execution::outstanding_work.tracked);
    auto work = net::make_work_guard(context);
    context_thread = std::thread([&] { context.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif // VXSDR_ASYNC_RX
    async_handler_thread = std::thread([&] { async_handler(); });
    udp_rx_state = STARTING;
    receiver_thread = std::thread([&] { receiver(); });
    udp_tx_state = STARTING;
    sender_thread = std::thread([&] { sender(); });
    unsigned total_wait_time_ms = 0;
    while(udp_tx_state != RUNNING or udp_rx_state != RUNNING) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      total_wait_time_ms++;
      if(total_wait_time_ms > 1000) {
        LOG_ERROR(fmt::format("timeout waiting for UDP transport: tx = {:s} rx ={:s}",
             vxsdr::transport_state_to_string(udp_tx_state), vxsdr::transport_state_to_string(udp_rx_state)));
        throw std::runtime_error("timeout waiting for UDP transport in vxsdr constructor");
      }
    }
    if(not tx_stop_now() or
       not rx_stop_now() or
       not device_get_buffer_info() or
       not set_tx_enable(true) or
       not set_rx_enable(true)) {
      LOG_ERROR("error initializing sdr");
      throw std::runtime_error("error initializing sdr in vxsdr constructor");
    }
    LOG_DEBUG("vxsdr constructor complete");
  }

  vxsdr::~vxsdr() {
    LOG_DEBUG("vxsdr destructor entered");
    set_tx_enable(false);
    set_rx_enable(false);
    LOG_DEBUG("joining receiver_thread");
    receiver_thread_stop_flag = true;
    boost::system::error_code err;
#ifndef VXSDR_ASYNCH__RX
    // use shutdown() to terminate the blocking read
    receiver_socket.shutdown(net::ip::udp::socket::shutdown_receive, err);
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
    LOG_DEBUG("joining error_handler thread");
    if(async_handler_thread.joinable()) {
      async_handler_thread.join();
    }
    LOG_DEBUG("vxsdr destructor complete");
  }

  size_t vxsdr::get_rx_data(std::vector<std::complex<int16_t> > &data, size_t n_desired, const double timeout_s) {
    LOG_DEBUG("get_rx_data entered");
    size_t n_received = 0;
    data_queue_element q;

    if(timeout_s <= 0.0) {
      LOG_ERROR("timeout_s must be positive in get_rx_data()");
      return 0;
    } else if(timeout_s > 3600.0) {
      LOG_ERROR("timeout_s must 3600 or less in get_rx_data()");
      return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);

    if(vxsdr::rx_stream_state != STREAMING and vxsdr::rx_stream_state != WAITING_FOR_START) {
      LOG_ERROR(fmt::format("rx stream state is {:s} in get_rx_data()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
      return 0;
    }

    if(n_desired == 0) {
      n_desired = data.size();
    } else {
      data.reserve(n_desired);
    }

    data.clear();

    LOG_INFO(fmt::format("receiving {:d} samples", n_desired));

    while(n_received < n_desired) {
      uint32_t wait_time_us = 0;
      // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(rx_data_queue.read_available() == 0 and wait_time_us < timeout_us) {
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
        vxsdr::rx_stream_state = STREAM_ERROR;
        data.resize(n_received);
        return n_received;
      }
    }
    LOG_DEBUG(fmt::format("get_rx_data complete ({:d} samples)", data.size()));
    return n_received;
  }

  size_t vxsdr::write_rx_data(std::ofstream &out_stream, const size_t n_desired, const double timeout_s,
                              size_t write_size_samples) {
    LOG_DEBUG("write_rx_data entered");

    if(timeout_s <= 0.0) {
      LOG_ERROR("timeout_s must be positive in write_rx_data()");
      return 0;
    } else if(timeout_s > 3600.0) {
      LOG_ERROR("timeout_s must 3600 or less in write_rx_data()");
      return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);

    if(vxsdr::rx_stream_state != STREAMING and vxsdr::rx_stream_state != WAITING_FOR_START) {
      LOG_ERROR(fmt::format("rx stream state is {:s} in write_rx_data()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
      return 0;
    }

    size_t n_received = 0, n_written = 0;
    data_queue_element q;

    if(write_size_samples < MAX_DATA_LENGTH_SAMPLES) {
      LOG_WARN(fmt::format("requested buffered write size too small; increasing to {:d} samples", MAX_DATA_LENGTH_SAMPLES));
      write_size_samples = MAX_DATA_LENGTH_SAMPLES;
    }

    std::vector<std::complex<int16_t>> buffer(2 * write_size_samples);

    LOG_INFO(fmt::format("receiving {:d} samples", n_desired));

    while(n_written < n_desired) {
      uint32_t wait_time_us = 0;
      // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(rx_data_queue.read_available() == 0 and wait_time_us < timeout_us) {
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
        vxsdr::rx_stream_state = STREAM_ERROR;
        return n_written;
      }
    }
    LOG_DEBUG(fmt::format("write_rx_data complete ({:d} samples)", n_written));
    return n_written;
  }

  size_t vxsdr::check_rx_data_sequence_only(size_t n_desired, const double timeout_s) {
    // FIXME: diagnostic routine for data transfer only;
    // drops data after sequence check and byte/sample counts
    LOG_DEBUG("check_rx_data_sequence_only started");

    if(timeout_s <= 0.0) {
      LOG_ERROR("timeout_s must be positive in check_rx_data_sequence_only()");
      return 0;
    } else if(timeout_s > 3600.0) {
      LOG_ERROR("timeout_s must 3600 or less in check_rx_data_sequence_only()");
      return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);

    if(vxsdr::rx_stream_state != STREAMING and vxsdr::rx_stream_state != WAITING_FOR_START) {
      LOG_ERROR(fmt::format("rx stream state is {:s} in check_rx_data_sequence_only()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
      return 0;
    }

    size_t n_received = 0;
    data_queue_element q;

    LOG_INFO(fmt::format("checking sequence for {:d} samples", n_desired));

    while(n_received < n_desired) {
      uint32_t wait_time_us = 0;
      // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(rx_data_queue.read_available() == 0 and wait_time_us < timeout_us) {
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
        vxsdr::rx_stream_state = STREAM_ERROR;
        return n_received;
      }
    }
    LOG_DEBUG(fmt::format("check_rx_data_sequence_only complete ({:d} samples)", n_received));
    return n_received;
  }

  size_t vxsdr::put_tx_data(std::vector<std::complex<int16_t> > data, const uint8_t subdev, const double timeout_s) {
    LOG_DEBUG("put_tx_data started");

   if(timeout_s <= 0.0) {
      LOG_ERROR("timeout_s must be positive in check_rx_data_sequence_only()");
      return 0;
    } else if(timeout_s > 3600.0) {
      LOG_ERROR("timeout_s must 3600 or less in check_rx_data_sequence_only()");
      return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);


    if(vxsdr::tx_stream_state != STREAMING and vxsdr::tx_stream_state != WAITING_FOR_START) {
      LOG_ERROR(fmt::format("tx stream state is {:s} in put_tx_data()",
                vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
      return 0;
    }

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

      uint32_t wait_time_us = 0;
      // setting tx_data_queue_wait_us = 0 results in a busy wait with no timeout
      while(tx_data_queue.write_available() == 0 and wait_time_us < timeout_us) {
        if(tx_data_queue_wait_us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(tx_data_queue_wait_us));
          wait_time_us += tx_data_queue_wait_us;
        }
      }

      if(tx_data_queue.push(q)) {
        n_put += n_samples;
      } else {
        LOG_ERROR("timeout pushing to tx data queue");
        vxsdr::tx_stream_state = STREAM_ERROR;
        return n_put;
      }
    }
    LOG_DEBUG(fmt::format("put_tx_data complete ({:d} samples)", n_put));
    return n_put;
  }

  std::optional<std::array<uint32_t, 4>> vxsdr::device_hello(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_HELLO, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_hello()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<hello_rsp_packet *>(&q);
      std::array<uint32_t, 4> res = {r->board_id, r->fpga_fw_version, r->mcu_sw_version, r->serial_no};
      return res;
    }
    return std::nullopt;
  }

  bool vxsdr::device_reset(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_RESET, subdev, flags, sizeof(p), 0 };
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::device_clear_status(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_CLEAR_STATUS, subdev, flags, sizeof(p), 0 };
    return vxsdr::send_packet_and_check_response(p);
  }

  std::optional<std::array<uint32_t, 6>> vxsdr::device_get_status(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_STATUS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_status()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<status_rsp_packet *>(&q);
      std::array<uint32_t, 6> res = {r->fpga_temperature, r->dac_temperature, r->sequence_error_count,
                                      r->data_flow_error_flags, r->ddr_arbiter_status, r->ethernet_status};
      return res;
    }
    return std::nullopt;
  }

  bool vxsdr::device_set_time_now(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev, const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NOW, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
    vxsdr::time_point_to_time_spec_t(t, p.time);
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::device_set_time_next_pps(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev, const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NEXT_PPS, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
    vxsdr::time_point_to_time_spec_t(t, p.time);
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::device_set_time_now(const int64_t sec, const int32_t nsec, const uint8_t subdev, const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NOW, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
    if(sec >= 0 and sec <= UINT32_MAX and nsec >= 0 and nsec < 1'000'000'000) {
      p.time.seconds = sec;
      p.time.nanoseconds = nsec;
      return vxsdr::send_packet_and_check_response(p);
    }
    LOG_ERROR("time out of range in device_set_time_now()");
    return false;
  }

  bool vxsdr::device_set_time_next_pps(const int64_t sec, const int32_t nsec, const uint8_t subdev, const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NEXT_PPS, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
    if(sec >= 0 and sec <= UINT32_MAX and nsec >= 0 and nsec < 1'000'000'000) {
      p.time.seconds = sec;
      p.time.nanoseconds = nsec;
      return vxsdr::send_packet_and_check_response(p);
    }
    LOG_ERROR("time out of range in device_set_time_next_pps()");
    return false;
  }

  bool vxsdr::device_get_time_now(uint32_t &sec, uint32_t &nsec, const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIME, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_time_now()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<cmd_or_rsp_packet_time *>(&q);
      sec = r->time.seconds;
      nsec = r->time.nanoseconds;
      return true;
    }
    return false;
  }

  std::optional<std::chrono::time_point<std::chrono::system_clock>> vxsdr::device_get_time_now(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIME, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_time_now()");
    if(res) {
      auto q = res.value();
      if((q.hdr.flags & FLAGS_TIME_PRESENT) != 0) {
        auto *r = reinterpret_cast<cmd_or_rsp_packet_time *>(&q);
        return std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(r->time.seconds) +
                                                                  std::chrono::nanoseconds(r->time.nanoseconds));
      } else {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  bool vxsdr::rx_start_now_until_stop(const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::rx_stream_state != NO_STREAM) {
      LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_now_until_stop()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
      return false;
    } else {
      header_only_packet p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0 };
      auto sent = vxsdr::send_packet_and_check_response(p);
      if(sent) {
        vxsdr::rx_stream_state = STREAMING;
        return true;
      }
      return false;
    }
  }

  bool vxsdr::tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::tx_stream_state != NO_STREAM) {
      LOG_ERROR(fmt::format("tx stream state is {:s} in tx_start_now_for_n_samples()",
                vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
      return false;
    } else {
      cmd_or_rsp_packet_samples p;
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
      p.n_samples = n;
      auto sent = vxsdr::send_packet_and_check_response(p);
      if(sent) {
        vxsdr::tx_stream_state = STREAMING;
        return true;
      }
      return false;
    }
  }

  bool vxsdr::rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::rx_stream_state != NO_STREAM) {
      LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_now_for_n_samples()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
      return false;
    } else {
      cmd_or_rsp_packet_samples p;
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0 };
      p.n_samples = n;
      auto sent = vxsdr::send_packet_and_check_response(p);
      if(sent) {
        vxsdr::rx_stream_state = STREAMING;
        return true;
      }
      return false;
    }
  }

  bool vxsdr::tx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock> &t, const uint64_t n,
                                             const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::tx_stream_state != NO_STREAM) {
      LOG_ERROR(fmt::format("tx stream state is {:s} in tx_start_at_time_for_n_samples()",
                vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
      return false;
    } else {
      cmd_or_rsp_packet_time_samples p = {};
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES, subdev,
                static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
      vxsdr::time_point_to_time_spec_t(t, p.time);
      p.n_samples = n;
      auto sent = vxsdr::send_packet_and_check_response(p);
      if(sent) {
        vxsdr::tx_stream_state = WAITING_FOR_START;
        return true;
      }
      return false;
    }
  }

  bool vxsdr::rx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock> &t,const uint64_t n,
                                             const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::rx_stream_state != NO_STREAM) {
      LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_at_time_for_n_samples()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
      return false;
    } else {
      cmd_or_rsp_packet_time_samples p = {};
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES, subdev,
                static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
      vxsdr::time_point_to_time_spec_t(t, p.time);
      p.n_samples = n;
      auto sent = vxsdr::send_packet_and_check_response(p);
      if(sent) {
        vxsdr::rx_stream_state = WAITING_FOR_START;
        return true;
      }
      return false;
    }
  }

  bool vxsdr::tx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::tx_stream_state != NO_STREAM) {
      LOG_ERROR(fmt::format("tx stream state is {:s} in tx_start_at_time_until_stop()",
                vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
      return false;
    } else {
      cmd_or_rsp_packet_time p = {};
      p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_AT_TIME_UNTIL_STOP, subdev,
                static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
      vxsdr::time_point_to_time_spec_t(t, p.time);
      auto sent = vxsdr::send_packet_and_check_response(p);
      if(sent) {
        vxsdr::tx_stream_state = WAITING_FOR_START;
        return true;
      }
      return false;
    }
  }

  bool vxsdr::rx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock> &t, const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::rx_stream_state != NO_STREAM) {
      LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_at_time_until_stop()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
      return false;
    } else {
      cmd_or_rsp_packet_time p = {};
      p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_AT_TIME_UNTIL_STOP, subdev,
                static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0 };
      vxsdr::time_point_to_time_spec_t(t, p.time);
      auto sent = vxsdr::send_packet_and_check_response(p);
      if(sent) {
        vxsdr::rx_stream_state = WAITING_FOR_START;
        return true;
      }
      return false;
    }
  }

  bool vxsdr::set_tx_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev, const uint8_t flags) {
    radio_iq_bias_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    p.i_bias = std::min(1.0, std::max(-1.0, i_bias));
    p.q_bias = std::min(1.0, std::max(-1.0, q_bias));
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::set_rx_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev, const uint8_t flags) {
    radio_iq_bias_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    p.i_bias = std::min(1.0, std::max(-1.0, i_bias));
    p.q_bias = std::min(1.0, std::max(-1.0, q_bias));
    return vxsdr::send_packet_and_check_response(p);
  }

  std::optional<std::array<double, 2>> vxsdr::get_tx_iq_bias(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_iq_bias()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_iq_bias_packet *>(&q);
      std::array<double, 2> res = {r->i_bias, r->q_bias};
      return res;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 2>> vxsdr::get_rx_iq_bias(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_iq_bias()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_iq_bias_packet *>(&q);
      std::array<double, 2> res = {r->i_bias, r->q_bias};
      return res;
    }
    return std::nullopt;
  }

  bool vxsdr::set_tx_iq_corr(const double ii_corr, const double iq_corr, const double qi_corr, const double qq_corr,
      const uint8_t subdev, const uint8_t flags) {
    radio_iq_corr_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_IQ_CORR, subdev, flags, sizeof(p), 0 };
    p.ii_corr = ii_corr;
    p.iq_corr = iq_corr;
    p.qi_corr = qi_corr;
    p.qq_corr = qq_corr;
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::set_rx_iq_corr(const double ii_corr, const double iq_corr, const double qi_corr, const double qq_corr,
      const uint8_t subdev, const uint8_t flags) {
    radio_iq_corr_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_IQ_CORR, subdev, flags, sizeof(p), 0 };
    p.ii_corr = ii_corr;
    p.iq_corr = iq_corr;
    p.qi_corr = qi_corr;
    p.qq_corr = qq_corr;
    return vxsdr::send_packet_and_check_response(p);
  }

  std::optional<std::array<double, 4>> vxsdr::get_tx_iq_corr(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_iq_corr()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_iq_corr_packet *>(&q);
      std::array<double, 4> corr = {r->ii_corr, r->iq_corr, r->qi_corr, r->qq_corr};
      return corr;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 4>> vxsdr::get_rx_iq_corr(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_IQ_BIAS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_iq_corr()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_iq_corr_packet *>(&q);
      std::array<double, 4> corr = {r->ii_corr, r->iq_corr, r->qi_corr, r->qq_corr};
      return corr;
    }
    return std::nullopt;
  }

  bool vxsdr::tx_stop_now(const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::tx_stream_state != STREAMING) {
      LOG_WARN(fmt::format("tx stream state is {:s} in tx_stop_now()",
                vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
    }
    // send a stop anyway, even if we think the state is wrong
    header_only_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0 };
    auto sent = vxsdr::send_packet_and_check_response(p);
    if(sent) {
      vxsdr::tx_stream_state = NO_STREAM;
      return true;
    }
    return false;
  }

  bool vxsdr::rx_stop_now(const uint8_t subdev, const uint8_t flags) {
    if(vxsdr::rx_stream_state != STREAMING) {
      LOG_WARN(fmt::format("rx stream state is {:s} in rx_stop_now()",
                vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
    }
    // send a stop anyway, even if we think the state is wrong
    header_only_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0 };
    auto sent = vxsdr::send_packet_and_check_response(p);
    if(sent) {
      vxsdr::rx_stream_state = NO_STREAM;
      return true;
    }
    return false;
  }

  std::optional<std::array<double, 2>> vxsdr::get_tx_freq_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ_RANGE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_freq_range()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<range_packet *>(&q);
      std::array<double, 2> res = {r->range_min, r->range_max};
      return res;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 2>> vxsdr::get_rx_freq_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ_RANGE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_freq_range()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<range_packet *>(&q);
      std::array<double, 2> res = {r->range_min, r->range_max};
      return res;
    }
    return std::nullopt;
  }

  bool vxsdr::set_tx_freq(const double freq_hz, const uint8_t subdev, const uint8_t flags) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    p.freq = freq_hz;
    p.rtol = 1e-9;
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::set_rx_freq(const double freq_hz, const uint8_t subdev, const uint8_t flags) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    p.freq = freq_hz;
    p.rtol = 1e-9;
    return vxsdr::send_packet_and_check_response(p);
  }

  std::optional<double> vxsdr::get_tx_freq(const uint8_t subdev, const uint8_t flags) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_freq()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_rf_freq_packet *>(&q);
      return r->freq;
    }
    return std::nullopt;
  }

  std::optional<double> vxsdr::get_rx_freq(const uint8_t subdev, const uint8_t flags) {
    radio_rf_freq_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_freq()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_rf_freq_packet *>(&q);
      return r->freq;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 2>> vxsdr::get_tx_gain_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_GAIN_RANGE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_gain_range()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<range_packet *>(&q);
      std::array<double, 2> res = {r->range_min, r->range_max};
      return res;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 2>> vxsdr::get_rx_gain_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_GAIN_RANGE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_gain_range()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<range_packet *>(&q);
      std::array<double, 2> res = {r->range_min, r->range_max};
      return res;
    }
    return std::nullopt;
  }

  bool vxsdr::set_tx_gain(const double gain_db, const uint8_t subdev, const uint8_t flags) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    p.gain = gain_db;
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::set_rx_gain(const double gain_db, const uint8_t subdev, const uint8_t flags) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    p.gain = gain_db;
    return vxsdr::send_packet_and_check_response(p);
  }

  std::optional<double> vxsdr::get_tx_gain(const uint8_t subdev, const uint8_t flags) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_gain()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_rf_gain_packet *>(&q);
      return r->gain;
    }
    return std::nullopt;
  }

  std::optional<double> vxsdr::get_rx_gain(const uint8_t subdev, const uint8_t flags) {
    radio_rf_gain_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_gain()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_rf_gain_packet *>(&q);
      return r->gain;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 2>> vxsdr::get_tx_rate_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RATE_RANGE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "tx_get_rate_range()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<range_packet *>(&q);
      std::array<double, 2> res = {r->range_min, r->range_max};
      return res;
    }
    return std::nullopt;
  }

  std::optional<std::array<double, 2>> vxsdr::get_rx_rate_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RATE_RANGE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "rx_get_rate_range()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<range_packet *>(&q);
      std::array<double, 2> res = {r->range_min, r->range_max};
      return res;
    }
    return std::nullopt;
  }

  bool vxsdr::set_tx_rate(const double rate_samples_sec, const uint8_t subdev, const uint8_t flags) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    p.rate = rate_samples_sec;
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::set_rx_rate(const double rate_samples_sec, const uint8_t subdev, const uint8_t flags) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    p.rate = rate_samples_sec;
    return vxsdr::send_packet_and_check_response(p);
  }

  std::optional<double> vxsdr::get_tx_rate(const uint8_t subdev, const uint8_t flags) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_rate()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_sample_rate_packet *>(&q);
      return r->rate;
    }
    return std::nullopt;
  }

  std::optional<double> vxsdr::get_rx_rate(const uint8_t subdev, const uint8_t flags) {
    radio_sample_rate_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_rate()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<radio_sample_rate_packet *>(&q);
      return r->rate;
    }
    return std::nullopt;
  }

  std::optional<unsigned> vxsdr::get_tx_num_ports(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_NUM_RF_PORTS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_num_ports()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<unsigned_packet *>(&q);
      return (unsigned)r->number;
    }
    return std::nullopt;
  }

  std::optional<unsigned> vxsdr::get_rx_num_ports(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_NUM_RF_PORTS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_num_ports()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<unsigned_packet *>(&q);
      return (unsigned)r->number;
    }
    return std::nullopt;
  }

  std::optional<std::string> vxsdr::get_tx_port_name(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_PORT_NAME, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_port_name()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<name_packet *>(&q);
      return std::string(r->name, MAX_PAYLOAD_LENGTH_BYTES - 1);
    }
    return std::nullopt;
  }

  std::optional<std::string> vxsdr::get_rx_port_name(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_PORT_NAME, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_port_name()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<name_packet *>(&q);
      return std::string(r->name, MAX_PAYLOAD_LENGTH_BYTES - 1);
    }
    return std::nullopt;
  }

  bool vxsdr::set_tx_port(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, subdev, flags, sizeof(p), 0 };
    p.number = port_num;
    if(not vxsdr::send_packet_and_check_response(p)) {
      return false;
    }
    return true;
  }

  bool vxsdr::set_tx_port(const std::string &port_name, const uint8_t subdev, const uint8_t flags) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, subdev, flags, sizeof(p), 0 };
    //FIXME needs implementation and checking!!!
    p.number = 0;
    if(not vxsdr::send_packet_and_check_response(p)) {
      return false;
    }
    return true;
  }

  bool vxsdr::set_rx_port(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, subdev, flags, sizeof(p), 0 };
    p.number = port_num;
    if(not vxsdr::send_packet_and_check_response(p)) {
      return false;
    }
    return true;
  }

  bool vxsdr::set_rx_port(const std::string &port_name, const uint8_t subdev, const uint8_t flags) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, subdev, flags, sizeof(p), 0 };
    //FIXME needs implementation and checking!!!
    p.number = 0;
    if(not vxsdr::send_packet_and_check_response(p)) {
      return false;
    }
    return true;
  }

  std::optional<unsigned> vxsdr::get_tx_port(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_PORT_NAME, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_port()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<unsigned_packet *>(&q);
      return (unsigned)r->number;
    }
    return std::nullopt;
  }

  std::optional<unsigned> vxsdr::get_rx_port(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_PORT_NAME, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_port()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<unsigned_packet *>(&q);
      return (unsigned)r->number;
    }
    return std::nullopt;
  }

  bool vxsdr::set_tx_enable(const bool enabled, const uint8_t subdev, const uint8_t flags) {
    radio_rf_enable_packet p;
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
    if(enabled) {
      p.enabled = 1;
    } else {
      p.enabled = 0;
    }
    return vxsdr::send_packet_and_check_response(p);
  }

  bool vxsdr::set_rx_enable(const bool enabled, const uint8_t subdev, const uint8_t flags) {
    radio_rf_enable_packet p;
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0 };
    if(enabled) {
      p.enabled = 1;
    } else {
      p.enabled = 0;
    }
    return vxsdr::send_packet_and_check_response(p);
  }

  std::optional<capabilities_t> vxsdr::get_tx_capabilities(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_CAPABILITIES, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_capabilities()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<capabilities_packet *>(&q);
      return r->capabilities;
    }
    return std::nullopt;
  }

  std::optional<capabilities_t> vxsdr::get_rx_capabilities(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_CAPABILITIES, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_capabilities()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<capabilities_packet *>(&q);
      return r->capabilities;
    }
    return std::nullopt;
  }

  std::optional<std::array<bool, 3>> vxsdr::device_get_timing_status(const uint8_t subdev, const uint8_t flags) {
    // FIXME: these need to be defined as part of a generic API when stable
    constexpr uint32_t TIMING_STATUS_EXT_PPS_LOCK   = 0x00000001;
    constexpr uint32_t TIMING_STATUS_EXT_10MHZ_LOCK = 0x00000002;
    constexpr uint32_t TIMING_STATUS_REF_OSC_LOCK   = 0x00000004;
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIMING_STATUS, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_timing_status()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<unsigned_packet *>(&q);
      std::array<bool, 3> ret = { (bool)(r->number & TIMING_STATUS_EXT_PPS_LOCK),
                                  (bool)(r->number & TIMING_STATUS_EXT_10MHZ_LOCK),
                                  (bool)(r->number & TIMING_STATUS_REF_OSC_LOCK) };
      return ret;
    }
    return std::nullopt;
  }

  bool vxsdr::set_host_device_command_timeout(const double timeout_s) {
    if(timeout_s <= 0.0 or timeout_s > 3600.0) {
      return false;
    }
    device_response_timeout_ms = std::llround(1e3 * timeout_s);
    return true;
  }

  double vxsdr::get_host_device_command_timeout() {
    return 1e-3 * device_response_timeout_ms;
  }

  std::optional<std::vector<net::ip::address_v4>> vxsdr::device_discover_ipv4_addresses(const net::ip::address_v4 broadcast_addr)
  {
    // FIXME: needs implementation
    return std::nullopt;
  }

  bool vxsdr::device_set_ipv4_address(const net::ip::address_v4 device_address) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TRANSPORT_ADDR, 0, 0, sizeof(p), 0 };
    p.number = device_address.to_uint();
    if(not vxsdr::send_packet_and_check_response(p)) {
      return false;
    }
    return true;
  }

  bool vxsdr::device_save_ipv4_address(const net::ip::address_v4 device_address) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TRANSPORT_ADDR, 0, 0, sizeof(p), 0 };
    p.number = device_address.to_uint();
    if(not vxsdr::send_packet_and_check_response(p)) {
      return false;
    }
    return true;
  }

  std::optional<unsigned> vxsdr::device_get_max_payload_bytes() {
    header_only_packet p = {};
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_MAX_PAYLOAD, 0, 0, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_max_payload_bytes()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<unsigned_packet *>(&q);
      return (unsigned)r->number;
    }
    return std::nullopt;
  }

  bool vxsdr::device_set_max_payload_bytes(const unsigned max_payload_bytes) {
    unsigned_packet p = {};
    p.hdr = { PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, 0, 0, sizeof(p), 0 };
    p.number = max_payload_bytes;
    if(not vxsdr::send_packet_and_check_response(p)) {
      return false;
    }
    return true;
  }

// private functions

  bool vxsdr::send_packet_and_check_response(packet &p, const std::string cmd_name) {
    if(not vxsdr::cmd_queue_push_check(p)) {
      return false;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < vxsdr::device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(vxsdr::device_response_wait_ms));
      wait_time_ms += vxsdr::device_response_wait_ms;
    }
    if(got_response) {
      if(((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD   and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP) or
          (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_RSP) or
          (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_RSP))
         and q.hdr.command == p.hdr.command) {
        return true;
      }
      if(((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD   and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_ERR) or
          (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_ERR) or
          (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_ERR))
         and q.hdr.command == p.hdr.command) {
        LOG_ERROR(fmt::format("command error in {:s}: {:s}", cmd_name, error_to_string(reinterpret_cast<error_packet *>(&q)->reason)));
        return false;
      }
      LOG_ERROR(fmt::format("invalid response in {:s}", cmd_name));
      return false;
    }
    LOG_ERROR(fmt::format("timeout in {:s}", cmd_name));
    return false;
  }

  std::optional<vxsdr::response_queue_element> vxsdr::send_packet_and_return_response(packet &p, const std::string cmd_name) {
    if(not vxsdr::cmd_queue_push_check(p)) {
      return std::nullopt;
    }
    response_queue_element q;
    bool got_response = false;
    unsigned wait_time_ms = 0;
    while(not (got_response = response_queue.pop(q)) and wait_time_ms < vxsdr::device_response_timeout_ms) {
      std::this_thread::sleep_for(std::chrono::milliseconds(vxsdr::device_response_wait_ms));
      wait_time_ms += vxsdr::device_response_wait_ms;
    }
    if(got_response) {
      if(((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD   and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP) or
          (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_RSP) or
          (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_RSP))
         and q.hdr.command == p.hdr.command) {
        return q;
      }
      if(((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD   and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_ERR) or
          (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_ERR) or
          (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_ERR))
         and q.hdr.command == p.hdr.command) {
        LOG_ERROR(fmt::format("command error in {:s}: {:s}", cmd_name, error_to_string(reinterpret_cast<error_packet *>(&q)->reason)));
        return std::nullopt;
      }
      LOG_ERROR(fmt::format("invalid response in {:s}", cmd_name));
      return std::nullopt;
    }
    LOG_ERROR(fmt::format("timeout in {:s}", cmd_name));
    return std::nullopt;
  }

  void vxsdr::time_point_to_time_spec_t(const std::chrono::time_point<std::chrono::system_clock> &t, time_spec_t &ts) {
    auto secs = std::chrono::time_point_cast<std::chrono::seconds>(t);
    auto nsecs = std::chrono::time_point_cast<std::chrono::nanoseconds>(t) - std::chrono::time_point_cast<std::chrono::nanoseconds>(secs);
    ts.seconds = (uint32_t)secs.time_since_epoch().count();
    ts.nanoseconds = (uint32_t)nsecs.count();
  }

  std::string vxsdr::stream_state_to_string(const stream_state_t state) {
    switch(state) {
      case NO_STREAM:
        return "NO_STREAM";
      case WAITING_FOR_START:
        return "WAITING_FOR_START";
      case STREAMING:
        return "STREAMING";
      case CLOSING:
        return "CLOSING";
      case STREAM_ERROR:
        return "STREAM_ERROR";
      default:
        return "UNKNOWN";
    }
  }

  std::string vxsdr::transport_state_to_string(const transport_state_t state) {
    switch(state) {
      case UNINITIALIZED:
        return "UNINITIALIZED";
      case STARTING:
        return "STARTING";
      case RUNNING:
        return "RUNNING";
      case SHUTDOWN:
        return "SHUTDOWN";
      case TRANSPORT_ERROR:
        return "TRANSPORT_ERROR";
      default:
        return "UNKNOWN";
    }
  }

  std::string vxsdr::error_to_string(const uint32_t reason) {
    switch(reason) {
      case ERR_BAD_COMMAND:
        return "BAD_COMMAND";
      case ERR_BUSY:
        return "BUSY";
      case ERR_NO_SUCH_SUBDEVICE:
        return "NO_SUCH_SUBDEVICE";
      case ERR_TIMEOUT:
        return "TIMEOUT";
      case ERR_BAD_PARAMETER:
        return "BAD_PARAMETER";
      case ERR_NOT_SUPPORTED:
        return "NOT_SUPPORTED";
      case ERR_BAD_PKT_SIZE:
        return "BAD_PKT_SIZE";
      default:
        return fmt::format("error {:d}", reason);
    }
  }

  void vxsdr::async_handler() {
    LOG_DEBUG("async_handler started");
    while(udp_rx_state != SHUTDOWN) {
      async_msg_queue_element a;
      while(async_msg_queue.pop(a)) {
        log_packet_info(a);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(async_queue_check_ms));
    }
    LOG_DEBUG("async_handler finished");
  }

  std::optional<std::array<uint32_t, 2>> vxsdr::device_get_buffer_info(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_INFO, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_buffer_info()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<buffer_info_rsp_packet *>(&q);
      std::array<uint32_t, 2> ret = { r->rx_buffer_size_bytes, r->tx_buffer_size_bytes };
      return ret;
    }
    return std::nullopt;
  }

  std::optional<std::array<uint32_t, 2>> vxsdr::device_get_buffer_use(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = { PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_USE, subdev, flags, sizeof(p), 0 };
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_buffer_use()");
    if(res) {
      auto q = res.value();
      auto *r = reinterpret_cast<buffer_use_rsp_packet *>(&q);
      std::array<uint32_t, 2> ret =  { r->rx_buffer_used_bytes, r->tx_buffer_used_bytes };
      return ret;
    }
    return std::nullopt;
  }

  [[nodiscard]] bool vxsdr::cmd_queue_push_check(packet &p) {
    command_queue_element q;
    std::memcpy(&q, &p, std::min((size_t)p.hdr.packet_size, sizeof(q)));
     if(not command_queue.bounded_push(q)) {
      LOG_ERROR("out of space pushing to command queue");
      log_packet_info(p);
      return false;
    }
    if((q.hdr.flags & FLAGS_REQUEST_ACK) != 0) {
      cmd_ack_queue_element a;
      bool got_response = false;
      unsigned wait_time_ms = 0;
      while(not (got_response = vxsdr::cmd_ack_queue.pop(a)) and wait_time_ms < device_response_timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(device_response_wait_ms));
        wait_time_ms += device_response_wait_ms;
      }
      if(got_response) {
        auto *r = reinterpret_cast<header_only_packet *>(&a);
        bool packet_types_match = false;
        if((q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD   and r->hdr.packet_type == PACKET_TYPE_DEVICE_CMD_ACK)   or
           (q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and r->hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_ACK) or
           (q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and r->hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_ACK)) {
          packet_types_match = true;
        }
        // for an ack to match, packet types, commands, and subdevices must all match
        if(packet_types_match and r->hdr.command == q.hdr.command and r->hdr.target_subdevice == q.hdr.target_subdevice) {
          return true;
        }
        LOG_ERROR("invalid ack received");
        return false;
      }
      LOG_ERROR("timeout waiting for ack");
      return false;
    }
    return true;
  }