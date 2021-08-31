#include <cstdint>
#include <complex>

#ifndef VXSDR_HEADER_HPP
#define VXSDR_HEADER_HPP

/*
  SDR packet definitions
  Version 0.2.2 31 Aug 2021
  FROZEN EXCEPT FOR ADDITIONS UNTIL INITIAL IMPLEMENTATION COMPLETE AND TESTED
*/

namespace PACKET_TYPES {
  // packet types
  static constexpr uint8_t TX_SIGNAL_DATA     = 0x00;
  static constexpr uint8_t RX_SIGNAL_DATA     = 0x01;
  static constexpr uint8_t DEVICE_CMD         = 0x02;
  static constexpr uint8_t TX_RADIO_CMD       = 0x03;
  static constexpr uint8_t RX_RADIO_CMD       = 0x04;
  static constexpr uint8_t STATUS_MSG         = 0x05;
  static constexpr uint8_t DEVICE_CMD_ERR     = 0x0A;
  static constexpr uint8_t TX_RADIO_CMD_ERR   = 0x0B;
  static constexpr uint8_t RX_RADIO_CMD_ERR   = 0x0C;
  static constexpr uint8_t DEVICE_CMD_RSP     = 0x0D;
  static constexpr uint8_t TX_RADIO_CMD_RSP   = 0x0E;
  static constexpr uint8_t RX_RADIO_CMD_RSP   = 0x0F;
  static constexpr uint8_t TX_SIGNAL_DATA_ACK = 0x80;
  static constexpr uint8_t RX_SIGNAL_DATA_ACK = 0x81;
  static constexpr uint8_t DEVICE_CMD_ACK     = 0x82;
  static constexpr uint8_t TX_RADIO_CMD_ACK   = 0x83;
  static constexpr uint8_t RX_RADIO_CMD_ACK   = 0x84;
  static constexpr uint8_t INVALID_PACKET     = 0xFF;
}

typedef struct {
  uint8_t                 packet_type = PACKET_TYPES::INVALID_PACKET;
  uint8_t                 command = 0;
  uint8_t                 target_subdevice = 0;  // for cases where multiple devices exist (e.g. multiple network I/F or multichannel TX/RX)
  uint8_t                 flags = 0;
  uint16_t                packet_size = 0;       // length of packet including header in bytes
  uint16_t                sequence_counter = 0;  // need to figure out when this resets (every new data stream, power up, ?)
} packet_header;

typedef struct {
  uint32_t                nanoseconds = 0;
  uint32_t                seconds = 0;
} time_spec_t;

typedef uint64_t          stream_spec_t;

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
}

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
  static constexpr uint8_t GET_TRANSPORT_INFO           = 0x10;  // unimplemented // how many interfaces, what kind
  static constexpr uint8_t GET_TRANSPORT_ADDR           = 0x11;  // we'll use this for IP addresses
  static constexpr uint8_t GET_TRANSPORT_PKTSIZ         = 0x12;
  // 0x12-0x1F reserved for more get_transport stuff as needed
  static constexpr uint8_t SET_TRANSPORT_ADDR           = 0x21;
  static constexpr uint8_t SET_TRANSPORT_PKTSIZ         = 0x22;
  static constexpr uint8_t SAVE_TRANSPORT_ADDR          = 0x23;
  // 0x22-0x2F reserved for more set_transport stuff as needed
  static constexpr uint8_t GET_CHANNEL_INFO             = 0x30;  // unimplemented // how many, are they TX/RX, etc
  // 0x31-0x4F reserved for channel parameter get and set
  static constexpr uint8_t GET_TIMING_INFO              = 0x50;  // unimplemented // what timing inputs are available
  static constexpr uint8_t GET_TIMING_STATUS            = 0x51;  // PPS present/absent; 10 MHz locked/unlocked
  static constexpr uint8_t GET_TIMING_REF               = 0x52;  // unimplemented // current sources of 10 MHz and PPS
  static constexpr uint8_t SET_TIMING_REF               = 0x53;  // unimplemented // set sources of 10 MHz and PPS
  static constexpr uint8_t READ_DAC_REG                 = 0x60;
  static constexpr uint8_t WRITE_DAC_REG                = 0x61;
  static constexpr uint8_t READ_ADC_REG                 = 0x62;
  static constexpr uint8_t WRITE_ADC_REG                = 0x63;

  // 0x64-0xFD reserved for stuff we haven't thought of yet
  static constexpr uint8_t STOP                         = 0xFE;  // unimplemented // stop all streaming and return to wait state within session
  static constexpr uint8_t RESET                        = 0xFF;  // unimplemented // do a power on reset (closing session and returning to idle)
}

