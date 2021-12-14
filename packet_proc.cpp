#include <bitset>
#include "logging.hpp"
#include "vxsdr_header.h"

std::string packet_type_to_name(const uint8_t number) {
    switch (number) {
        case PACKET_TYPE_TX_SIGNAL_DATA:
            return "TX_SIGNAL_DATA";
        case PACKET_TYPE_RX_SIGNAL_DATA:
            return "RX_SIGNAL_DATA";
        case PACKET_TYPE_DEVICE_CMD:
            return "DEVICE_CMD";
        case PACKET_TYPE_TX_RADIO_CMD:
            return "TX_RADIO_CMD";
        case PACKET_TYPE_RX_RADIO_CMD:
            return "RX_RADIO_CMD";
        case PACKET_TYPE_ASYNC_MSG:
            return "ASYNC_MSG";
        case PACKET_TYPE_DEVICE_CMD_ERR:
            return "DEVICE_CMD_ERR";
        case PACKET_TYPE_TX_RADIO_CMD_ERR:
            return "TX_RADIO_CMD_ERR";
        case PACKET_TYPE_RX_RADIO_CMD_ERR:
            return "RX_RADIO_CMD_ERR";
        case PACKET_TYPE_DEVICE_CMD_RSP:
            return "DEVICE_CMD_RSP";
        case PACKET_TYPE_TX_RADIO_CMD_RSP:
            return "TX_RADIO_CMD_RSP";
        case PACKET_TYPE_RX_RADIO_CMD_RSP:
            return "RX_RADIO_CMD_RSP";
        case PACKET_TYPE_TX_SIGNAL_DATA_ACK:
            return "TX_SIGNAL_DATA_ACK";
        case PACKET_TYPE_RX_SIGNAL_DATA_ACK:
            return "RX_SIGNAL_DATA_ACK";
        case PACKET_TYPE_DEVICE_CMD_ACK:
            return "DEVICE_CMD_ACK";
        case PACKET_TYPE_TX_RADIO_CMD_ACK:
            return "TX_RADIO_CMD_ACK";
        case PACKET_TYPE_RX_RADIO_CMD_ACK:
            return "RX_RADIO_CMD_ACK";
        case PACKET_TYPE_INVALID_PACKET:
            return "INVALID_PACKET";
        default:
            return fmt::format("packet type {:d}", number);
            ;
    }
}

