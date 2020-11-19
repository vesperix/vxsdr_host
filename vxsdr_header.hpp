#ifndef VXSDR_HEADER_HPP
#define VXSDR_HEADER_HPP

#include <cstdint>
#include <complex>

namespace PACKET_TYPES {
  // packet types
  static constexpr uint8_t TX_SIGNAL_DATA     =   1;
  static constexpr uint8_t RX_SIGNAL_DATA     =   2;
  static constexpr uint8_t DEVICE_CMD         =   3;
  static constexpr uint8_t TX_RADIO_CMD       =   4;
  static constexpr uint8_t RX_RADIO_CMD       =   5;
  static constexpr uint8_t DEVICE_ENQ         =   6;
  static constexpr uint8_t TX_RADIO_ENQ       =   7;
  static constexpr uint8_t RX_RADIO_ENQ       =   8;
  static constexpr uint8_t TX_SIGNAL_DATA_ACK =   9;
  static constexpr uint8_t RX_SIGNAL_DATA_ACK =  10;
  static constexpr uint8_t DEVICE_CMD_ACK     =  11;
  static constexpr uint8_t TX_RADIO_CMD_ACK   =  12;
  static constexpr uint8_t RX_RADIO_CMD_ACK   =  13;
  static constexpr uint8_t DEVICE_CMD_ERR     =  14;
  static constexpr uint8_t TX_RADIO_CMD_ERR   =  15;
  static constexpr uint8_t RX_RADIO_CMD_ERR   =  16;
  static constexpr uint8_t DEVICE_ENQ_RSP     =  17;
  static constexpr uint8_t TX_RADIO_ENQ_RSP   =  18;
  static constexpr uint8_t RX_RADIO_ENQ_RSP   =  19;
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
  static constexpr uint16_t REQUEST_ACQ       = (1u <<  0);  // bit 0
  static constexpr uint16_t TIME_PRESENT      = (1u <<  1);  // bit 1
  static constexpr uint16_t STREAM_ID_PRESENT = (1u <<  2);  // bit 2
//     bits 3-7: reserved for additional generally applicable settings
//     bits 8-15: reserved for settings specific to each packet type
  namespace RADIO_CMD {
    // bits 8-10 of TX_RADIO_CMD and RX_RADIO_CMD
    // NOTE: the values below make bit 8 a N_SAMPLES_PRESENT flag
    static constexpr uint16_t START_NOW_UNTIL_STOP         = (0u <<  8);  // bits 8-10 = 000
    static constexpr uint16_t START_AT_TIME_UNTIL_STOP     = (1u <<  8);  // bits 8-10 = 001; error if TIME_PRESENT is 0
    static constexpr uint16_t STOP_NOW                     = (3u <<  8);  // bits 8-10 = 011; stops anything going on 
    static constexpr uint16_t START_NOW_FOR_N_SAMPLES      = (4u <<  8);  // bits 8-10 = 100
    static constexpr uint16_t START_AT_TIME_FOR_N_SAMPLES  = (5u <<  8);  // bits 8-10 = 101; error if TIME_PRESENT is 0
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
} ack_packet;

typedef struct {
  packet_header           hdr;
  std::complex<int16_t>   data[(hdr.packet_size - sizeof(packet_header)) / sizeof(std::complex<int16_t>)]; // FIXME: find proper way to represent variable-mength data in struct
} data_packet;

#endif // VXSDR_HEADER_HPP