#include "vxsdr.hpp"

/*! @file vxsdr.cpp
    @brief Function definitions for the @p vxsdr class.
*/

vxsdr::vxsdr(const net::ip::udp::endpoint& local_send_endpoint,
             const net::ip::udp::endpoint& local_receive_endpoint,
             const net::ip::udp::endpoint& device_endpoint,
             const bool nondata_packet_logging,
             const bool set_affinity,
             const unsigned base_cpu_number,
             const bool set_priority,
             const int base_priority)
    : sender_socket(context, local_send_endpoint), receiver_socket(context, local_receive_endpoint) {
    LOG_DEBUG("vxsdr constructor entered");
    device_response_timeout = 1s;
    log_nondata_packets     = nondata_packet_logging;
    thread_set_affinity     = set_affinity;
    send_thread_cpu         = base_cpu_number;
    receive_thread_cpu      = base_cpu_number + 1;
    thread_set_priority     = set_priority;
    send_thread_priority    = base_priority;
    receive_thread_priority = base_priority;
    destination_endpoint    = device_endpoint;
#ifdef VXSDR_ASYNC_RX
    // https://www.boost.org/doc/libs/1_77_0/doc/html/boost_asio/example/cpp11/futures/daytime_client.cpp
    // We run the io_context in its own thread so that it operates
    // asynchronously with respect to the rest of the program.
    //
    // net::execution needs Boost 1.76 or higher; use make_work_guard for backwards compatibility
    // auto work = net::require(context.get_executor(), net::execution::outstanding_work.tracked);
    auto work      = net::make_work_guard(context);
    context_thread = std::thread([&] { context.run(); });
#endif  // VXSDR_ASYNC_RX
    async_handler_thread        = std::thread([&] { async_handler(); });
    udp_rx_state                = TRANSPORT_STARTING;
    receiver_thread             = std::thread([&] { receiver(); });
    udp_tx_state                = TRANSPORT_STARTING;
    sender_thread               = std::thread([&] { sender(); });
    auto start_time = std::chrono::steady_clock::now();
    while (udp_tx_state != TRANSPORT_RUNNING or udp_rx_state != TRANSPORT_RUNNING) {
        std::this_thread::sleep_for(udp_ready_wait);
        if ((std::chrono::steady_clock::now() - start_time) > udp_ready_timeout) {
            LOG_ERROR(fmt::format("timeout waiting for UDP transport: tx = {:s} rx ={:s}",
                                  vxsdr::transport_state_to_string(udp_tx_state), vxsdr::transport_state_to_string(udp_rx_state)));
            throw std::runtime_error("timeout waiting for UDP transport in vxsdr constructor");
        }
    }
    if (not tx_stop_now() or not rx_stop_now() or
        not device_clear_status() or not device_get_buffer_info()
        or not set_tx_enable(true) or not set_rx_enable(true)) {
        LOG_ERROR("error initializing sdr");
        throw std::runtime_error("error initializing sdr in vxsdr constructor");
    }
    LOG_DEBUG("vxsdr constructor complete");
}

vxsdr::~vxsdr() {
    LOG_DEBUG("vxsdr destructor entered");
    tx_stop_now();
    rx_stop_now();
    set_tx_enable(false);
    set_rx_enable(false);
    receiver_thread_stop_flag = true;
    boost::system::error_code err;
#ifndef VXSDR_ASYNC_RX
    // use shutdown() to terminate the blocking read
    receiver_socket.shutdown(net::ip::udp::socket::shutdown_receive, err);
    if (err != boost::system::errc::success and err != boost::system::errc::not_connected) {
        // the not connected error is expected since it's a UDP socket
        LOG_ERROR(fmt::format("receiver socket shutdown: {:s}", err.message()));
    }
#endif  // VXSDR_ASYNC_RX
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }
    udp_rx_state = TRANSPORT_SHUTDOWN;
    receiver_socket.close(err);
    if (err != boost::system::errc::success) {
        LOG_ERROR(fmt::format("receiver socket close: {:s}", err.message()));
    }
    LOG_DEBUG("joining sender thread");
    sender_thread_stop_flag = true;
    if (sender_thread.joinable()) {
        sender_thread.join();
    }
    udp_tx_state = TRANSPORT_SHUTDOWN;
    sender_socket.close(err);
    if (err != boost::system::errc::success) {
        LOG_ERROR(fmt::format("sender socket close: {:s}", err.message()));
    }
#ifdef VXSDR_ASYNC_RX
    LOG_DEBUG("joining context thread");
    context_thread.join();
#endif  // VXSDR_ASYNC_RX
    LOG_DEBUG("joining error_handler thread");
    if (async_handler_thread.joinable()) {
        async_handler_thread.join();
    }
    LOG_DEBUG("vxsdr destructor complete");
}

size_t vxsdr::get_rx_data(std::vector<std::complex<int16_t>>& data, size_t n_desired, const double timeout_s) {
    LOG_DEBUG("get_rx_data entered");
    size_t n_received = 0;
    data_queue_element q;

    if (timeout_s <= 0.0) {
        LOG_ERROR("timeout_s must be positive in get_rx_data()");
        return 0;
    }
    if (timeout_s > 3600.0) {
        LOG_ERROR("timeout_s must 3600 or less in get_rx_data()");
        return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);

    if (vxsdr::rx_stream_state != STREAM_RUNNING and vxsdr::rx_stream_state != STREAM_WAITING_FOR_START) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in get_rx_data()", vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
        return 0;
    }

    if (n_desired == 0) {
        n_desired = data.size();
    } else {
        data.reserve(n_desired);
    }

    data.clear();

    LOG_INFO(fmt::format("receiving {:d} samples", n_desired));

    while (n_received < n_desired) {
        uint32_t wait_time_us = 0;
        // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
        while (rx_data_queue.read_available() == 0 and wait_time_us < timeout_us) {
            if (rx_data_queue_wait_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
                wait_time_us += rx_data_queue_wait_us;
            }
        }
        if (rx_data_queue.pop(q)) {
            bool has_time      = (bool)(q.hdr.flags & FLAGS_TIME_PRESENT);
            bool has_stream_id = (bool)(q.hdr.flags & FLAGS_STREAM_ID_PRESENT);
            if (not has_time and not has_stream_id) {
                auto* p             = reinterpret_cast<data_packet*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header);
                size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
                std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(data));
                n_received += data_samples;
            } else if (has_time and not has_stream_id) {
                auto* p             = reinterpret_cast<data_packet_time*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t);
                size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
                std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(data));
                n_received += data_samples;
            } else if (has_stream_id and not has_time) {
                auto* p             = reinterpret_cast<data_packet_stream*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header) - sizeof(stream_spec_t);
                size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
                std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(data));
                n_received += data_samples;
            } else {
                auto* p             = reinterpret_cast<data_packet_time_stream*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t) - sizeof(stream_spec_t);
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