std::string device_cmd_to_name(const uint8_t cmd) {
    switch (cmd) {
        case DEVICE_CMD_HELLO:
            return "HELLO";
        case DEVICE_CMD_SET_TIME_NOW:
            return "SET_TIME_NOW";
        case DEVICE_CMD_SET_TIME_NEXT_PPS:
            return "SET_TIME_NEXT_PPS";
        case DEVICE_CMD_GET_TIME:
            return "GET_TIME";
        case DEVICE_CMD_GET_STATUS:
            return "GET_STATUS";
        case DEVICE_CMD_CLEAR_STATUS:
            return "CLEAR_STATUS";
        case DEVICE_CMD_GET_BUFFER_INFO:
            return "GET_BUFFER_INFO";
        case DEVICE_CMD_GET_BUFFER_USE:
            return "GET_BUFFER_USE";
        case DEVICE_CMD_GET_TRANSPORT_INFO:
            return "GET_TRANSPORT_INFO";
        case DEVICE_CMD_GET_TRANSPORT_ADDR:
            return "GET_TRANSPORT_ADDR";
        case DEVICE_CMD_GET_MAX_PAYLOAD:
            return "GET_MAX_PAYLOAD";
        case DEVICE_CMD_CLEAR_DATA_BUFFER:
            return "CLEAR_DATA_BUFFER";
        case DEVICE_CMD_SET_TRANSPORT_ADDR:
            return "SET_TRANSPORT_ADDR";
        case DEVICE_CMD_SET_MAX_PAYLOAD:
            return "SET_MAX_PAYLOAD";
        case DEVICE_CMD_SAVE_TRANSPORT_ADDR:
            return "SAVE_TRANSPORT_ADDR";
        case DEVICE_CMD_GET_CHANNEL_INFO:
            return "GET_CHANNEL_INFO";
        case DEVICE_CMD_GET_TIMING_INFO:
            return "GET_TIMING_INFO";
        case DEVICE_CMD_GET_TIMING_STATUS:
            return "GET_TIMING_STATUS";
        case DEVICE_CMD_GET_TIMING_REF:
            return "GET_TIMING_REF";
        case DEVICE_CMD_SET_TIMING_REF:
            return "SET_TIMING_REF";
        // FIXME: these may be removed in the future
        case DEVICE_CMD_READ_DAC_REG:
            return "READ_DAC_REG";
        case DEVICE_CMD_WRITE_DAC_REG:
            return "WRITE_DAC_REG";
        case DEVICE_CMD_READ_ADC_REG:
            return "READ_ADC_REG";
        case DEVICE_CMD_WRITE_ADC_REG:
            return "WRITE_ADC_REG";
        // end deprecated commands
        case DEVICE_CMD_APP_UPDATE_MODE_SET:
            return "APP_UPDATE_MODE_SET";
        case DEVICE_CMD_APP_UPDATE_DATA:
            return "APP_UPDATE_DATA";
        case DEVICE_CMD_APP_UPDATE_DONE:
            return "APP_UPDATE_DONE";
        case DEVICE_CMD_STOP:
            return "STOP";
        case DEVICE_CMD_RESET:
            return "RESET";
        default:
            return fmt::format("command {:d}", cmd);
    }
}
std::string device_cmd_error_to_name(const uint32_t reason) {
    switch (reason) {
        case DEVICE_CMD_ERR_BAD_CMD:
            return "BAD_CMD";
        case DEVICE_CMD_ERR_BUSY:
            return "BUSY";
        case DEVICE_CMD_ERR_NO_SUCH_SUBDEVICE:
            return "NO_SUCH_SUBDEVICE";
        case DEVICE_CMD_ERR_TIMEOUT:
            return "TIMEOUT";
        case DEVICE_CMD_ERR_BAD_PARAMETER:
            return "BAD_PARAMETER";
        case DEVICE_CMD_ERR_NOT_SUPPORTED:
            return "NOT_SUPPORTED";
        case DEVICE_CMD_ERR_BAD_PKT_SIZE:
            return "BAD_PKT_SIZE";
        default:
            return fmt::format("reason {:d}", reason);
    }
}

std::string radio_cmd_to_name(const uint8_t cmd) {
    switch (cmd) {
        case RADIO_CMD_START_NOW_UNTIL_STOP:
            return "START_NOW_UNTIL_STOP";
        case RADIO_CMD_START_AT_TIME_UNTIL_STOP:
            return "START_AT_TIME_UNTIL_STOP";
        case RADIO_CMD_STOP_NOW:
            return "STOP_NOW";
        case RADIO_CMD_START_NOW_FOR_N_SAMPLES:
            return "START_NOW_FOR_N_SAMPLES";
        case RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES:
            return "START_AT_TIME_FOR_N_SAMPLES";
        case RADIO_CMD_GET_RF_FREQ:
            return "GET_RF_FREQ";
        case RADIO_CMD_GET_RF_GAIN:
            return "GET_RF_GAIN";
        case RADIO_CMD_GET_SAMPLE_RATE:
            return "GET_SAMPLE_RATE";
        case RADIO_CMD_GET_RF_BW:
            return "GET_RF_BW";
        case RADIO_CMD_GET_RF_ENABLED:
            return "GET_RF_ENABLED";
        case RADIO_CMD_GET_RF_PORT:
            return "GET_RF_PORT";
        case RADIO_CMD_GET_NUM_RF_PORTS:
            return "GET_NUM_RF_PORTS";
        case RADIO_CMD_GET_RF_PORT_NAME:
            return "GET_RF_PORT_NAME";
        case RADIO_CMD_GET_LO_INPUT:
            return "GET_LO_INPUT";
        case RADIO_CMD_GET_MASTER_CLK:
            return "GET_MASTER_CLK";
        case RADIO_CMD_SET_RF_FREQ:
            return "SET_RF_FREQ";
        case RADIO_CMD_SET_RF_GAIN:
            return "SET_RF_GAIN";
        case RADIO_CMD_SET_SAMPLE_RATE:
            return "SET_SAMPLE_RATE";
        case RADIO_CMD_SET_RF_BW:
            return "SET_RF_BW";
        case RADIO_CMD_SET_RF_ENABLED:
            return "SET_RF_ENABLED";
        case RADIO_CMD_SET_RF_PORT:
            return "SET_RF_PORT";
        case RADIO_CMD_SET_LO_INPUT:
            return "SET_LO_INPUT";
        case RADIO_CMD_SET_MASTER_CLK:
            return "SET_MASTER_CLK";
        case RADIO_CMD_GET_NUM_SUBDEVS:
            return "GET_NUM_SUBDEVS";
        case RADIO_CMD_GET_CAPABILITIES:
            return "GET_CAPABILITIES";
        case RADIO_CMD_GET_RF_FREQ_RANGE:
            return "GET_RF_FREQ_RANGE";
        case RADIO_CMD_GET_RF_GAIN_RANGE:
            return "GET_GAIN_RANGE";
        case RADIO_CMD_GET_SAMPLE_RATE_RANGE:
            return "GET_RATE_RANGE";
        case RADIO_CMD_GET_IQ_BIAS:
            return "GET_IQ_BIAS";
        case RADIO_CMD_GET_IQ_CORR:
            return "GET_IQ_CORR";
        case RADIO_CMD_SET_IQ_BIAS:
            return "SET_IQ_BIAS";
        case RADIO_CMD_SET_IQ_CORR:
            return "SET_IQ_CORR";
        default:
            return fmt::format("command {:d}", cmd);
    }
}

