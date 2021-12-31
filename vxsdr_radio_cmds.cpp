#include "vxsdr.hpp"

/*! @file vxsdr_radio_cmds.cpp
    @brief Radio command functions for the @p vxsdr class.
*/

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