size_t vxsdr::write_rx_data(std::ofstream& out_stream, const size_t n_desired, const double timeout_s, size_t write_size_samples) {
    LOG_DEBUG("write_rx_data entered");

    if (timeout_s <= 0.0) {
        LOG_ERROR("timeout_s must be positive in write_rx_data()");
        return 0;
    }
    if (timeout_s > 3600.0) {
        LOG_ERROR("timeout_s must 3600 or less in write_rx_data()");
        return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);

    if (vxsdr::rx_stream_state != STREAM_RUNNING and vxsdr::rx_stream_state != STREAM_WAITING_FOR_START) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in write_rx_data()", vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
        return 0;
    }

    size_t n_received = 0, n_written = 0;
    data_queue_element q;

    if (write_size_samples < MAX_DATA_LENGTH_SAMPLES) {
        LOG_WARN(fmt::format("requested buffered write size too small; increasing to {:d} samples", MAX_DATA_LENGTH_SAMPLES));
        write_size_samples = MAX_DATA_LENGTH_SAMPLES;
    }

    std::vector<std::complex<int16_t>> buffer;
    buffer.reserve(2 * write_size_samples);

    LOG_INFO(fmt::format("receiving {:d} samples", n_desired));

    while (n_written < n_desired) {
        uint32_t wait_time_us = 0;
        // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
        while (rx_data_queue.read_available() == 0 and wait_time_us < timeout_us) {
            if (rx_data_queue_wait_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
                wait_time_us += rx_data_queue_wait_us;
            }
        }
        if (rx_data_queue.pop(q)) {
            bool has_time      = (bool)(q.hdr.flags & FLAGS_TIME_PRESENT);
            bool has_stream_id = (bool)(q.hdr.flags & FLAGS_STREAM_ID_PRESENT);
            if (not has_time and not has_stream_id) {
                auto* p             = reinterpret_cast<data_packet*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header);
                size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
                std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
                n_received += data_samples;
            } else if (has_time and not has_stream_id) {
                auto* p             = reinterpret_cast<data_packet_time*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t);
                size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
                std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
                n_received += data_samples;
            } else if (has_stream_id and not has_time) {
                auto* p             = reinterpret_cast<data_packet_stream*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header) - sizeof(stream_spec_t);
                size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
                std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
                n_received += data_samples;
            } else {
                auto* p             = reinterpret_cast<data_packet_time_stream*>(&q);
                size_t data_bytes   = p->hdr.packet_size - sizeof(packet_header) - sizeof(time_spec_t) - sizeof(stream_spec_t);
                size_t data_samples = data_bytes / sizeof(std::complex<int16_t>);
                std::copy_n(std::begin(p->data), std::min(n_desired - n_received, data_samples), std::back_inserter(buffer));
                n_received += data_samples;
            }
            if (buffer.size() >= write_size_samples or n_received >= n_desired) {
                size_t n_to_write = std::min(n_desired - n_written, write_size_samples);
                // disable clang-tidy warning for this line because outstream.write requires a char *
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
                out_stream.write((char*)buffer.data(), sizeof(std::complex<int16_t>) * n_to_write);
                if (out_stream.fail()) {
                    LOG_ERROR(fmt::format("error writing to output file ({:d} of {:d} samples)", n_received, n_desired));
                    break;
                }
                n_written += n_to_write;
                // copy anything that hasn't been written to the front of the buffer
                size_t n_to_copy = std::min(n_desired - n_written, buffer.size() - n_to_write);
                std::copy_n(buffer.begin() + n_to_write, n_to_copy, buffer.begin());
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

    if (timeout_s <= 0.0) {
        LOG_ERROR("timeout_s must be positive in check_rx_data_sequence_only()");
        return 0;
    }
    if (timeout_s > 3600.0) {
        LOG_ERROR("timeout_s must 3600 or less in check_rx_data_sequence_only()");
        return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);

    if (vxsdr::rx_stream_state != STREAM_RUNNING and vxsdr::rx_stream_state != STREAM_WAITING_FOR_START) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in check_rx_data_sequence_only()",
                              vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
        return 0;
    }

    size_t n_received = 0;
    data_queue_element q;

    LOG_INFO(fmt::format("checking sequence for {:d} samples", n_desired));

    while (n_received < n_desired) {
        uint32_t wait_time_us = 0;
        // setting rx_data_queue_wait_us = 0 results in a busy wait with no timeout
        while (rx_data_queue.read_available() == 0 and wait_time_us < timeout_us) {
            if (rx_data_queue_wait_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(rx_data_queue_wait_us));
                wait_time_us += rx_data_queue_wait_us;
            }
        }
        if (rx_data_queue.pop(q)) {
            auto* p           = reinterpret_cast<packet_header*>(&q);
            size_t data_bytes = p->packet_size - sizeof(packet_header);
            if ((bool)(p->flags & FLAGS_TIME_PRESENT)) {
                data_bytes -= sizeof(time_spec_t);
            }
            if ((bool)(p->flags & FLAGS_STREAM_ID_PRESENT)) {
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

size_t vxsdr::put_tx_data(std::vector<std::complex<int16_t>> data, const uint8_t subdev, const double timeout_s) {
    LOG_DEBUG("put_tx_data started");

    if (timeout_s <= 0.0) {
        LOG_ERROR("timeout_s must be positive in put_tx_data()");
        return 0;
    }
    if (timeout_s > 3600.0) {
        LOG_ERROR("timeout_s must 3600 or less in put_tx_data()");
        return 0;
    }
    uint32_t timeout_us = std::llround(1'000'000 * timeout_s);

    if (vxsdr::tx_stream_state != STREAM_RUNNING and vxsdr::tx_stream_state != STREAM_WAITING_FOR_START) {
        LOG_ERROR(fmt::format("tx stream state is {:s} in put_tx_data()", vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
        return 0;
    }

    // puts plain data_packets (no time, no stream)
    size_t n_put = 0;
    for (size_t i = 0; i < data.size(); i += MAX_DATA_LENGTH_SAMPLES) {
        data_queue_element q;
        auto* p               = reinterpret_cast<data_packet*>(&q);
        unsigned n_samples    = std::min(MAX_DATA_LENGTH_SAMPLES, data.size() - i);
        unsigned n_data_bytes = n_samples * sizeof(std::complex<int16_t>);
        uint16_t packet_size  = sizeof(packet_header) + n_data_bytes;
        p->hdr                = {PACKET_TYPE_TX_SIGNAL_DATA, 0, subdev, 0, packet_size, 0};
        for (unsigned j = 0; j < n_samples; j++) {
            p->data[j] = data[i + j];
        }

        uint32_t wait_time_us = 0;
        // setting tx_data_queue_wait_us = 0 results in a busy wait with no timeout
        while (tx_data_queue.write_available() == 0 and wait_time_us < timeout_us) {
            if (tx_data_queue_wait_us > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(tx_data_queue_wait_us));
                wait_time_us += tx_data_queue_wait_us;
            }
        }

        if (tx_data_queue.push(q)) {
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
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_HELLO, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_hello()");
    if (res) {
        auto q                      = res.value();
        auto* r                     = reinterpret_cast<four_uint32_packet*>(&q);
        std::array<uint32_t, 4> res = {r->value1, r->value2, r->value3, r->value4};
        return res;
    }
    return std::nullopt;
}

bool vxsdr::device_reset(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_RESET, subdev, flags, sizeof(p), 0};
    return vxsdr::send_packet_and_check_response(p, "device_reset()");
}

bool vxsdr::device_clear_status(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_CLEAR_STATUS, subdev, flags, sizeof(p), 0};
    return vxsdr::send_packet_and_check_response(p, "device_clear_status()");
}

std::optional<std::array<uint32_t, 8>> vxsdr::device_get_status(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_STATUS, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_status()");
    if (res) {
        auto q                      = res.value();
        auto* r                     = reinterpret_cast<eight_uint32_packet*>(&q);
        std::array<uint32_t, 8> res = {r->value1, r->value2, r->value3, r->value4, r->value5, r->value6, r->value7, r->value8};
        return res;
    }
    return std::nullopt;
}

bool vxsdr::device_set_time_now(const std::chrono::time_point<std::chrono::system_clock>& t,
                                const uint8_t subdev,
                                const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NOW, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    vxsdr::time_point_to_time_spec_t(t, p.time);
    return vxsdr::send_packet_and_check_response(p, "device_set_time_now()");
}

bool vxsdr::device_set_time_next_pps(const std::chrono::time_point<std::chrono::system_clock>& t,
                                     const uint8_t subdev,
                                     const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NEXT_PPS, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    vxsdr::time_point_to_time_spec_t(t, p.time);
    return vxsdr::send_packet_and_check_response(p, "device_set_time_next_pps()");
}

bool vxsdr::device_set_time_now(const int64_t sec, const int32_t nsec, const uint8_t subdev, const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NOW, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    if (sec >= 0 and sec <= UINT32_MAX and nsec >= 0 and nsec < 1'000'000'000) {
        p.time.seconds     = sec;
        p.time.nanoseconds = nsec;
        return vxsdr::send_packet_and_check_response(p, "device_set_time_now()");
    }
    LOG_ERROR("time out of range in device_set_time_now()");
    return false;
}

bool vxsdr::device_set_time_next_pps(const int64_t sec, const int32_t nsec, const uint8_t subdev, const uint8_t flags) {
    cmd_or_rsp_packet_time p = {};
    p.hdr = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TIME_NEXT_PPS, subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    if (sec >= 0 and sec <= UINT32_MAX and nsec >= 0 and nsec < 1'000'000'000) {
        p.time.seconds     = sec;
        p.time.nanoseconds = nsec;
        return vxsdr::send_packet_and_check_response(p, "device_set_time_next_pps()");
    }
    LOG_ERROR("time out of range in device_set_time_next_pps()");
    return false;
}

bool vxsdr::device_get_time_now(uint32_t& sec, uint32_t& nsec, const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIME, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_time_now()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<cmd_or_rsp_packet_time*>(&q);
        sec     = r->time.seconds;
        nsec    = r->time.nanoseconds;
        return true;
    }
    return false;
}

std::optional<std::chrono::time_point<std::chrono::system_clock>> vxsdr::device_get_time_now(const uint8_t subdev,
                                                                                             const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIME, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_time_now()");
    if (res) {
        auto q = res.value();
        if ((q.hdr.flags & FLAGS_TIME_PRESENT) != 0) {
            auto* r = reinterpret_cast<cmd_or_rsp_packet_time*>(&q);
            return std::chrono::time_point<std::chrono::system_clock>(std::chrono::seconds(r->time.seconds) +
                                                                      std::chrono::nanoseconds(r->time.nanoseconds));
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<vxsdr::stream_state_t> vxsdr::get_tx_stream_state(const uint8_t subdev, const uint8_t flags) {
    // FIXME: temporary implementation using device_get_status
    auto res = vxsdr::device_get_status(subdev, flags);
    if(res) {
        uint32_t stat = res->at(6) >> 16U;
        bool running_flag = (stat & 0x01U) != 0;
        bool waiting_flag = (stat & 0x02U) != 0;
        if (not running_flag and not waiting_flag) {
            vxsdr::tx_stream_state = STREAM_STOPPED;
        } else if (not running_flag and waiting_flag) {
            vxsdr::tx_stream_state = STREAM_WAITING_FOR_START;
        }
        else if (running_flag and not waiting_flag) {
            vxsdr::tx_stream_state = STREAM_RUNNING;
        } else {
            vxsdr::tx_stream_state = STREAM_ERROR;
        }
        return vxsdr::tx_stream_state;
    }
    return std::nullopt;
}

std::optional<vxsdr::stream_state_t> vxsdr::get_rx_stream_state(const uint8_t subdev, const uint8_t flags) {
    // FIXME: temporary implementation using device_get_status
    auto res = vxsdr::device_get_status(subdev, flags);
    if(res) {
        uint32_t stat = res->at(6) & 0xFFFFU;
        bool running_flag = (stat & 0x01U) != 0;
        bool waiting_flag = (stat & 0x02U) != 0;
        if (not running_flag and not waiting_flag) {
            vxsdr::rx_stream_state = STREAM_STOPPED;
        } else if (not running_flag and waiting_flag) {
            vxsdr::rx_stream_state = STREAM_WAITING_FOR_START;
        }
        else if (running_flag and not waiting_flag) {
            vxsdr::rx_stream_state = STREAM_RUNNING;
        } else {
            vxsdr::rx_stream_state = STREAM_ERROR;
        }
        return vxsdr::rx_stream_state;
    }
    return std::nullopt;
}

bool vxsdr::tx_start_now_until_stop(const uint8_t subdev, const uint8_t flags) {
    auto res = vxsdr::get_tx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get tx stream state in tx_start_now_until_stop()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("tx stream state is {:s} in tx_start_now_until_stop()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_tx_stream(0);
    header_only_packet p;
    p.hdr        = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0};
    vxsdr::tx_stream_state = STREAM_RUNNING;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "tx_start_now_until_stop()");
    if (resp_ok) {
        return true;
    }
    vxsdr::tx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::rx_start_now_until_stop(const uint8_t subdev, const uint8_t flags) {
    auto res = vxsdr::get_rx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get rx stream state in rx_start_now_until_stop()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_now_until_stop()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_rx_stream(0);
    header_only_packet p;
    p.hdr        = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_UNTIL_STOP, subdev, flags, sizeof(p), 0};
    vxsdr::rx_stream_state = STREAM_RUNNING;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "rx_start_now_until_stop()");
    if (resp_ok) {
        return true;
    }
    vxsdr::rx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::tx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev, const uint8_t flags) {
    auto res = vxsdr::get_tx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get tx stream state in tx_start_now_for_n_samples()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("tx stream state is {:s} in tx_start_now_for_n_samples()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_tx_stream(n);
    cmd_or_rsp_packet_samples p{};
    p.hdr        = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0};
    p.n_samples  = n;
    vxsdr::tx_stream_state = STREAM_RUNNING;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "tx_start_now_for_n_samples()");
    if (resp_ok) {
        return true;
    }
    vxsdr::tx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::rx_start_now_for_n_samples(const uint64_t n, const uint8_t subdev, const uint8_t flags) {
    auto res = vxsdr::get_rx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get rx stream state in rx_start_now_for_n_samples()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_now_for_n_samples()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_rx_stream(n);
    cmd_or_rsp_packet_samples p{};
    p.hdr        = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_NOW_FOR_N_SAMPLES, subdev, flags, sizeof(p), 0};
    p.n_samples  = n;
    vxsdr::rx_stream_state = STREAM_RUNNING;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "rx_start_now_for_n_samples()");
    if (resp_ok) {
        return true;
    }
    vxsdr::rx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::tx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock>& t,
                                           const uint64_t n,
                                           const uint8_t subdev,
                                           const uint8_t flags) {
    auto res = vxsdr::get_tx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get tx stream state in tx_start_at_time_for_n_samples()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("tx stream state is {:s} in tx_start_at_time_for_n_samples()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_tx_stream(n);
    cmd_or_rsp_packet_time_samples p{};
    p.hdr = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES,
             subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    vxsdr::time_point_to_time_spec_t(t, p.time);
    p.n_samples  = n;
    vxsdr::tx_stream_state = STREAM_WAITING_FOR_START;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "tx_start_at_time_for_n_samples()");
    if (resp_ok) {
        return true;
    }
    vxsdr::tx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::rx_start_at_time_for_n_samples(const std::chrono::time_point<std::chrono::system_clock>& t,
                                           const uint64_t n,
                                           const uint8_t subdev,
                                           const uint8_t flags) {
    auto res = vxsdr::get_rx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get rx stream state in rx_start_at_time_for_n_samples()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_at_time_for_n_samples()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_rx_stream(n);
    cmd_or_rsp_packet_time_samples p{};
    p.hdr = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES,
             subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    vxsdr::time_point_to_time_spec_t(t, p.time);
    p.n_samples  = n;
    vxsdr::rx_stream_state = STREAM_WAITING_FOR_START;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "rx_start_at_time_for_n_samples()");
    if (resp_ok) {
        return true;
    }
    vxsdr::rx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::tx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock>& t,
                                        const uint8_t subdev,
                                        const uint8_t flags) {
    auto res = vxsdr::get_tx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get tx stream state in tx_start_at_time_until_stop()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("tx stream state is {:s} in tx_start_at_time_until_stop()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_tx_stream(0);
    cmd_or_rsp_packet_time p{};
    p.hdr = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_START_AT_TIME_UNTIL_STOP,
             subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    vxsdr::time_point_to_time_spec_t(t, p.time);
    vxsdr::tx_stream_state = STREAM_WAITING_FOR_START;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "tx_start_at_time_until_stop()");
    if (resp_ok) {
        return true;
    }
    vxsdr::tx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::rx_start_at_time_until_stop(const std::chrono::time_point<std::chrono::system_clock>& t,
                                        const uint8_t subdev,
                                        const uint8_t flags) {
    auto res = vxsdr::get_rx_stream_state(subdev, flags);
    if (not res) {
        LOG_ERROR("unable to get rx stream state in rx_start_at_time_until_stop()");
        return false;
    }
    if (res.value() != STREAM_STOPPED) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in rx_start_at_time_until_stop()",
                              vxsdr::stream_state_to_string(res.value())));
        return false;
    }
    vxsdr::reset_rx_stream(0);
    cmd_or_rsp_packet_time p{};
    p.hdr = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_START_AT_TIME_UNTIL_STOP,
             subdev, static_cast<uint8_t>(flags | FLAGS_TIME_PRESENT), sizeof(p), 0};
    vxsdr::time_point_to_time_spec_t(t, p.time);
    vxsdr::rx_stream_state = STREAM_WAITING_FOR_START;
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "rx_start_at_time_until_stop()");
    if (resp_ok) {
        return true;
    }
    vxsdr::rx_stream_state = STREAM_ERROR;
    return false;
}

