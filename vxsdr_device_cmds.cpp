#include "vxsdr.hpp"

/*! @file vxsdr_device_cmds.cpp
    @brief Device command functions for the @p vxsdr class.
*/

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
    p.hdr               = {PACKET_TYPE_DEVICE_CMD,  DEVICE_CMD_SET_MAX_PAYLOAD, 0, 0, sizeof(p), 0};
    p.value1            = max_payload_bytes;
    return vxsdr::send_packet_and_check_response(p, "device_set_max_payload_bytes()");
}
