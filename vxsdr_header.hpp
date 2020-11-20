#ifndef VXSDR_HEADER_HPP
#define VXSDR_HEADER_HPP

/*
  SDR packet headers
  Version 0,1 20 Nov 2020
  FROZEN UNTIL INITIAL IMPLEMENTATION COMPLETE AND TESTED
*/

#include <cstdint>
#include <complex>

namespace PACKET_TYPES {
  // packet types
  static constexpr uint8_t TX_SIGNAL_DATA     = 0x00;
  static constexpr uint8_t RX_SIGNAL_DATA     = 0x01;
  static constexpr uint8_t DEVICE_CMD         = 0x02;
  static constexpr uint8_t TX_RADIO_CMD       = 0x03;
  static constexpr uint8_t RX_RADIO_CMD       = 0x04;
  static constexpr uint8_t TX_SIGNAL_DATA_ACK = 0x05;
  static constexpr uint8_t RX_SIGNAL_DATA_ACK = 0x06;
  static constexpr uint8_t DEVICE_CMD_ACK     = 0x07;
  static constexpr uint8_t TX_RADIO_CMD_ACK   = 0x08;
  static constexpr uint8_t RX_RADIO_CMD_ACK   = 0x09;
  static constexpr uint8_t DEVICE_CMD_ERR     = 0x0A;
  static constexpr uint8_t TX_RADIO_CMD_ERR   = 0x0B;
  static constexpr uint8_t RX_RADIO_CMD_ERR   = 0x0C;
  static constexpr uint8_t DEVICE_CMD_RSP     = 0x0D;
  static constexpr uint8_t TX_RADIO_CMD_RSP   = 0x0E;
  static constexpr uint8_t RX_RADIO_CMD_RSP   = 0x0F;
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
  static constexpr uint16_t REQUEST_ACK       = 0x8000;  // bit 0
  static constexpr uint16_t TIME_PRESENT      = 0x4000;  // bit 1
  static constexpr uint16_t STREAM_ID_PRESENT = 0x2000;  // bit 2
//     bits 3-7: reserved for additional generally applicable settings
  static constexpr uint16_t RESERVED_BIT_3    = 0x1000;
  static constexpr uint16_t RESERVED_BIT_4    = 0x0800;
  static constexpr uint16_t RESERVED_BIT_5    = 0x0400;
  static constexpr uint16_t RESERVED_BIT_6    = 0x0200;
  static constexpr uint16_t RESERVED_BIT_7    = 0x0100;
//     bits 8-15: reserved for settings specific to each packet type; given below for each type
  namespace DEVICE_CMD {
    static constexpr uint8_t HELLO                        = 0x00;
    static constexpr uint8_t SET_TIME_NOW                 = 0x01;  // error if TIME_PRESENT is 0
    static constexpr uint8_t SET_TIME_NEXT_PPS            = 0x02;  // error if TIME_PRESENT is 0
    static constexpr uint8_t GET_TIME                     = 0x03;
    static constexpr uint8_t GET_STATUS                   = 0x04;
    static constexpr uint8_t CLEAR_STATUS                 = 0x05;  // resets error flags and counters
    static constexpr uint8_t GET_BUFFER_USE               = 0x07;
    static constexpr uint8_t OPEN_SESSION                 = 0x08;
    static constexpr uint8_t CLOSE_SESSION                = 0x09;
    static constexpr uint8_t RESET                        = 0xFF;  // do a power on reset
  };
  namespace DEVICE_CMD_RSP {
    static constexpr uint8_t HELLO_RSP                    = 0x00;
    static constexpr uint8_t GET_TIME_RSP                 = 0x03;
    static constexpr uint8_t GET_STATUS_RSP               = 0x04;
    static constexpr uint8_t GET_BUFFER_USE_RSP           = 0x07;
    static constexpr uint8_t OPEN_SESSION_RSP             = 0x08;
    static constexpr uint8_t CLOSE_SESSION_RSP            = 0x09;
  };
  namespace DEVICE_CMD_ERR {
    static constexpr uint8_t BAD_CMD                      = 0x01;
    static constexpr uint8_t BUSY                         = 0x02;
  };
  namespace RADIO_CMD {
    static constexpr uint8_t START_NOW_UNTIL_STOP         = 0x01;
    static constexpr uint8_t START_AT_TIME_UNTIL_STOP     = 0x02;  // error if TIME_PRESENT is 0
    static constexpr uint8_t STOP_NOW                     = 0x03;  // stops anything going on
    static constexpr uint8_t START_NOW_FOR_N_SAMPLES      = 0x04;
    static constexpr uint8_t START_AT_TIME_FOR_N_SAMPLES  = 0x05;  // error if TIME_PRESENT is 0
  };
};

typedef struct {
  uint32_t                seconds;
  uint32_t                nanoseconds;
} time_spec;

typedef struct {
  packet_header           hdr;       // hdr.packet_type must be TX_RADIO_CMD or RX_RADIO_CMD
} stream_control_packet_bare;

typedef struct {
  packet_header           hdr;       // hdr.packet_type must be TX_RADIO_CMD or RX_RADIO_CMD
  uint64_t                n_samples;
} stream_control_packet_samples;

typedef struct {
  packet_header           hdr;
  time_spec               time;
} stream_control_packet_time;

typedef struct {
  packet_header           hdr;
  time_spec               time;
  uint64_t                n_samples;
} stream_control_packet_time_samples;

typedef struct {
  packet_header           hdr;
  uint32_t                dac_ddr_used;
  uint32_t                dac_ddr_size;
} get_buffer_use_rsp_packet;

typedef struct {
  packet_header           hdr;
  std::complex<int16_t>   data[(hdr.packet_size - sizeof(packet_header)) / sizeof(std::complex<int16_t>)]; // FIXME: find proper way to represent variable-mength data in struct
} data_packet;

#endif // VXSDR_HEADER_HPP