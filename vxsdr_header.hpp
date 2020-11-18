#ifndef VXSDR_HEADER_HPP
#define VXSDR_HEADER_HPP

#include <cstdint>
#include <complex>

namespace PACKET_TYPES {
  // packet types
  static constexpr uint8_t TX_SIGNAL_DATA     =   1;
  static constexpr uint8_t RX_SIGNAL_DATA     =   2;
  static constexpr uint8_t FPGA_CMD           =   3;
  static constexpr uint8_t MCU_CMD            =   4;
  static constexpr uint8_t TX_RADIO_CMD       =   5;
  static constexpr uint8_t RX_RADIO_CMD       =   6;
  static constexpr uint8_t FPGA_ENQ           =   7;
  static constexpr uint8_t MCU_ENQ            =   8;
  static constexpr uint8_t TX_RADIO_ENQ       =   9;
  static constexpr uint8_t RX_RADIO_ENQ       =  10;
  static constexpr uint8_t TX_SIGNAL_DATA_ACK =  11;
  static constexpr uint8_t RX_SIGNAL_DATA_ACK =  12;
  static constexpr uint8_t FPGA_CMD_ACK       =  15;
  static constexpr uint8_t MCU_CMD_ACK        =  16;
  static constexpr uint8_t TX_RADIO_CMD_ACK   =  17;
  static constexpr uint8_t RX_RADIO_CMD_ACK   =  18;
  static constexpr uint8_t FPGA_CMD_ERR       =  19;
  static constexpr uint8_t MCU_CMD_ERR        =  20;
  static constexpr uint8_t TX_RADIO_CMD_ERR   =  21;
  static constexpr uint8_t RX_RADIO_CMD_ERR   =  22;
  static constexpr uint8_t FPGA_ENQ_RSP       =  23;
  static constexpr uint8_t CPU_ENQ_RSP        =  24;
  static constexpr uint8_t TX_RADIO_ENQ_RSP   =  25;
  static constexpr uint8_t RX_RADIO_ENQ_RSP   =  26;
};

static constexpr size_t   MAX_DATA_LENGTH = 2048;

typedef struct {
  uint8_t                 packet_type;
  uint8_t                 target_subdevice;  // for cases where multiple devices share a connection (e.g. multichannel TX/RX)
  uint16_t                flags;             // some meanings will be fixed (e.g. request ACK), some depend on packet type
  uint16_t                packet_size;       // length of packet including header in bytes
  uint16_t                sequence_counter;  // need to figure out when this resets (every new data stream, power up, ?)
} packet_header;

namespace FLAGS {
  static constexpr uint16_t REQUEST_ACQ       = 0x0001;  // bit 0
  static constexpr uint16_t TIME_PRESENT      = 0x0002;  // bit 1
  static constexpr uint16_t STREAM_ID_PRESENT = 0x0004;  // bit 2
//     bits 3-7: reserved for additional generally applicable settings
//     bits 8-15: reserved for settings specific to each packet type
};

typedef struct {
  packet_header           hdr;
  uint32_t                dac_ddr_used;
  uint32_t                dac_ddr_size;
} ack_packet;

typedef struct {
  packet_header           hdr;
  std::complex<int16_t>   data[(hdr.packet_size - sizeof(packet_header)) / sizeof(std::complex<int16_t>)]; // FIXME: find proper way to represent variable-mength data in struct
} data_packet;

#endif // VXSDR_HEADER_HPP