bool vxsdr::tx_stop_now(const uint8_t subdev, const uint8_t flags) {
    if (vxsdr::tx_stream_state == STREAM_ERROR) {
        LOG_ERROR(fmt::format("tx stream state is {:s} in tx_stop_now()", vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
    } else if(vxsdr::tx_stream_state == STREAM_STOPPED) {
        LOG_DEBUG(fmt::format("tx stream state is {:s} in tx_stop_now()", vxsdr::stream_state_to_string(vxsdr::tx_stream_state)));
    }
    // send a stop anyway, even if we think the state is wrong
    header_only_packet p;
    p.hdr        = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0};
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "tx_stop_now()");
    if (resp_ok) {
        vxsdr::tx_stream_state = STREAM_STOPPED;
        return true;
    }
    return false;
}

bool vxsdr::rx_stop_now(const uint8_t subdev, const uint8_t flags) {
    if (vxsdr::rx_stream_state == STREAM_ERROR) {
        LOG_ERROR(fmt::format("rx stream state is {:s} in rx_stop_now()", vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
    } else if(vxsdr::rx_stream_state == STREAM_STOPPED) {
        LOG_DEBUG(fmt::format("rx stream state is {:s} in rx_stop_now()", vxsdr::stream_state_to_string(vxsdr::rx_stream_state)));
    }
    // send a stop anyway, even if we think the state is wrong
    header_only_packet p;
    p.hdr        = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_STOP_NOW, subdev, flags, sizeof(p), 0};
    auto resp_ok = vxsdr::send_packet_and_check_response(p, "rx_stop_now()");
    if (resp_ok) {
        vxsdr::rx_stream_state = STREAM_STOPPED;
        return true;
    }
    return false;
}

bool vxsdr::set_tx_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev, const uint8_t flags) {
    two_double_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_IQ_BIAS, subdev, flags, sizeof(p), 0};
    p.value1 = std::min(1.0, std::max(-1.0, i_bias));
    p.value2 = std::min(1.0, std::max(-1.0, q_bias));
    return vxsdr::send_packet_and_check_response(p, "set_tx_iq_bias()");
}