std::string radio_cmd_error_to_name(const uint32_t reason) {
    switch (reason) {
        case RADIO_CMD_ERR_BAD_CMD:
            return "BAD_CMD";
        case RADIO_CMD_ERR_BUSY:
            return "BUSY";
        case RADIO_CMD_ERR_NO_SUCH_SUBDEVICE:
            return "NO_SUCH_SUBDEVICE";
        case RADIO_CMD_ERR_TIMEOUT:
            return "TIMEOUT";
        case RADIO_CMD_ERR_BAD_PARAMETER:
            return "BAD_PARAMETER";
        case RADIO_CMD_ERR_NOT_SUPPORTED:
            return "NOT_SUPPORTED";
        case RADIO_CMD_ERR_BAD_PKT_SIZE:
            return "BAD_PKT_SIZE";
        default:
            return fmt::format("reason {:d}", reason);
    }
}

std::string async_msg_to_name(const uint8_t msg) {
    switch (msg) {
        case ASYNC_NO_ERROR:
            return "NO_ERROR";
        case ASYNC_DATA_UNDERFLOW:
            return "DATA_UNDERFLOW";
        case ASYNC_DATA_OVERFLOW:
            return "DATA_OVERFLOW";
        case ASYNC_OVER_TEMP:
            return "OVER_TEMP";
        case ASYNC_POWER_ERROR:
            return "POWER_ERROR";
        case ASYNC_FREQ_ERROR:
            return "FREQ_ERROR";
        case ASYNC_OUT_OF_SEQUENCE:
            return "OUT_OF_SEQUENCE";
        case ASYNC_CTRL_OVERFLOW:
            return "CTRL_OVERFLOW";
        case ASYNC_PPS_TIMEOUT:
            return "PPS_TIMEOUT";
        default:
            return fmt::format("message {:d}", msg);
    }
}

