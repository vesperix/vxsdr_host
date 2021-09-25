#include <bitset>

#include <boost/format.hpp>

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
    case DEVICE_CMD::HELLO:
      return "HELLO";
    case DEVICE_CMD::SET_TIME_NOW:
      return "SET_TIME_NOW";
    case DEVICE_CMD::SET_TIME_NEXT_PPS:
      return "SET_TIME_NEXT_PPS";
    case DEVICE_CMD::GET_TIME:
      return "GET_TIME";
    case DEVICE_CMD::GET_STATUS:
      return "GET_STATUS";
    case DEVICE_CMD::GET_BUFFER_INFO:
      return "GET_BUFFER_INFO";
    case DEVICE_CMD::GET_BUFFER_USE:
      return "GET_BUFFER_USE";
    case DEVICE_CMD::OPEN_SESSION:
      return "OPEN_SESSION";
    case DEVICE_CMD::CLOSE_SESSION:
      return "CLOSE_SESSION";
    case DEVICE_CMD::GET_TRANSPORT_INFO:
      return "GET_TRANSPORT_INFO";
    case DEVICE_CMD::GET_TRANSPORT_ADDR:
      return "GET_TRANSPORT_ADDR";
    case DEVICE_CMD::GET_TRANSPORT_PKTSIZ:
      return "GET_TRANSPORT_PKTSIZ";
    case DEVICE_CMD::SET_TRANSPORT_ADDR:
      return "SET_TRANSPORT_ADDR";
    case DEVICE_CMD::SET_TRANSPORT_PKTSIZ:
      return "SET_TRANSPORT_PKTSIZ";
    case DEVICE_CMD::SAVE_TRANSPORT_ADDR:
      return "SAVE_TRANSPORT_ADDR";
    case DEVICE_CMD::GET_CHANNEL_INFO:
      return "GET_CHANNEL_INFO";
    case DEVICE_CMD::GET_TIMING_INFO:
      return "GET_TIMING_INFO";
    case DEVICE_CMD::GET_TIMING_STATUS:
      return "GET_TIMING_STATUS";
    case DEVICE_CMD::GET_TIMING_REF:
      return "GET_TIMING_REF";
    case DEVICE_CMD::SET_TIMING_REF:
      return "SET_TIMING_REF";
    case DEVICE_CMD::READ_DAC_REG:
      return "READ_DAC_REG";
    case DEVICE_CMD::WRITE_DAC_REG:
      return "WRITE_DAC_REG";
    case DEVICE_CMD::READ_ADC_REG:
      return "READ_ADC_REG";
    case DEVICE_CMD::WRITE_ADC_REG:
      return "WRITE_ADC_REG";
    case DEVICE_CMD::STOP:
      return "STOP";
    case DEVICE_CMD::RESET:
      return "RESET";
    default:
      return "UNDEFINED";
  }
}