bool vxsdr::set_tx_iq_bias(const std::array<double, 2> bias, const uint8_t subdev, const uint8_t flags) {
    return vxsdr::set_tx_iq_bias(bias.at(0), bias.at(1), subdev, flags);
}

bool vxsdr::set_rx_iq_bias(const double i_bias, const double q_bias, const uint8_t subdev, const uint8_t flags) {
    two_double_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_IQ_BIAS, subdev, flags, sizeof(p), 0};
    p.value1 = std::min(1.0, std::max(-1.0, i_bias));
    p.value2 = std::min(1.0, std::max(-1.0, q_bias));
    return vxsdr::send_packet_and_check_response(p, "set_rx_iq_bias()");
}

bool vxsdr::set_rx_iq_bias(const std::array<double, 2> bias, const uint8_t subdev, const uint8_t flags) {
    return vxsdr::set_rx_iq_bias(bias.at(0), bias.at(1), subdev, flags);
}

std::optional<std::array<double, 2>> vxsdr::get_tx_iq_bias(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_IQ_BIAS, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_iq_bias()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

std::optional<std::array<double, 2>> vxsdr::get_rx_iq_bias(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_IQ_BIAS, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_iq_bias()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

bool vxsdr::set_tx_iq_corr(const double ii_corr,
                           const double iq_corr,
                           const double qi_corr,
                           const double qq_corr,
                           const uint8_t subdev,
                           const uint8_t flags) {
    four_double_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_IQ_CORR, subdev, flags, sizeof(p), 0};
    p.value1 = ii_corr;
    p.value2 = iq_corr;
    p.value3 = qi_corr;
    p.value4 = qq_corr;
    return vxsdr::send_packet_and_check_response(p, "set_tx_iq_corr()");
}

bool vxsdr::set_tx_iq_corr(const std::array<double, 4> corr, const uint8_t subdev, const uint8_t flags) {
    return vxsdr::set_tx_iq_corr(corr.at(0), corr.at(1), corr.at(2), corr.at(3), subdev, flags);
}

bool vxsdr::set_rx_iq_corr(const double ii_corr,
                           const double iq_corr,
                           const double qi_corr,
                           const double qq_corr,
                           const uint8_t subdev,
                           const uint8_t flags) {
    four_double_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_IQ_CORR, subdev, flags, sizeof(p), 0};
    p.value1 = ii_corr;
    p.value2 = iq_corr;
    p.value3 = qi_corr;
    p.value4 = qq_corr;
    return vxsdr::send_packet_and_check_response(p, "set_rx_iq_corr()");
}

