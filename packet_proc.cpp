#include <bitset>

#include <boost/format.hpp>

#include "logging.hpp"
#include "vxsdr_header.hpp"

void log_packet_info(cmd_or_rsp_packet &packet)
{
  switch(packet.hdr.packet_type) {
    case PACKET_TYPES::DEVICE_CMD_ACK:
      LOGGER(info) << "DEVICE_CMD_ACK: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::DEVICE_CMD_ERR:
      LOGGER(info) << "DEVICE_CMD_ERR: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::DEVICE_CMD_RSP:
      switch(packet.hdr.command) {
        case DEVICE_CMD::HELLO:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: HELLO";
            hello_rsp_packet *hr = reinterpret_cast<hello_rsp_packet *>(&packet);
            LOGGER(info) << "   proc_rev                  = " << (uint32_t)hr->proc_rev;
            LOGGER(info) << "   fpga_rev                  = " << (uint32_t)hr->fpga_rev;
          }
          break;
        case DEVICE_CMD::GET_STATUS:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: GET_STATUS";
            status_rsp_packet *sr = reinterpret_cast<status_rsp_packet *>(&packet);
            LOGGER(info) << "   fpga_temp                 = " << sr->fpga_temp;
            LOGGER(info) << "   dac_temp                  = " << sr->dac_temp;
            LOGGER(info) << "   sequence_err_count        = " << sr->sequence_err_count;
            LOGGER(info) << "   error_flags               = " << std::bitset<8>((sr->error_flags) % 256);
            LOGGER(info) << "   ddr_arbiter_status        = " << sr->ddr_arbiter_status;
            LOGGER(info) << "   tgbe_status               = " << std::bitset<8>((sr->tgbe_status) % 256);
          }
          break;
        case DEVICE_CMD::GET_BUFFER_INFO:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: GET_BUFFER_INFO";
            get_buffer_info_rsp_packet *br = reinterpret_cast<get_buffer_info_rsp_packet *>(&packet);
            LOGGER(info) << "   rx_buffer_size_bytes      = " << (uint32_t)br->rx_buffer_size_bytes;
            LOGGER(info) << "   tx_buffer_size_bytes      = " << (uint32_t)br->tx_buffer_size_bytes;
          }
          break;
        case DEVICE_CMD::GET_BUFFER_USE:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: GET_BUFFER_USE";
            get_buffer_use_rsp_packet *br = reinterpret_cast<get_buffer_use_rsp_packet *>(&packet);
            LOGGER(info) << "   rx_buffer_used_bytes      = " << (uint32_t)br->rx_buffer_used_bytes;
            LOGGER(info) << "   tx_buffer_used_bytes      = " << (uint32_t)br->tx_buffer_used_bytes;
          }
          break;
        case DEVICE_CMD::READ_ADC_REG:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: READ_ADC_REG";
            dev_read_write_reg_packet *br = reinterpret_cast<dev_read_write_reg_packet *>(&packet);
            LOGGER(info) << "   value                     = " << boost::format("%08x") % (uint32_t)br->value;
          }
          break;
        case DEVICE_CMD::READ_DAC_REG:
          {
            LOGGER(info) << "DEVICE_CMD_RSP: READ_DAC_REG";
            dev_read_write_reg_packet *br = reinterpret_cast<dev_read_write_reg_packet *>(&packet);
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
    case PACKET_TYPES::RX_RADIO_CMD_RSP:
      LOGGER(info) << "RX_RADIO_CMD_RSP: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::RX_RADIO_CMD_ERR:
      LOGGER(info) << "RX_RADIO_CMD_ERR: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::RX_RADIO_CMD_ACK:
      LOGGER(info) << "RX_RADIO_CMD_ACK: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::TX_RADIO_CMD_RSP:
      LOGGER(info) << "RX_RADIO_CMD_RSP: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::TX_RADIO_CMD_ERR:
      LOGGER(info) << "TX_RADIO_CMD_ERR: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::TX_RADIO_CMD_ACK:
      LOGGER(info) << "TX_RADIO_CMD_ACK: " << (int)packet.hdr.command;
      break;
    case PACKET_TYPES::TX_SIGNAL_DATA_ACK:
      {
        LOGGER(info) << "TX_SIGNAL_DATA_ACK";
        tx_signal_data_ack_packet *br = reinterpret_cast<tx_signal_data_ack_packet *>(&packet);
        LOGGER(info) << "   requester_seq_num         = " << (uint64_t)br->requester_seq_num;
        LOGGER(info) << "   tx_buffer_used_bytes      = " << (uint32_t)br->tx_buffer_used_bytes;
        LOGGER(info) << "   sequence_err_count        = " << (uint32_t)br->sequence_err_count;
      }
      break;
    case PACKET_TYPES::RX_SIGNAL_DATA:
      break;
    default:
      LOGGER(info) << "PACKET_TYPE: " << (int)packet.hdr.packet_type << " COMMAND " << (int)packet.hdr.command;
      break;
  }
}