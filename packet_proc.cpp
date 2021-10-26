#include <bitset>
#include "logging.hpp"
#include "vxsdr_header.hpp"

std::string packet_type_to_name(const uint8_t number) {
  switch(number) {
    case PACKET_TYPE_TX_SIGNAL_DATA:
      return "TX_SIGNAL_DATA";
    case PACKET_TYPE_RX_SIGNAL_DATA:
      return "RX_SIGNAL_DATA";
    case PACKET_TYPE_DEVICE_CMD:
      return "DEVICE_CMD";
    case PACKET_TYPE_ASYNC_MSG:
      return "ASYNC_MSG";
    case PACKET_TYPE_TX_RADIO_CMD:
      return "TX_RADIO_CMD";
    case PACKET_TYPE_RX_RADIO_CMD:
      return "RX_RADIO_CMD";
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
    default:
      return "UNDEFINED";
  }
}

std::string device_command_to_name(const uint8_t cmd) {
  switch(cmd) {
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
    case DEVICE_CMD_GET_BUFFER_INFO:
      return "GET_BUFFER_INFO";
    case DEVICE_CMD_GET_BUFFER_USE:
      return "GET_BUFFER_USE";
    case DEVICE_CMD_OPEN_SESSION:
      return "OPEN_SESSION";
    case DEVICE_CMD_CLOSE_SESSION:
      return "CLOSE_SESSION";
    case DEVICE_CMD_GET_TRANSPORT_INFO:
      return "GET_TRANSPORT_INFO";
    case DEVICE_CMD_GET_TRANSPORT_ADDR:
      return "GET_TRANSPORT_ADDR";
    case DEVICE_CMD_GET_MAX_PAYLOAD:
      return "GET_MAX_PAYLOAD";
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
/*
    case DEVICE_CMD_READ_DAC_REG:
      return "READ_DAC_REG";
    case DEVICE_CMD_WRITE_DAC_REG:
      return "WRITE_DAC_REG";
    case DEVICE_CMD_READ_ADC_REG:
      return "READ_ADC_REG";
    case DEVICE_CMD_WRITE_ADC_REG:
      return "WRITE_ADC_REG";
*/
    case DEVICE_CMD_STOP:
      return "STOP";
    case DEVICE_CMD_RESET:
      return "RESET";
    default:
      return "UNDEFINED";
  }
}

std::string device_command_error_to_name(const uint8_t cmd) {
  switch(cmd) {
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
    case DEVICE_CMD_ERR_OUT_OF_SEQUENCE:
      return "OUT_OF_SEQUENCE";
    case DEVICE_CMD_ERR_CTRL_BUFFER_OVERFLOW:
      return "CTRL_BUFFER_OVERFLOW";
    default:
      return fmt::format("{:d}", cmd);
  }
}