namespace DEVICE_CMD_RSP {
  static constexpr uint8_t HELLO_RSP                    = 0x00;
  static constexpr uint8_t GET_TIME_RSP                 = 0x03;
  static constexpr uint8_t GET_STATUS_RSP               = 0x04;
  static constexpr uint8_t GET_BUFFER_INFO_RSP          = 0x06;
  static constexpr uint8_t GET_BUFFER_USE_RSP           = 0x07;
  static constexpr uint8_t OPEN_SESSION_RSP             = 0x08;
  static constexpr uint8_t CLOSE_SESSION_RSP            = 0x09;
  static constexpr uint8_t GET_TRANSPORT_INFO_RSP       = 0x10; // fixed to match command
  static constexpr uint8_t GET_TRANSPORT_ADDR_RSP       = 0x11; // fixed to match command
  static constexpr uint8_t GET_TRANSPORT_PKTSIZ_RSP     = 0x12; // fixed to match command
  static constexpr uint8_t GET_CHANNEL_INFO_RSP         = 0x30;
  static constexpr uint8_t GET_TIMING_INFO_RSP          = 0x50;
  static constexpr uint8_t GET_TIMING_STATUS_RSP        = 0x51;
  static constexpr uint8_t GET_TIMING_REF_RSP           = 0x52;
  static constexpr uint8_t READ_DAC_REG_RSP             = 0x60;
  static constexpr uint8_t READ_ADC_REG_RSP             = 0x62;
}

namespace DEVICE_CMD_ERR {
  static constexpr uint8_t BAD_CMD                      = 0x01;  // generic bad stuff happened; can substitute for more detailed ones until they're implemented
  static constexpr uint8_t BUSY                         = 0x02;  // can't accept that command because we're running
  static constexpr uint8_t NO_SUCH_SUBDEVICE            = 0x03;  // the specified subdevice doesn't exist
  static constexpr uint8_t TIMEOUT                      = 0x04;  // tried to do command but something didn't answer
  static constexpr uint8_t BAD_PARAMETER                = 0x05;  // command parameter is out of allowed range
  static constexpr uint8_t OUT_OF_SEQUENCE              = 0x06;  // command received with sequence error
  static constexpr uint8_t DATA_BUFFER_OVERFLOW         = 0x07;  // overflow data buffer
  static constexpr uint8_t CTRL_BUFFER_OVERFLOW         = 0x08;  // overflow control buffer
}

namespace RADIO_CMD {
  static constexpr uint8_t START_NOW_UNTIL_STOP         = 0x01;
  static constexpr uint8_t START_AT_TIME_UNTIL_STOP     = 0x02;  // error if TIME_PRESENT is 0
  static constexpr uint8_t STOP_NOW                     = 0x03;  // stops anything going on
  static constexpr uint8_t START_NOW_FOR_N_SAMPLES      = 0x04;
  static constexpr uint8_t START_AT_TIME_FOR_N_SAMPLES  = 0x05;  // error if TIME_PRESENT is 0
  // 0x06-0x0F reserved
  static constexpr uint8_t GET_RF_INFO                  = 0x10;  // unimplemented // available frequency, gain, sample rate, and bandwidth ranges, number of selectable I/Os
  static constexpr uint8_t GET_RF_FREQ                  = 0x11;  // unimplemented
  static constexpr uint8_t GET_RF_GAIN                  = 0x12;  // unimplemented
  static constexpr uint8_t GET_SAMPLE_RATE              = 0x13;
  static constexpr uint8_t GET_RF_BW                    = 0x14;  // unimplemented
  static constexpr uint8_t GET_RF_ENABLED               = 0x15;  // unimplemented
  static constexpr uint8_t GET_RF_PORT                  = 0x16;  // unimplemented
  // 0x17-0x20 reserved
  static constexpr uint8_t SET_RF_FREQ                  = 0x21;
  static constexpr uint8_t SET_RF_GAIN                  = 0x22;
  static constexpr uint8_t SET_SAMPLE_RATE              = 0x23;  // sets CIC interpolation/decimation rate (valid 1-128)
  static constexpr uint8_t SET_RF_BW                    = 0x24;  // unimplemented
  static constexpr uint8_t SET_RF_ENABLED               = 0x25;  // enables(0x1) or disables(0x0) RX/TX RF path
  static constexpr uint8_t SET_RF_PORT                  = 0x26;  // FIXME: this selects input and output ports
  // 0x27-0xFF reserved
}

namespace RADIO_CMD_RSP {
  static constexpr uint8_t GET_RF_INFO_RSP              = 0x10;
  static constexpr uint8_t GET_RF_FREQ_RSP              = 0x11;
  static constexpr uint8_t GET_RF_GAIN_RSP              = 0x12;
  static constexpr uint8_t GET_SAMPLE_RATE_RSP          = 0x13;
  static constexpr uint8_t GET_RF_BW_RSP                = 0x14;
  static constexpr uint8_t GET_RF_ENABLED_RSP           = 0x15;
  static constexpr uint8_t GET_RF_PORT_RSP              = 0x16;
}