bool vxsdr::set_rx_iq_corr(const std::array<double, 4> corr, const uint8_t subdev, const uint8_t flags) {
    return vxsdr::set_rx_iq_corr(corr.at(0), corr.at(1), corr.at(2), corr.at(3), subdev, flags);
}

std::optional<std::array<double, 4>> vxsdr::get_tx_iq_corr(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_IQ_CORR, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_iq_corr()");
    if (res) {
        auto q                     = res.value();
        auto* r                    = reinterpret_cast<four_double_packet*>(&q);
        std::array<double, 4> corr = {r->value1, r->value2, r->value3, r->value4};
        return corr;
    }
    return std::nullopt;
}

std::optional<std::array<double, 4>> vxsdr::get_rx_iq_corr(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_IQ_CORR, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_iq_corr()");
    if (res) {
        auto q                     = res.value();
        auto* r                    = reinterpret_cast<four_double_packet*>(&q);
        std::array<double, 4> corr = {r->value1, r->value2, r->value3, r->value4};
        return corr;
    }
    return std::nullopt;
}

std::optional<std::array<double, 2>> vxsdr::get_tx_freq_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ_RANGE, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_tx_freq_range()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

std::optional<std::array<double, 2>> vxsdr::get_rx_freq_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ_RANGE, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_rx_freq_range()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

bool vxsdr::set_tx_freq(const double freq_hz, const uint8_t subdev, const uint8_t flags) {
    two_double_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0};
    p.value1 = freq_hz;
    p.value2 = 1e-9;
    return vxsdr::send_packet_and_check_response(p, "set_tx_freq()");
}

bool vxsdr::set_rx_freq(const double freq_hz, const uint8_t subdev, const uint8_t flags) {
    two_double_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_FREQ, subdev, flags, sizeof(p), 0};
    p.value1 = freq_hz;
    p.value2 = 1e-9;
    return vxsdr::send_packet_and_check_response(p, "set_rx_freq()");
}

std::optional<double> vxsdr::get_tx_freq(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_freq()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_double_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<double> vxsdr::get_rx_freq(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_FREQ, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_freq()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_double_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<std::array<double, 2>> vxsdr::get_tx_gain_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN_RANGE, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_tx_gain_range()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

std::optional<std::array<double, 2>> vxsdr::get_rx_gain_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN_RANGE, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_rx_gain_range()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

bool vxsdr::set_tx_gain(const double gain_db, const uint8_t subdev, const uint8_t flags) {
    one_double_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0};
    p.value1 = gain_db;
    return vxsdr::send_packet_and_check_response(p, "set_tx_gain()");
}

bool vxsdr::set_rx_gain(const double gain_db, const uint8_t subdev, const uint8_t flags) {
    one_double_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_GAIN, subdev, flags, sizeof(p), 0};
    p.value1 = gain_db;
    return vxsdr::send_packet_and_check_response(p, "set_rx_gain()");
}

std::optional<double> vxsdr::get_tx_gain(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_gain()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_double_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<double> vxsdr::get_rx_gain(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_GAIN, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_gain()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_double_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<std::array<double, 2>> vxsdr::get_tx_rate_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE_RANGE, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_tx_rate_range()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

std::optional<std::array<double, 2>> vxsdr::get_rx_rate_range(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE_RANGE, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_rx_rate_range()");
    if (res) {
        auto q                    = res.value();
        auto* r                   = reinterpret_cast<two_double_packet*>(&q);
        std::array<double, 2> res = {r->value1, r->value2};
        return res;
    }
    return std::nullopt;
}

bool vxsdr::set_tx_rate(const double rate_samples_sec, const uint8_t subdev, const uint8_t flags) {
    one_double_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0};
    p.value1 = rate_samples_sec;
    return vxsdr::send_packet_and_check_response(p, "set_tx_rate()");
}

bool vxsdr::set_rx_rate(const double rate_samples_sec, const uint8_t subdev, const uint8_t flags) {
    one_double_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_SAMPLE_RATE, subdev, flags, sizeof(p), 0};
    p.value1 = rate_samples_sec;
    return vxsdr::send_packet_and_check_response(p, "set_rx_rate()");
}

std::optional<double> vxsdr::get_tx_rate(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_tx_rate()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_double_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<double> vxsdr::get_rx_rate(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_SAMPLE_RATE, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "get_rx_rate()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_double_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<unsigned> vxsdr::get_tx_num_ports(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_NUM_RF_PORTS, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_tx_num_ports()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_uint32_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<unsigned> vxsdr::get_rx_num_ports(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_NUM_RF_PORTS, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_rx_num_ports()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_uint32_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<std::string> vxsdr::get_tx_port_name(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    one_uint32_packet p = {};
    p.hdr               = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_PORT_NAME, subdev, flags, sizeof(p), 0};
    p.value1            = port_num;
    auto res            = vxsdr::send_packet_and_return_response(p, "get_tx_port_name()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<name_packet*>(&q);
        return std::string((char*)r->name, std::min(strlen((char*)r->name), MAX_PAYLOAD_LENGTH_BYTES - 1));
    }
    return std::nullopt;
}

