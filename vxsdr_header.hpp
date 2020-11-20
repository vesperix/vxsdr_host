#ifndef VXSDR_HEADER_HPP
#define VXSDR_HEADER_HPP

/*
  SDR packet headers
  Version 0.1.2 20 Nov 2020
  FROZEN EXCEPT FOR ADDITIONS UNTIL INITIAL IMPLEMENTATION COMPLETE AND TESTED
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
  uint8_t                 command;
  uint8_t                 target_subdevice;  // for cases where multiple devices exist (e.g. multiple network I/F or multichannel TX/RX)
  uint8_t                 flags;
  uint16_t                packet_size;       // length of packet including header in bytes
  uint16_t                sequence_counter;  // need to figure out when this resets (every new data stream, power up, ?)
} packet_header;

namespace FLAGS {
  static constexpr uint8_t REQUEST_ACK       = 0x80;  // bit 0
  static constexpr uint8_t TIME_PRESENT      = 0x40;  // bit 1
  static constexpr uint8_t STREAM_ID_PRESENT = 0x20;  // bit 2
//     bits 3-7: reserved for additional settings
  static constexpr uint8_t RESERVED_BIT_3    = 0x10;
  static constexpr uint8_t RESERVED_BIT_4    = 0x08;
  static constexpr uint8_t RESERVED_BIT_5    = 0x04;
  static constexpr uint8_t RESERVED_BIT_6    = 0x02;
  static constexpr uint8_t RESERVED_BIT_7    = 0x01;
};

namespace DEVICE_CMD {
  static constexpr uint8_t HELLO                        = 0x00;
  static constexpr uint8_t SET_TIME_NOW                 = 0x01;  // error if TIME_PRESENT is 0
  static constexpr uint8_t SET_TIME_NEXT_PPS            = 0x02;  // error if TIME_PRESENT is 0
  static constexpr uint8_t GET_TIME                     = 0x03;
  static constexpr uint8_t GET_STATUS                   = 0x04;
  static constexpr uint8_t CLEAR_STATUS                 = 0x05;  // resets error flags and counters
  static constexpr uint8_t GET_BUFFER_INFO              = 0x06;  // returns info on how many buffers and their purpose
  static constexpr uint8_t GET_BUFFER_USE               = 0x07;  // asks for buffer info
  static constexpr uint8_t OPEN_SESSION                 = 0x08;
  static constexpr uint8_t CLOSE_SESSION                = 0x09;
  // 0x0A-0x0F reserved
  static constexpr uint8_t GET_TRANSPORT_INFO           = 0x10;  // how many interfaces, what kind
  static constexpr uint8_t GET_TRANSPORT_ADDR           = 0x11;  // we'll use this for IP addresses
  static constexpr uint8_t GET_TRANSPORT_PKTSIZ         = 0x12;
  // 0x12-0x1F reserved for more get_transport stuff as needed
  static constexpr uint8_t SET_TRANSPORT_ADDR           = 0x21;
  static constexpr uint8_t SET_TRANSPORT_PKTSIZ         = 0x22;
  // 0x22-0x2F reserved for more set_transport stuff as needed
  static constexpr uint8_t GET_CHANNEL_INFO             = 0x30;  // how many, are they TX/RX, etc
  // 0x31-0x4F reserved for channel parameter get and set
  static constexpr uint8_t GET_TIMING_INFO              = 0x50;  // what timing inputs are available
  static constexpr uint8_t GET_TIMING_STATUS            = 0x51;  // PPS present/absent; 10 MHz locked/unlocked
  static constexpr uint8_t GET_TIMING_REF               = 0x52;  // current sources of 10 MHz and PPS
  static constexpr uint8_t SET_TIMING_REF               = 0x53;  // set sources of 10 MHz and PPS

  // 0x50-0xFD reserved for stuff we haven't thought of yet
  static constexpr uint8_t STOP                         = 0xFE;  // stop all streaaming and return to wait state within session
  static constexpr uint8_t RESET                        = 0xFF;  // do a power on reset (closing session and returning to idle)
};
namespace DEVICE_CMD_RSP {
  static constexpr uint8_t HELLO_RSP                    = 0x00;  
  static constexpr uint8_t GET_TIME_RSP                 = 0x03;
  static constexpr uint8_t GET_STATUS_RSP               = 0x04;
  static constexpr uint8_t GET_BUFFER_USE_RSP           = 0x07;
  static constexpr uint8_t OPEN_SESSION_RSP             = 0x08;
  static constexpr uint8_t CLOSE_SESSION_RSP            = 0x09;
  static constexpr uint8_t GET_TRANSPORT_INFO_RSP       = 0x0A;
  static constexpr uint8_t GET_TRANSPORT_ADDR_RSP       = 0x10;
  static constexpr uint8_t GET_TRANSPORT_PKTSIZ_RSP     = 0x11;
  static constexpr uint8_t GET_CHANNEL_INFO_RSP         = 0x30;
  static constexpr uint8_t GET_TIMING_INFO_RSP          = 0x50;
  static constexpr uint8_t GET_TIMING_STATUS_RSP        = 0x51;
  static constexpr uint8_t GET_TIMING_REF_RSP           = 0x52;
};
namespace DEVICE_CMD_ERR {
  static constexpr uint8_t BAD_CMD                      = 0x01;  // generic bad stuff happened; can substitute for more detailed ones until they're implemented
  static constexpr uint8_t BUSY                         = 0x02;  // can't accept that command because we're running
  static constexpr uint8_t NO_SUCH_SUBDEVICE            = 0x03;  // the specified subdevice doesn't exist
  static constexpr uint8_t TIMEOUT                      = 0x04;  // tried to do command but something didn't answer
  static constexpr uint8_t BAD_PARAMETER                = 0x05;  // command parameter is out of allowed range
};
namespace RADIO_CMD {
  static constexpr uint8_t START_NOW_UNTIL_STOP         = 0x01;
  static constexpr uint8_t START_AT_TIME_UNTIL_STOP     = 0x02;  // error if TIME_PRESENT is 0
  static constexpr uint8_t STOP_NOW                     = 0x03;  // stops anything going on
  static constexpr uint8_t START_NOW_FOR_N_SAMPLES      = 0x04;
  static constexpr uint8_t START_AT_TIME_FOR_N_SAMPLES  = 0x05;  // error if TIME_PRESENT is 0
  // 0x06-0x0F reserved
  static constexpr uint8_t GET_RF_INFO                  = 0x10;  // available frequency, gain, sample rate, and bandwidth ranges, number of selectable I/Os
  static constexpr uint8_t GET_RF_FREQ                  = 0x11;
  static constexpr uint8_t GET_RF_GAIN                  = 0x12;
  static constexpr uint8_t GET_SAMPLE_RATE              = 0x13;
  static constexpr uint8_t GET_RF_BW                    = 0x14;
  static constexpr uint8_t GET_RF_IO                    = 0x15;
  // 0x16-0x20 reserved
  static constexpr uint8_t SET_RF_FREQ                  = 0x21;
  static constexpr uint8_t SET_RF_GAIN                  = 0x22;
  static constexpr uint8_t SET_RF_BW                    = 0x23;
  static constexpr uint8_t SET_SAMPLE_RATE              = 0x24;
  static constexpr uint8_t SET_RF_IO                    = 0x25;
  // 0x26-0xFF reserved
};
namespace RADIO_CMD_RSP {
  static constexpr uint8_t GET_RF_INFO_RSP              = 0x10;
  static constexpr uint8_t GET_RF_FREQ_RSP              = 0x11;
  static constexpr uint8_t GET_RF_GAIN_RSP              = 0x12;
  static constexpr uint8_t GET_SAMPLE_RATE_RSP          = 0x13;
  static constexpr uint8_t GET_RF_BW_RSP                = 0x14;
  static constexpr uint8_t GET_RF_IO_RSP                = 0x15;
};
namespace RADIO_CMD_ERR {
  static constexpr uint8_t BAD_CMD                      = 0x01;
  static constexpr uint8_t BUSY                         = 0x02;
  static constexpr uint8_t NO_SUCH_SUBDEVICE            = 0x03;
  static constexpr uint8_t TIMEOUT                      = 0x04;
  static constexpr uint8_t BAD_PARAMETER                = 0x05;
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