void log_packet_info(packet& packet, const int direction) {
    if (direction == 0) {
        LOG_PACKET(fmt::format("PACKET: type 0x{:02X} cmd 0x{:02X} flags 0x{:02X} subdev {:d} size {:d} seq {:d}",
                               packet.hdr.packet_type, packet.hdr.command, packet.hdr.flags, packet.hdr.target_subdevice,
                               packet.hdr.packet_size, packet.hdr.sequence_counter));
    } else if (direction > 0) {
        LOG_PACKET(fmt::format("SENT: type 0x{:02X} cmd 0x{:02X} flags 0x{:02X} subdev {:d} size {:d} seq {:d}",
                               packet.hdr.packet_type, packet.hdr.command, packet.hdr.flags, packet.hdr.target_subdevice,
                               packet.hdr.packet_size, packet.hdr.sequence_counter));
    } else {
        LOG_PACKET(
            fmt::format("RECEIVED: type 0x{:02X} cmd 0x{:02X} flags 0x{:02X} subdev {:d} size {:d} seq {:d}",
                        packet.hdr.packet_type, packet.hdr.command, packet.hdr.flags, packet.hdr.target_subdevice,
                        packet.hdr.packet_size, packet.hdr.sequence_counter));
    }
    switch (packet.hdr.packet_type) {
        case PACKET_TYPE_DEVICE_CMD:
            switch (packet.hdr.command) {
                case DEVICE_CMD_SET_TIME_NOW: {
                    LOG_PACKET(fmt::format("DEVICE_CMD: SET_TIME_NOW"));
                    auto* tr = reinterpret_cast<cmd_or_rsp_packet_time*>(&packet);
                    LOG_PACKET(fmt::format("   seconds                   = {:d}", (uint32_t)(tr->time.seconds)));
                    LOG_PACKET(fmt::format("   nanoseconds               = {:09d}", (uint32_t)(tr->time.nanoseconds)));
                } break;
                case DEVICE_CMD_SET_TIME_NEXT_PPS: {
                    LOG_PACKET(fmt::format("DEVICE_CMD: SET_TIME_NEXT_PPS"));
                    auto* tr = reinterpret_cast<cmd_or_rsp_packet_time*>(&packet);
                    LOG_PACKET(fmt::format("   seconds                   = {:d}", (uint32_t)(tr->time.seconds)));
                    LOG_PACKET(fmt::format("   nanoseconds               = {:09d}", (uint32_t)(tr->time.nanoseconds)));
                } break;
                default:
                    LOG_PACKET(fmt::format("DEVICE_CMD: {:s}", device_cmd_to_name(packet.hdr.command)));
                    break;
            }
            break;
        case PACKET_TYPE_DEVICE_CMD_ACK:
            LOG_PACKET(fmt::format("DEVICE_CMD_ACK: {:s}", device_cmd_to_name(packet.hdr.command)));
            break;
        case PACKET_TYPE_DEVICE_CMD_ERR: {
            LOG_PACKET(fmt::format("DEVICE_CMD_ERR: {:s}", device_cmd_to_name(packet.hdr.command)));
            auto* er = reinterpret_cast<error_packet*>(&packet);
            LOG_PACKET(fmt::format("   reason                      = {:s}", device_cmd_error_to_name(er->value1)));
        } break;
        case PACKET_TYPE_DEVICE_CMD_RSP:
            switch (packet.hdr.command) {
                case DEVICE_CMD_HELLO: {
                    LOG_PACKET(fmt::format("DEVICE_CMD_RSP: HELLO"));
                    auto* hr = reinterpret_cast<four_uint32_packet*>(&packet);
                    LOG_PACKET(fmt::format("   board id                  = {:d}", (uint32_t)hr->value1));
                    LOG_PACKET(fmt::format("   fpga fw version           = {:d}", (uint32_t)hr->value2));
                    LOG_PACKET(fmt::format("   mcu sw version            = {:d}", (uint32_t)hr->value3));
                    LOG_PACKET(fmt::format("   serial number             = {:d}", (uint32_t)hr->value4));
                } break;
                case DEVICE_CMD_GET_STATUS: {
                    LOG_PACKET(fmt::format("DEVICE_CMD_RSP: GET_STATUS"));
                    auto* sr = reinterpret_cast<eight_uint32_packet*>(&packet);
                    // FIXME: figure out
                    LOG_PACKET(fmt::format("   fpga_temperature          = {:d}", sr->value1));
                    LOG_PACKET(fmt::format("   dac_temperature           = {:d}", sr->value2));
                    LOG_PACKET(fmt::format("   sequence_error_count      = {:d}", sr->value3));
                    LOG_PACKET(fmt::format("   data_flow_error_flags     = {:s}", std::bitset<8>((sr->value4) % 256).to_string()));
                    LOG_PACKET(fmt::format("   ddr_arbiter_status        = {:d}", sr->value5));
                    LOG_PACKET(fmt::format("   ethernet_status           = {:s}", std::bitset<8>((sr->value6) % 256).to_string()));
                    LOG_PACKET(fmt::format("   adc_dac_status            = {:x}", sr->value7));
                    LOG_PACKET(fmt::format("   ddr_cal_result            = {:d}", sr->value8));
                } break;
                case DEVICE_CMD_GET_BUFFER_INFO: {
                    LOG_PACKET(fmt::format("DEVICE_CMD_RSP: GET_BUFFER_INFO"));
                    auto* br = reinterpret_cast<two_uint32_packet*>(&packet);
                    LOG_PACKET(fmt::format("   rx_buffer_size_bytes      = {:d}", (uint32_t)br->value1));
                    LOG_PACKET(fmt::format("   tx_buffer_size_bytes      = {:d}", (uint32_t)br->value2));
                } break;
                case DEVICE_CMD_GET_BUFFER_USE: {
                    LOG_PACKET(fmt::format("DEVICE_CMD_RSP: GET_BUFFER_USE"));
                    auto* br = reinterpret_cast<two_uint32_packet*>(&packet);
                    LOG_PACKET(fmt::format("   rx_buffer_used_bytes      = {:d}", (uint32_t)br->value1));
                    LOG_PACKET(fmt::format("   tx_buffer_used_bytes      = {:d}", (uint32_t)br->value2));
                } break;
                case DEVICE_CMD_GET_TIME: {
                    LOG_PACKET(fmt::format("DEVICE_CMD_RSP: GET_TIME"));
                    auto* tr = reinterpret_cast<cmd_or_rsp_packet_time*>(&packet);
                    LOG_PACKET(fmt::format("   seconds                   = {:d}", (uint32_t)(tr->time.seconds)));
                    LOG_PACKET(fmt::format("   nanoseconds               = {:09d}", (uint32_t)(tr->time.nanoseconds)));
                } break;
                default:
                    LOG_PACKET(fmt::format("DEVICE_CMD_RSP: {:s}", device_cmd_to_name(packet.hdr.command)));
                    break;
            }
            break;
        case PACKET_TYPE_RX_RADIO_CMD:
            LOG_PACKET(fmt::format("RX_RADIO_CMD: {:s}", radio_cmd_to_name(packet.hdr.command)));
            break;
        case PACKET_TYPE_RX_RADIO_CMD_RSP:
            switch (packet.hdr.command) {
                case RADIO_CMD_GET_RF_FREQ: {
                    LOG_PACKET(fmt::format("RX_RADIO_CMD_RSP: GET_RF_FREQ"));
                    auto* fr = reinterpret_cast<one_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   freq                      = {:.5e}", fr->value1));
                } break;
                case RADIO_CMD_GET_RF_GAIN: {
                    LOG_PACKET(fmt::format("RX_RADIO_CMD_RSP: GET_RF_GAIN"));
                    auto* gr = reinterpret_cast<one_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   gain                      = {:.2f}", gr->value1));
                } break;
                case RADIO_CMD_GET_SAMPLE_RATE: {
                    LOG_PACKET(fmt::format("RX_RADIO_CMD_RSP: GET_SAMPLE_RATE"));
                    auto* sr = reinterpret_cast<one_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   rate                      = {:.5e}", sr->value1));
                } break;
                case RADIO_CMD_GET_SAMPLE_RATE_RANGE:
                case RADIO_CMD_GET_RF_FREQ_RANGE: {
                    LOG_PACKET(fmt::format("RX_RADIO_CMD_RSP: {:s}", radio_cmd_to_name(packet.hdr.command)));
                    auto* r = reinterpret_cast<two_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   min                       = {:.5e}", r->value1));
                    LOG_PACKET(fmt::format("   max                       = {:.5e}", r->value2));
                } break;
                case RADIO_CMD_GET_RF_GAIN_RANGE: {
                    LOG_PACKET(fmt::format("RX_RADIO_CMD_RSP: {:s}", radio_cmd_to_name(packet.hdr.command)));
                    auto* r = reinterpret_cast<two_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   min                       = {:.1f}", r->value1));
                    LOG_PACKET(fmt::format("   max                       = {:.1f}", r->value2));
                } break;
                default:
                    LOG_PACKET(fmt::format("RX_RADIO_CMD_RSP: {:s}", radio_cmd_to_name(packet.hdr.command)));
                    break;
            }
            break;
        case PACKET_TYPE_RX_RADIO_CMD_ERR: {
            LOG_PACKET(fmt::format("RX_RADIO_CMD_ERR: {:s}", radio_cmd_to_name(packet.hdr.command)));
            auto* er = reinterpret_cast<error_packet*>(&packet);
            LOG_PACKET(fmt::format("   reason                      = {:s}", radio_cmd_error_to_name(er->value1)));
        } break;
        case PACKET_TYPE_RX_RADIO_CMD_ACK:
            LOG_PACKET(fmt::format("RX_RADIO_CMD_ACK: {:s}", radio_cmd_to_name(packet.hdr.command)));
            break;
        case PACKET_TYPE_TX_RADIO_CMD:
            LOG_PACKET(fmt::format("TX_RADIO_CMD: {:s}", radio_cmd_to_name(packet.hdr.command)));
            break;
        case PACKET_TYPE_TX_RADIO_CMD_RSP:
            switch (packet.hdr.command) {
                case RADIO_CMD_GET_RF_FREQ: {
                    LOG_PACKET(fmt::format("TX_RADIO_CMD_RSP: GET_RF_FREQ"));
                    auto* fr = reinterpret_cast<one_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   freq                      = {:.5e}", fr->value1));
                } break;
                case RADIO_CMD_GET_RF_GAIN: {
                    LOG_PACKET(fmt::format("TX_RADIO_CMD_RSP: GET_RF_GAIN"));
                    auto* gr = reinterpret_cast<one_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   gain                      = {:.2f}", gr->value1));
                } break;
                case RADIO_CMD_GET_SAMPLE_RATE: {
                    LOG_PACKET(fmt::format("TX_RADIO_CMD_RSP: GET_SAMPLE_RATE"));
                    auto* sr = reinterpret_cast<one_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   rate                      = {:.5e}", sr->value1));
                } break;
                case RADIO_CMD_GET_SAMPLE_RATE_RANGE:
                case RADIO_CMD_GET_RF_FREQ_RANGE: {
                    LOG_PACKET(fmt::format("TX_RADIO_CMD_RSP: {:s}", radio_cmd_to_name(packet.hdr.command)));
                    auto* r = reinterpret_cast<two_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   min                       = {:.1f}", r->value1));
                    LOG_PACKET(fmt::format("   max                       = {:.1f}", r->value2));
                } break;
                case RADIO_CMD_GET_RF_GAIN_RANGE: {
                    LOG_PACKET(fmt::format("TX_RADIO_CMD_RSP: {:s}", radio_cmd_to_name(packet.hdr.command)));
                    auto* r = reinterpret_cast<two_double_packet*>(&packet);
                    LOG_PACKET(fmt::format("   min                       = {:.1f}", r->value1));
                    LOG_PACKET(fmt::format("   max                       = {:.1f}", r->value2));
                } break;
                default:
                    LOG_PACKET(fmt::format("TX_RADIO_CMD_RSP: {:s}", radio_cmd_to_name(packet.hdr.command)));
                    break;
            }
            break;
        case PACKET_TYPE_TX_RADIO_CMD_ERR:
            switch (packet.hdr.command) {
                default: {
                    LOG_PACKET(fmt::format("TX_RADIO_CMD_ERR: {:s}", radio_cmd_to_name(packet.hdr.command)));
                    auto* er = reinterpret_cast<error_packet*>(&packet);
                    LOG_PACKET(fmt::format("   reason                      = {:s}", radio_cmd_error_to_name(er->value1)));
                } break;
            }
        case PACKET_TYPE_TX_RADIO_CMD_ACK:
            LOG_PACKET(fmt::format("TX_RADIO_CMD_ACK: {:s}", radio_cmd_to_name(packet.hdr.command)));
            break;
        case PACKET_TYPE_TX_SIGNAL_DATA_ACK: {
            LOG_PACKET(fmt::format("TX_SIGNAL_DATA_ACK"));
            auto* br = reinterpret_cast<six_uint32_packet*>(&packet);
            LOG_PACKET(fmt::format("   requester_seq_num         = {:d}", br->value1));
            LOG_PACKET(fmt::format("   tx_buffer_used_bytes      = {:d}", br->value3));
            LOG_PACKET(fmt::format("   tx_buffer_size_bytes      = {:d}", br->value4));
            LOG_PACKET(fmt::format("   sequence_error_count      = {:d}", br->value5));
        } break;
        case PACKET_TYPE_ASYNC_MSG:
            LOG_PACKET(fmt::format("ASYNC_MSG: {:s}", async_msg_to_name(packet.hdr.command)));
            break;
        default:
            LOG_PACKET(fmt::format("PACKET_TYPE: {:s} COMMAND {:d}", packet_type_to_name(packet.hdr.packet_type),
                                   (int)packet.hdr.command));
            break;
    }
}