void log_packet_info(packet &packet)
{
  switch(packet.hdr.packet_type) {
    case PACKET_TYPE_DEVICE_CMD_ACK:
      LOG_INFO(fmt::format("DEVICE_CMD_ACK: {:s}", device_command_to_name(packet.hdr.command)));
      break;
    case PACKET_TYPE_DEVICE_CMD_ERR:
        LOG_INFO(fmt::format("DEVICE_CMD_ERR: {:s}", device_command_error_to_name(packet.hdr.command)));
        break;
    case PACKET_TYPE_DEVICE_CMD_RSP:
      switch(packet.hdr.command) {
        case DEVICE_CMD_HELLO:
          {
            LOG_INFO(fmt::format("DEVICE_CMD_RSP: HELLO"));
            auto *hr = reinterpret_cast<hello_rsp_packet *>(&packet);
            LOG_INFO(fmt::format("   mcu_sw_version            = {:d}", (uint32_t)hr->mcu_sw_version));
            LOG_INFO(fmt::format("   fpga_fw_version           = {:d}", (uint32_t)hr->fpga_fw_version));
          }
          break;
        case DEVICE_CMD_GET_STATUS:
          {
            LOG_INFO(fmt::format("DEVICE_CMD_RSP: GET_STATUS"));
            auto *sr = reinterpret_cast<status_rsp_packet *>(&packet);
            LOG_INFO(fmt::format("   fpga_temperature          = {:d}", sr->fpga_temperature));
            LOG_INFO(fmt::format("   dac_temperature           = {:d}", sr->dac_temperature));
            LOG_INFO(fmt::format("   sequence_error_count      = {:d}", sr->sequence_error_count));
            LOG_INFO(fmt::format("   data_flow_error_flags     = {:s}", std::bitset<8>((sr->data_flow_error_flags) % 256).to_string()));
            LOG_INFO(fmt::format("   ddr_arbiter_status        = {:d}", sr->ddr_arbiter_status));
            LOG_INFO(fmt::format("   ethernet_status           = {:s}", std::bitset<8>((sr->ethernet_status) % 256).to_string()));
          }
          break;
        case DEVICE_CMD_GET_BUFFER_INFO:
          {
            LOG_INFO(fmt::format("DEVICE_CMD_RSP: GET_BUFFER_INFO"));
            auto *br = reinterpret_cast<get_buffer_info_rsp_packet *>(&packet);
            LOG_INFO(fmt::format("   rx_buffer_size_bytes      = {:d}", (uint32_t)br->rx_buffer_size_bytes));
            LOG_INFO(fmt::format("   tx_buffer_size_bytes      = {:d}", (uint32_t)br->tx_buffer_size_bytes));
          }
          break;
        case DEVICE_CMD_GET_BUFFER_USE:
          {
            LOG_INFO(fmt::format("DEVICE_CMD_RSP: GET_BUFFER_USE"));
            auto *br = reinterpret_cast<get_buffer_use_rsp_packet *>(&packet);
            LOG_INFO(fmt::format("   rx_buffer_used_bytes      = {:d}", (uint32_t)br->rx_buffer_used_bytes));
            LOG_INFO(fmt::format("   tx_buffer_used_bytes      = {:d}", (uint32_t)br->tx_buffer_used_bytes));
          }
          break;
        case DEVICE_CMD_GET_TIME:
          {
            LOG_INFO(fmt::format("DEVICE_CMD_RSP: GET_TIME"));
            auto *tr = reinterpret_cast<cmd_or_rsp_packet_time *>(&packet);
            LOG_INFO(fmt::format("   seconds                   = {:d}", (uint32_t)(tr->time.seconds)));
            LOG_INFO(fmt::format("   nanoseconds               = {:09d}", (uint32_t)(tr->time.nanoseconds)));
          }
          break;
        default:
          {
            LOG_INFO(fmt::format("DEVICE_CMD_RSP: {:d}", (int)packet.hdr.command));
          }
          break;
      }
      break;
    case PACKET_TYPE_RX_RADIO_CMD_RSP:
      switch(packet.hdr.command) {
        case RADIO_CMD_GET_RF_FREQ:
          {
            LOG_INFO(fmt::format("RX_RADIO_CMD_RSP: GET_RF_FREQ"));
            auto *fr = reinterpret_cast<radio_rf_freq_packet *>(&packet);
            LOG_INFO(fmt::format("   freq                      = {:.5e}", fr->freq));
          }
          break;
        case RADIO_CMD_GET_RF_GAIN:
          {
            LOG_INFO(fmt::format("RX_RADIO_CMD_RSP: GET_RF_GAIN"));
            auto *gr = reinterpret_cast<radio_rf_gain_packet *>(&packet);
            LOG_INFO(fmt::format("   gain                      = {:.2f}", gr->gain));
          }
          break;
        case RADIO_CMD_GET_SAMPLE_RATE:
          {
            LOG_INFO(fmt::format("RX_RADIO_CMD_RSP: GET_SAMPLE_RATE"));
            auto *sr = reinterpret_cast<radio_sample_rate_packet *>(&packet);
            LOG_INFO(fmt::format("   rate                      = {:.5e}", sr->rate));
          }
          break;
        default:
          LOG_INFO(fmt::format("RX_RADIO_CMD_RSP: {:d}", (int)packet.hdr.command));
          break;
      }
      break;
    case PACKET_TYPE_RX_RADIO_CMD_ERR:
      switch(packet.hdr.command) {
        default:
          LOG_INFO(fmt::format("RX_RADIO_CMD_ERR: {:d}", (int)packet.hdr.command));
          break;
      }
    case PACKET_TYPE_RX_RADIO_CMD_ACK:
      LOG_INFO(fmt::format("RX_RADIO_CMD_ACK: {:d}", (int)packet.hdr.command));
      break;
    case PACKET_TYPE_TX_RADIO_CMD:
      switch(packet.hdr.command) {
        case RADIO_CMD_GET_RF_FREQ:
          {
            LOG_INFO(fmt::format("TX_RADIO_CMD_RSP: GET_RF_FREQ"));
            auto *fr = reinterpret_cast<radio_rf_freq_packet *>(&packet);
            LOG_INFO(fmt::format("   freq                      = {:.5e}", fr->freq));
          }
          break;
        case RADIO_CMD_GET_RF_GAIN:
          {
            LOG_INFO(fmt::format("TX_RADIO_CMD_RSP: GET_RF_GAIN"));
            auto *gr = reinterpret_cast<radio_rf_gain_packet *>(&packet);
            LOG_INFO(fmt::format("   gain                      = {:.2f}", gr->gain));
          }
          break;
        case RADIO_CMD_GET_SAMPLE_RATE:
          {
            LOG_INFO(fmt::format("TX_RADIO_CMD_RSP: GET_SAMPLE_RATE"));
            auto *sr = reinterpret_cast<radio_sample_rate_packet *>(&packet);
            LOG_INFO(fmt::format("   rate                      = {:.5e}", sr->rate));
          }
          break;
        default:
          LOG_INFO(fmt::format("TX_RADIO_CMD_RSP: {:d}", (int)packet.hdr.command));
          break;
      }
      break;
    case PACKET_TYPE_TX_RADIO_CMD_ERR:
      switch(packet.hdr.command) {
        default:
          LOG_INFO(fmt::format("TX_RADIO_CMD_ERR: {:d}", (int)packet.hdr.command));
          break;
      }
    case PACKET_TYPE_TX_RADIO_CMD_ACK:
      LOG_INFO(fmt::format("TX_RADIO_CMD_ACK: {:d}", (int)packet.hdr.command));
      break;
    case PACKET_TYPE_TX_SIGNAL_DATA_ACK:
      {
        LOG_INFO(fmt::format("TX_SIGNAL_DATA_ACK"));
        auto *br = reinterpret_cast<tx_signal_data_ack_packet *>(&packet);
        LOG_INFO(fmt::format("   requester_seq_num         = {:d}", (uint64_t)br->requester_seq_num));
        LOG_INFO(fmt::format("   tx_buffer_used_bytes      = {:d}", (uint32_t)br->tx_buffer_used_bytes));
        LOG_INFO(fmt::format("   sequence_error_count      = {:d}", (uint32_t)br->sequence_error_count));
      }
      break;
    case PACKET_TYPE_ASYNC_MSG:
      switch(packet.hdr.command) {
        case ASYNC_DATA_UNDERFLOW:
          LOG_INFO(fmt::format("ASYNC_DATA_UNDERFLOW"));
          break;
        case ASYNC_DATA_OVERFLOW:
          LOG_INFO(fmt::format("ASYNC_DATA_OVERFLOW"));
          break;
        case ASYNC_OVER_TEMP:
          LOG_INFO(fmt::format("ASYNC_OVER_TEMP"));
          break;
        case ASYNC_POWER_ERROR:
          LOG_INFO(fmt::format("ASYNC_POWER_ERROR"));
          break;
        case ASYNC_FREQ_ERROR:
          LOG_INFO(fmt::format("ASYNC_FREQ_ERROR"));
          break;
        default:
          LOG_INFO(fmt::format("ASYNC_MSG: {:d}", (int)packet.hdr.command));
          break;
      }
    default:
      LOG_INFO(fmt::format("PACKET_TYPE: {:d} COMMAND {:d}", packet.hdr.packet_type, (int)packet.hdr.command));
      break;
  }
}