void log_packet_info(cmd_or_rsp_packet &packet)
{
  switch(packet.hdr.packet_type) {
    case PACKET_TYPE_DEVICE_CMD_ACK:
      LOGGER(info) << "DEVICE_CMD_ACK: " << device_command_to_name(packet.hdr.command);
      break;
    case PACKET_TYPE_DEVICE_CMD_ERR:
      LOGGER(info) << "DEVICE_CMD_ERR: " << device_command_to_name(packet.hdr.command);
      break;
    case PACKET_TYPE_DEVICE_CMD_RSP:
      switch(packet.hdr.command) {
        case DEVICE_CMD::HELLO:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: HELLO";
            auto *hr = reinterpret_cast<hello_rsp_packet *>(&packet);
            LOGGER(info) << "   mcu_sw_version            = " << (uint32_t)hr->mcu_sw_version;
            LOGGER(info) << "   fpga_fw_version           = " << (uint32_t)hr->fpga_fw_version;
          }
          break;
        case DEVICE_CMD::GET_STATUS:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: GET_STATUS";
            auto *sr = reinterpret_cast<status_rsp_packet *>(&packet);
            LOGGER(info) << "   fpga_temperature          = " << sr->fpga_temperature;
            LOGGER(info) << "   dac_temperature           = " << sr->dac_temperature;
            LOGGER(info) << "   sequence_error_count      = " << sr->sequence_error_count;
            LOGGER(info) << "   data_flow_error_flags     = " << std::bitset<8>((sr->data_flow_error_flags) % 256);
            LOGGER(info) << "   ddr_arbiter_status        = " << sr->ddr_arbiter_status;
            LOGGER(info) << "   ethernet_status           = " << std::bitset<8>((sr->ethernet_status) % 256);
          }
          break;
        case DEVICE_CMD::GET_BUFFER_INFO:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: GET_BUFFER_INFO";
            auto *br = reinterpret_cast<get_buffer_info_rsp_packet *>(&packet);
            LOGGER(info) << "   rx_buffer_size_bytes      = " << (uint32_t)br->rx_buffer_size_bytes;
            LOGGER(info) << "   tx_buffer_size_bytes      = " << (uint32_t)br->tx_buffer_size_bytes;
          }
          break;
        case DEVICE_CMD::GET_BUFFER_USE:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: GET_BUFFER_USE";
            auto *br = reinterpret_cast<get_buffer_use_rsp_packet *>(&packet);
            LOGGER(info) << "   rx_buffer_used_bytes      = " << (uint32_t)br->rx_buffer_used_bytes;
            LOGGER(info) << "   tx_buffer_used_bytes      = " << (uint32_t)br->tx_buffer_used_bytes;
          }
          break;
        case DEVICE_CMD::READ_ADC_REG:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: READ_ADC_REG";
            auto *br = reinterpret_cast<dev_read_write_reg_packet *>(&packet);
            LOGGER(info) << "   value                     = " << boost::format("%08x") % (uint32_t)br->value;
          }
          break;
        case DEVICE_CMD::READ_DAC_REG:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: READ_DAC_REG";
            auto *br = reinterpret_cast<dev_read_write_reg_packet *>(&packet);
            LOGGER(info) << "   value                     = " << boost::format("%04x") % (uint16_t)br->value;
          }
          break;
        default:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: " << (int)packet.hdr.command;
          }
          break;
      }
      break;
    case PACKET_TYPE_RX_RADIO_CMD_RSP:
      switch(packet.hdr.command) {
        case RADIO_CMD_RSP::GET_RF_FREQ_RSP:
          {
            LOGGER(info) << "RX_RADIO_CMD_RSP: GET_RF_FREQ";
            auto *fr = reinterpret_cast<radio_rf_freq_packet *>(&packet);
            LOGGER(info) << "   freq                      = " << boost::format("%.5e") % fr->freq;
          }
          break;
        case RADIO_CMD_RSP::GET_RF_GAIN_RSP:
          {
            LOGGER(info) << "RX_RADIO_CMD_RSP: GET_RF_GAIN";
            auto *gr = reinterpret_cast<radio_rf_gain_packet *>(&packet);
            LOGGER(info) << "   gain                      = " << boost::format("%.2f") % gr->gain;
          }
          break;
        case RADIO_CMD_RSP::GET_SAMPLE_RATE_RSP:
          {
            LOGGER(info) << "RX_RADIO_CMD_RSP: GET_SAMPLE_RATE";
            auto *sr = reinterpret_cast<radio_sample_rate_packet *>(&packet);
            LOGGER(info) << "   rate                      = " << boost::format("%.5e") % sr->rate;
          }
          break;
        default:
          LOGGER(info) << "RX_RADIO_CMD_RSP: " << (int)packet.hdr.command;
          break;
      }
      break;
    case PACKET_TYPE_RX_RADIO_CMD_ERR:
      LOGGER(info) << "RX_RADIO_CMD_ERR: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPE_RX_RADIO_CMD_ACK:
      LOGGER(info) << "RX_RADIO_CMD_ACK: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPE_TX_RADIO_CMD_RSP:
      switch(packet.hdr.command) {
        case RADIO_CMD_RSP::GET_RF_FREQ_RSP:
          {
            LOGGER(info) << "TX_RADIO_CMD_RSP: GET_RF_FREQ";
            auto *fr = reinterpret_cast<radio_rf_freq_packet *>(&packet);
            LOGGER(info) << "   freq                      = " << boost::format("%.5e") % fr->freq;
          }
          break;
        case RADIO_CMD_RSP::GET_RF_GAIN_RSP:
          {
            LOGGER(info) << "TX_RADIO_CMD_RSP: GET_RF_GAIN";
            auto *gr = reinterpret_cast<radio_rf_gain_packet *>(&packet);
            LOGGER(info) << "   gain                      = " << boost::format("%.2f") % gr->gain;
          }
          break;
        case RADIO_CMD_RSP::GET_SAMPLE_RATE_RSP:
          {
            LOGGER(info) << "TX_RADIO_CMD_RSP: GET_SAMPLE_RATE";
            auto *sr = reinterpret_cast<radio_sample_rate_packet *>(&packet);
            LOGGER(info) << "   rate                      = " << boost::format("%.5e") % sr->rate;
          }
          break;
        default:
          LOGGER(info) << "TX_RADIO_CMD_RSP: " << (int)packet.hdr.command;
          break;
      }
      break;
    case PACKET_TYPE_TX_RADIO_CMD_ERR:
      LOGGER(info) << "TX_RADIO_CMD_ERR: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPE_TX_RADIO_CMD_ACK:
      LOGGER(info) << "TX_RADIO_CMD_ACK: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPE_TX_SIGNAL_DATA_ACK:
      {
        LOGGER(info) << "TX_SIGNAL_DATA_ACK";
        auto *br = reinterpret_cast<tx_signal_data_ack_packet *>(&packet);
        LOGGER(info) << "   requester_seq_num         = " << (uint64_t)br->requester_seq_num;
        LOGGER(info) << "   tx_buffer_used_bytes      = " << (uint32_t)br->tx_buffer_used_bytes;
        LOGGER(info) << "   sequence_error_count      = " << (uint32_t)br->sequence_error_count;
      }
      break;
    default:
      LOGGER(info) << "PACKET_TYPE: " << (int)packet.hdr.packet_type << " COMMAND " << (int)packet.hdr.command;
      break;
  }
}