std::optional<std::string> vxsdr::get_rx_port_name(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    one_uint32_packet p = {};
    p.hdr               = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_PORT_NAME, subdev, flags, sizeof(p), 0};
    p.value1            = port_num;
    auto res            = vxsdr::send_packet_and_return_response(p, "get_rx_port_name()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<name_packet*>(&q);
        return std::string((char*)r->name, std::min(strlen((char*)r->name), MAX_PAYLOAD_LENGTH_BYTES - 1));
    }
    return std::nullopt;
}

bool vxsdr::set_tx_port(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    one_uint32_packet p = {};
    p.hdr               = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, subdev, flags, sizeof(p), 0};
    p.value1            = port_num;
    return vxsdr::send_packet_and_check_response(p, "set_tx_port() [by number]");
}

bool vxsdr::set_tx_port(const std::string& port_name, const uint8_t subdev, const uint8_t flags) {
    name_packet p = {};
    p.hdr         = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_PORT_BY_NAME, subdev, flags, sizeof(p), 0};
    strncpy((char*)p.name, port_name.c_str(), MAX_PAYLOAD_LENGTH_BYTES - 1);
    p.name[MAX_PAYLOAD_LENGTH_BYTES - 1] = 0;
    return vxsdr::send_packet_and_check_response(p, "set_tx_port() [by name]");
}

bool vxsdr::set_rx_port(const unsigned port_num, const uint8_t subdev, const uint8_t flags) {
    one_uint32_packet p = {};
    p.hdr               = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, subdev, flags, sizeof(p), 0};
    p.value1            = port_num;
    return vxsdr::send_packet_and_check_response(p, "set_rx_port() [by number]");
}

bool vxsdr::set_rx_port(const std::string& port_name, const uint8_t subdev, const uint8_t flags) {
    name_packet p = {};
    p.hdr         = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_PORT_BY_NAME, subdev, flags, sizeof(p), 0};
    strncpy((char*)p.name, port_name.c_str(), MAX_PAYLOAD_LENGTH_BYTES - 1);
    p.name[MAX_PAYLOAD_LENGTH_BYTES - 1] = 0;
    return vxsdr::send_packet_and_check_response(p, "set_rx_port() [by name]");
}

std::optional<unsigned> vxsdr::get_tx_port(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_RF_PORT, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_tx_port()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_uint32_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<unsigned> vxsdr::get_rx_port(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_RF_PORT, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_rx_port()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_uint32_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

bool vxsdr::set_tx_enable(const bool enabled, const uint8_t subdev, const uint8_t flags) {
    one_uint32_packet p;
    p.hdr = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0};
    if (enabled) {
        p.value1 = 1;
    } else {
        p.value1 = 0;
    }
    return vxsdr::send_packet_and_check_response(p, "set_tx_enable()");
}

bool vxsdr::set_rx_enable(const bool enabled, const uint8_t subdev, const uint8_t flags) {
    one_uint32_packet p;
    p.hdr = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_ENABLED, subdev, flags, sizeof(p), 0};
    if (enabled) {
        p.value1 = 1;
    } else {
        p.value1 = 0;
    }
    return vxsdr::send_packet_and_check_response(p, "set_rx_enable()");
}

std::optional<capabilities_t> vxsdr::get_tx_capabilities(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_TX_RADIO_CMD, RADIO_CMD_GET_CAPABILITIES, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_tx_capabilities()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_uint64_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<capabilities_t> vxsdr::get_rx_capabilities(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_GET_CAPABILITIES, subdev, flags, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "get_rx_capabilities()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_uint64_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

std::optional<std::array<uint32_t, 2>> vxsdr::device_get_buffer_info(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_INFO, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_buffer_info()");
    if (res) {
        auto q                      = res.value();
        auto* r                     = reinterpret_cast<two_uint32_packet*>(&q);
        std::array<uint32_t, 2> ret = {r->value1, r->value2};
        return ret;
    }
    return std::nullopt;
}

std::optional<std::array<uint32_t, 2>> vxsdr::device_get_buffer_use(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p;
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_BUFFER_USE, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_buffer_use()");
    if (res) {
        auto q                      = res.value();
        auto* r                     = reinterpret_cast<two_uint32_packet*>(&q);
        std::array<uint32_t, 2> ret = {r->value1, r->value2};
        return ret;
    }
    return std::nullopt;
}

std::optional<std::array<bool, 3>> vxsdr::device_get_timing_status(const uint8_t subdev, const uint8_t flags) {
    // FIXME: these need to be defined as part of a generic API when stable
    constexpr uint32_t TIMING_STATUS_EXT_PPS_LOCK   = 0x00000001;
    constexpr uint32_t TIMING_STATUS_EXT_10MHZ_LOCK = 0x00000002;
    constexpr uint32_t TIMING_STATUS_REF_OSC_LOCK   = 0x00000004;
    header_only_packet p                            = {};
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIMING_STATUS, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_timing_status()");
    if (res) {
        auto q                  = res.value();
        auto* r                 = reinterpret_cast<one_uint32_packet*>(&q);
        std::array<bool, 3> ret = {(bool)(r->value1 & TIMING_STATUS_EXT_PPS_LOCK),
                                   (bool)(r->value1 & TIMING_STATUS_EXT_10MHZ_LOCK),
                                   (bool)(r->value1 & TIMING_STATUS_REF_OSC_LOCK)};
        return ret;
    }
    return std::nullopt;
}

std::optional<double> vxsdr::device_get_timing_resolution(const uint8_t subdev, const uint8_t flags) {
    header_only_packet p = {};
    p.hdr    = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_TIMING_RESOLUTION, subdev, flags, sizeof(p), 0};
    auto res = vxsdr::send_packet_and_return_response(p, "device_get_timing_resolution()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_double_packet*>(&q);
        return r->value1;
    }
    return std::nullopt;
}

bool vxsdr::set_host_device_command_timeout(const double timeout_s) {
    uint64_t timeout_us = std::llround(1e6 * timeout_s);
    if (timeout_us < 1 or timeout_us > 3'600'000'000ULL) {
        return false;
    }
    device_response_timeout = std::chrono::microseconds(timeout_us);
    return true;
}

double vxsdr::get_host_device_command_timeout() const {
    return std::chrono::duration<double>(device_response_timeout).count();
}

std::vector<net::ip::address_v4> device_discover_ipv4_addresses(const net::ip::address_v4& local_addr,
                                                                const net::ip::address_v4& broadcast_addr,
                                                                const unsigned destination_port,
                                                                const double timeout_s) {
    std::vector<net::ip::address_v4> ret;

    // wait 1 / 1000 of the timeout specified each rx
    unsigned discover_wait_ms = llround(timeout_s);

    boost::system::error_code error;
    net::io_context discover_context;

    auto work           = net::make_work_guard(discover_context);
    auto context_thread = std::thread([&] { discover_context.run(); });

    net::ip::udp::endpoint local_endpoint(local_addr, destination_port);
    net::ip::udp::endpoint destination_endpoint(broadcast_addr, destination_port);

    net::ip::udp::socket discover_socket(discover_context, local_endpoint);

    discover_socket.open(net::ip::udp::v4(), error);
    if (error) {
        LOG_ERROR("unable to open socket in device_discover_ipv4_addresses()");
        if (context_thread.joinable()) {
            context_thread.join();
        }
        return ret;
    }

    discover_socket.set_option(net::ip::udp::socket::reuse_address(true), error);
    if (error) {
        LOG_ERROR("unable to set reuse_address option in device_discover_ipv4_addresses()");
        if (context_thread.joinable()) {
            context_thread.join();
        }
        return ret;
    }
    discover_socket.set_option(net::socket_base::broadcast(true), error);
    if (error) {
        LOG_ERROR("unable to set broadcast option in device_discover_ipv4_addresses()");
        if (context_thread.joinable()) {
            context_thread.join();
        }
        return ret;
    }

    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_DISCOVER, 0, 0, sizeof(p), 0};

    auto bytes_sent      = discover_socket.send_to(net::buffer(&p, sizeof(p)), destination_endpoint);
    if (bytes_sent != sizeof(p)) {
        LOG_ERROR("error sending discover packet in device_discover_ipv4_addresses()");
        if (context_thread.joinable()) {
            context_thread.join();
        }
        return ret;
    }

    auto t_start                    = std::chrono::steady_clock::now();
    std::chrono::duration<double> t = {};

    do {
        one_uint32_packet q = {};
        net::ip::udp::endpoint remote_endpoint;

        std::future<size_t> result =
            discover_socket.async_receive_from(net::buffer(&q, sizeof(q)), remote_endpoint, net::use_future);

        if (result.wait_for(std::chrono::milliseconds(discover_wait_ms)) == std::future_status::ready) {
            if (result.get() == sizeof(one_uint32_packet) and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP and
                q.hdr.command == p.hdr.command) {
                auto* r = reinterpret_cast<one_uint32_packet*>(&q);
                ret.emplace_back(net::ip::address_v4(r->value1));
            } else {
                LOG_WARN("extraneous response received in device_discover_ipv4_addresses()");
            }
        }
        t = t_start - std::chrono::steady_clock::now();
    } while (t.count() <= timeout_s);

    discover_socket.close(error);
    if (error) {
        LOG_ERROR("error closing socket in device_discover_ipv4_addresses()");
    }
    if (context_thread.joinable()) {
        context_thread.join();
    }

    return ret;
}