namespace RADIO_CMD_ERR {
  static constexpr uint8_t BAD_CMD                      = 0x01;
  static constexpr uint8_t BUSY                         = 0x02;
  static constexpr uint8_t NO_SUCH_SUBDEVICE            = 0x03;
  static constexpr uint8_t TIMEOUT                      = 0x04;
  static constexpr uint8_t BAD_PARAMETER                = 0x05;
}

static constexpr size_t   MAX_DATA_LENGTH_SAMPLES  = 2048;
static constexpr size_t   MAX_PAYLOAD_LENGTH_BYTES = 32;  // maximum length of a CMD or RSP packet, excluding header

class packet {
  public:
    packet_header          hdr;
};

//FIXME: Need to add number of samples so that we don't always use MAX_DATA_LENGTH_SAMPLES!

class data_packet : public packet {
  public:
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class data_packet_time : public packet {
  public:
    time_spec_t            time;
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class data_packet_stream : public packet {
  public:
    stream_spec_t          stream_id = 0;
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class data_packet_time_stream : public packet {
  public:
    time_spec_t            time;
    stream_spec_t          stream_id = 0;
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class largest_data_packet : public data_packet_time_stream {};

class cmd_or_rsp_packet : public packet {};

class cmd_or_rsp_packet_time : public cmd_or_rsp_packet {
  public:
    time_spec_t            time;
};

class cmd_or_rsp_packet_time_stream : public cmd_or_rsp_packet {
  public:
    time_spec_t            time;
    stream_spec_t          stream_id;
};

class largest_cmd_or_rsp_packet : public cmd_or_rsp_packet_time_stream {
    [[maybe_unused]]
    uint8_t                payload[MAX_PAYLOAD_LENGTH_BYTES];
};

class cmd_packet : public cmd_or_rsp_packet {};

class rsp_packet : public cmd_or_rsp_packet {};

class ack_packet : public cmd_or_rsp_packet {};

class status_packet : public cmd_or_rsp_packet {};

class stream_ctrl_packet : public cmd_packet {};

class stream_ctrl_packet_time_samples : public stream_ctrl_packet {
  public:
    time_spec_t            time;
    uint64_t               n_samples = 0;
};

class tx_signal_data_ack_packet : public ack_packet {
  public:
    uint64_t               requester_seq_num = 0;
    uint32_t               tx_buffer_used_bytes = 0;
    uint32_t               sequence_error_count = 0;
};

class get_buffer_use_rsp_packet : public rsp_packet {
  public:
    uint32_t               rx_buffer_used_bytes = 0;
    uint32_t               tx_buffer_used_bytes = 0;
};

class get_buffer_info_rsp_packet : public rsp_packet {
  public:
    uint32_t               rx_buffer_size_bytes = 0;
    uint32_t               tx_buffer_size_bytes = 0;
};

class hello_rsp_packet : public rsp_packet {
  public:
    uint32_t               mcu_sw_version = 0;
    uint32_t               fpga_fw_version = 0;
};

class status_rsp_packet : public rsp_packet {
  public:
    uint16_t               fpga_temperature = 0;
    uint16_t               dac_temperature = 0;
    uint32_t               reserved = 0;
    uint32_t               sequence_error_count = 0;
    uint32_t               data_flow_error_flags = 0;
    uint32_t               ddr_arbiter_status = 0;
    uint32_t               ethernet_status = 0;
};

class dev_read_write_reg_packet : public cmd_or_rsp_packet {
  public:
    uint32_t               register_number = 0;
    uint32_t               value = 0;
};

class transport_addr_packet : public cmd_or_rsp_packet {
  public:
    uint32_t               ip_addr = 0;
    uint32_t               reserved = 0;
};

class transport_pktsiz_packet : public cmd_or_rsp_packet {
  public:
    uint32_t               packet_size_bytes = 0;  // must be multiple of 8 bytes, min = 1024, max = 8216
    uint32_t               reserved = 0;
};

class dataflow_status_packet : public status_packet {
  public:
    uint16_t               tx_radio_status = 0;
    uint16_t               rx_radio_status = 0;
    uint32_t               device_status = 0;
};

class radio_rf_freq_packet : public cmd_or_rsp_packet {
  public:
    double                 freq = 0.0;
    double                 rtol = 0.0;
};

//negative gain value sets attenuator (0 to -15.5dB) parameter range [0 to -31]
//FIXME: the description below for positive values appears to be reversed
//       (63 = min gain, 0 = max gain) in board testing
//(RX only) positive gain value sets VGA gain (0 to 31.5dB) paramter range [0 to 63]
class radio_rf_gain_packet : public cmd_or_rsp_packet {
  public:
    double                 gain = 0.0;
};

class radio_rf_sample_rate_packet : public cmd_or_rsp_packet {
  public:
    uint32_t               rate = 0;
    uint32_t               reserved = 0;
};

class radio_rf_enable_packet : public cmd_or_rsp_packet {
  public:
    uint32_t               enabled = 0; //TX/RX path enable (1 = on, 0 = off)
    uint32_t               reserved = 0;
};

#endif // VXSDR_HEADER_HPP