bool vxsdr::device_set_ipv4_address(const net::ip::address_v4& device_address) {
    one_uint32_packet p = {};
    p.hdr               = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TRANSPORT_ADDR, 0, 0, sizeof(p), 0};
    p.value1            = device_address.to_uint();
    return vxsdr::send_packet_and_check_response(p, "device_set_ipv4_address()");
}

bool vxsdr::device_save_ipv4_address(const net::ip::address_v4& device_address) {
    one_uint32_packet p = {};
    p.hdr               = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_SET_TRANSPORT_ADDR, 0, 0, sizeof(p), 0};
    p.value1            = device_address.to_uint();
    return vxsdr::send_packet_and_check_response(p, "device_save_ipv4_address()");
}

std::optional<unsigned> vxsdr::device_get_max_payload_bytes() {
    header_only_packet p = {};
    p.hdr                = {PACKET_TYPE_DEVICE_CMD, DEVICE_CMD_GET_MAX_PAYLOAD, 0, 0, sizeof(p), 0};
    auto res             = vxsdr::send_packet_and_return_response(p, "device_get_max_payload_bytes()");
    if (res) {
        auto q  = res.value();
        auto* r = reinterpret_cast<one_uint32_packet*>(&q);
        return (unsigned)r->value1;
    }
    return std::nullopt;
}

bool vxsdr::device_set_max_payload_bytes(const unsigned max_payload_bytes) {
    one_uint32_packet p = {};
    p.hdr               = {PACKET_TYPE_RX_RADIO_CMD, RADIO_CMD_SET_RF_PORT, 0, 0, sizeof(p), 0};
    p.value1            = max_payload_bytes;
    return vxsdr::send_packet_and_check_response(p, "device_set_max_payload_bytes()");
}

// private functions

[[nodiscard]] bool vxsdr::send_packet_and_check_response(packet& p, const std::string& cmd_name) {
#ifdef VXSDR_RESPONSE_TIME_DIAG
    auto t0 = std::chrono::steady_clock::now();
#endif // VXSDR_RESPONSE_TIME_DIAG
    if (not vxsdr::cmd_queue_push_check(p)) {
        return false;
    }
    response_queue_element q;
    bool got_response     = false;
    auto start_time = std::chrono::steady_clock::now();
    while (not(got_response = response_queue.pop(q)) and (std::chrono::steady_clock::now() - start_time) < vxsdr::device_response_timeout) {
        std::this_thread::sleep_for(vxsdr::device_response_wait);
    }
    if (got_response) {
        if (((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP) or
             (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_RSP) or
             (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_RSP)) and
            q.hdr.command == p.hdr.command) {
#ifdef VXSDR_RESPONSE_TIME_DIAG
            auto t1 = std::chrono::steady_clock::now();
            std::chrono::duration<double> diff = t1 - t0;
            LOG_INFO(fmt::format("TIMING: type 0x{:02x} cmd 0x{:02x} response {:10.3f} ms", p.hdr.packet_type, p.hdr.command, 1e3 * diff.count()));
#endif // VXSDR_RESPONSE_TIME_DIAG
            return true;
        }
        if (((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_ERR) or
             (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_ERR) or
             (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_ERR)) and
            q.hdr.command == p.hdr.command) {
            LOG_ERROR(
                fmt::format("command error in {:s}: {:s}", cmd_name, error_to_string(reinterpret_cast<error_packet*>(&q)->value1)));
            return false;
        }
        LOG_ERROR(fmt::format("invalid response received in {:s}", cmd_name));
        return false;
    }
#ifdef VXSDR_RESPONSE_TIME_DIAG
    auto t1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = t1 - t0;
    LOG_ERROR(fmt::format("TIMING: type 0x{:02x} cmd 0x{:02x} response {:10.3f} ms", p.hdr.packet_type, p.hdr.command, 1e3 * diff.count()));
#endif // VXSDR_RESPONSE_TIME_DIAG
    LOG_ERROR(fmt::format("timeout waiting for response in {:s}", cmd_name));
    return false;
}

[[nodiscard]] std::optional<vxsdr::response_queue_element> vxsdr::send_packet_and_return_response(packet& p,
                                                                                                  const std::string& cmd_name) {
#ifdef VXSDR_RESPONSE_TIME_DIAG
    auto t0 = std::chrono::steady_clock::now();
#endif // VXSDR_RESPONSE_TIME_DIAG
    if (not vxsdr::cmd_queue_push_check(p)) {
        return std::nullopt;
    }
    response_queue_element q;
    bool got_response     = false;
    auto start_time = std::chrono::steady_clock::now();
    while (not(got_response = response_queue.pop(q)) and (std::chrono::steady_clock::now() - start_time) < vxsdr::device_response_timeout)  {
        std::this_thread::sleep_for(vxsdr::device_response_wait);
    }
    if (got_response) {
        if (((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_RSP) or
             (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_RSP) or
             (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_RSP)) and
            q.hdr.command == p.hdr.command) {
#ifdef VXSDR_RESPONSE_TIME_DIAG
            auto t1 = std::chrono::steady_clock::now();
            std::chrono::duration<double> diff = t1 - t0;
            LOG_INFO(fmt::format("TIMING: type 0x{:02x} cmd 0x{:02x} response {:10.3f} ms", p.hdr.packet_type, p.hdr.command, 1e3 * diff.count()));
#endif // VXSDR_RESPONSE_TIME_DIAG
            return q;
        }
        if (((p.hdr.packet_type == PACKET_TYPE_DEVICE_CMD and q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD_ERR) or
             (p.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_ERR) or
             (p.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_ERR)) and
            q.hdr.command == p.hdr.command) {
            LOG_ERROR(
                fmt::format("command error in {:s}: {:s}", cmd_name, error_to_string(reinterpret_cast<error_packet*>(&q)->value1)));
            return std::nullopt;
        }
        LOG_ERROR(fmt::format("invalid response received in {:s}", cmd_name));
        return std::nullopt;
    }
#ifdef VXSDR_RESPONSE_TIME_DIAG
    auto t1 = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = t1 - t0;
    LOG_ERROR(fmt::format("TIMING: type 0x{:02x} cmd 0x{:02x} response {:10.3f} ms", p.hdr.packet_type, p.hdr.command, 1e3 * diff.count()));
#endif // VXSDR_RESPONSE_TIME_DIAG
    LOG_ERROR(fmt::format("timeout waiting for response in {:s}", cmd_name));
    return std::nullopt;
}

[[nodiscard]] bool vxsdr::cmd_queue_push_check(packet& p, const std::string& cmd_name) {
    command_queue_element q;
    std::memcpy(&q, &p, std::min((size_t)p.hdr.packet_size, sizeof(q)));
    if (not command_queue.bounded_push(q)) {
        LOG_ERROR(fmt::format("error pushing to command queue in {:s}", cmd_name));
        log_packet_info(p);
        return false;
    }
    if ((q.hdr.flags & FLAGS_REQUEST_ACK) != 0) {
        cmd_ack_queue_element a;
        bool got_response     = false;
        auto start_time = std::chrono::steady_clock::now();
        while (not(got_response = response_queue.pop(q)) and (std::chrono::steady_clock::now() - start_time) < vxsdr::device_response_timeout) {
            std::this_thread::sleep_for(device_response_wait);
        }
        if (got_response) {
            auto* r                 = reinterpret_cast<header_only_packet*>(&a);
            bool packet_types_match = false;
            if ((q.hdr.packet_type == PACKET_TYPE_DEVICE_CMD and r->hdr.packet_type == PACKET_TYPE_DEVICE_CMD_ACK) or
                (q.hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD and r->hdr.packet_type == PACKET_TYPE_RX_RADIO_CMD_ACK) or
                (q.hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD and r->hdr.packet_type == PACKET_TYPE_TX_RADIO_CMD_ACK)) {
                packet_types_match = true;
            }
            // for an ack to match, packet types, commands, and subdevices must all match
            if (packet_types_match and r->hdr.command == q.hdr.command and r->hdr.target_subdevice == q.hdr.target_subdevice) {
                return true;
            }
            LOG_ERROR(fmt::format("invalid ack received in {:s}", cmd_name));
            return false;
        }
        LOG_ERROR(fmt::format("timeout waiting for ack in {:s}", cmd_name));
        return false;
    }
    return true;
}

void vxsdr::time_point_to_time_spec_t(const std::chrono::time_point<std::chrono::system_clock>& t, time_spec_t& ts) {
    auto secs = std::chrono::time_point_cast<std::chrono::seconds>(t);
    auto nsecs = std::chrono::time_point_cast<std::chrono::nanoseconds>(t)
                    - std::chrono::time_point_cast<std::chrono::nanoseconds>(secs);
    ts.seconds     = (uint32_t)secs.time_since_epoch().count();
    ts.nanoseconds = (uint32_t)nsecs.count();
}

std::string vxsdr::stream_state_to_string(const stream_state_t state) {
    switch (state) {
        case STREAM_WAITING_FOR_START:
            return "WAITING_FOR_START";
        case STREAM_RUNNING:
            return "RUNNING";
        case STREAM_STOPPED:
            return "STOPPED";
        case STREAM_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string vxsdr::transport_state_to_string(const transport_state_t state) {
    switch (state) {
        case TRANSPORT_UNINITIALIZED:
            return "UNINITIALIZED";
        case TRANSPORT_STARTING:
            return "STARTING";
        case TRANSPORT_RUNNING:
            return "RUNNING";
        case TRANSPORT_SHUTDOWN:
            return "SHUTDOWN";
        case TRANSPORT_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string vxsdr::error_to_string(const uint32_t reason) {
    switch (reason) {
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

bool vxsdr::reset_rx_stream(const uint64_t n_samples_desired) {
    LOG_DEBUG("reset rx stream started");
    vxsdr::desired_rx_stream_samples =  n_samples_desired;
    vxsdr::sequence_errors_current_stream = 0;
    vxsdr::samples_received_current_stream = 0;
    vxsdr::rx_data_queue.reset();
    if(rx_data_queue.read_available() > 0) {
        LOG_ERROR("rx data queue should be empty");
    }
    LOG_DEBUG("reset rx stream finished");
    return true;
}

bool vxsdr::reset_tx_stream(const uint64_t n_samples_desired) {
    LOG_DEBUG("reset tx stream started");
    vxsdr::desired_tx_stream_samples =  n_samples_desired;
    vxsdr::send_errors_current_stream = 0;
    vxsdr::samples_sent_current_stream = 0;
    vxsdr::tx_data_queue.reset();
    if(tx_data_queue.read_available() > 0) {
        LOG_ERROR("tx data queue should be empty");
    }
    LOG_DEBUG("reset tx stream finished");
    return true;
}

void vxsdr::async_handler() {
    LOG_DEBUG("async_handler started");
    while (udp_rx_state != TRANSPORT_SHUTDOWN) {
        async_msg_queue_element a;
        while (async_msg_queue.pop(a)) {
            log_packet_info(a);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(async_queue_check_us));
    }
    LOG_DEBUG("async_handler finished");
}
