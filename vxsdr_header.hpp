#include <cstdint>
#include <complex>

#ifndef VXSDR_HEADER_HPP
#define VXSDR_HEADER_HPP

/*
  SDR packet definitions
  Version 0.2.2 31 Aug 2021
  FROZEN EXCEPT FOR ADDITIONS UNTIL INITIAL IMPLEMENTATION COMPLETE AND TESTED
*/

// Packet Types
#define  PACKET_TYPE_TX_SIGNAL_DATA            (0x00)
#define  PACKET_TYPE_RX_SIGNAL_DATA            (0x01)
#define  PACKET_TYPE_DEVICE_CMD                (0x02)
#define  PACKET_TYPE_TX_RADIO_CMD              (0x03)
#define  PACKET_TYPE_RX_RADIO_CMD              (0x04)
#define  PACKET_TYPE_STATUS_MSG                (0x05)
#define  PACKET_TYPE_DEVICE_CMD_ERR            (0x0A)
#define  PACKET_TYPE_TX_RADIO_CMD_ERR          (0x0B)
#define  PACKET_TYPE_RX_RADIO_CMD_ERR          (0x0C)
#define  PACKET_TYPE_DEVICE_CMD_RSP            (0x0D)
#define  PACKET_TYPE_TX_RADIO_CMD_RSP          (0x0E)
#define  PACKET_TYPE_RX_RADIO_CMD_RSP          (0x0F)
#define  PACKET_TYPE_TX_SIGNAL_DATA_ACK        (0x80)
#define  PACKET_TYPE_RX_SIGNAL_DATA_ACK        (0x81)
#define  PACKET_TYPE_DEVICE_CMD_ACK            (0x82)
#define  PACKET_TYPE_TX_RADIO_CMD_ACK          (0x83)
#define  PACKET_TYPE_RX_RADIO_CMD_ACK          (0x84)
#define  PACKET_TYPE_INVALID_PACKET            (0xFF)

// Flags
#define FLAGS_REQUEST_ACK                      (0x80U)
#define FLAGS_TIME_PRESENT                     (0x40U)
#define FLAGS_STREAM_ID_PRESENT                (0x20U)
//     bits 3-7: reserved for additional settings

// Device Commands
#define DEVICE_CMD_HELLO                       (0x00)
#define DEVICE_CMD_SET_TIME_NOW                (0x01)  // error if TIME_PRESENT is 0
#define DEVICE_CMD_SET_TIME_NEXT_PPS           (0x02)  // error if TIME_PRESENT is 0
#define DEVICE_CMD_GET_TIME                    (0x03)
#define DEVICE_CMD_GET_STATUS                  (0x04)
#define DEVICE_CMD_CLEAR_STATUS                (0x05)  // resets error flags and counters
#define DEVICE_CMD_GET_BUFFER_INFO             (0x06)  // returns info on how many buffers and their purpose
#define DEVICE_CMD_GET_BUFFER_USE              (0x07)  // asks for buffer info
#define DEVICE_CMD_OPEN_SESSION                (0x08)
#define DEVICE_CMD_CLOSE_SESSION               (0x09)
  // 0x0A-0x0F reserved
#define DEVICE_CMD_GET_TRANSPORT_INFO          (0x10)  // unimplemented // how many interfaces, what kind
#define DEVICE_CMD_GET_TRANSPORT_ADDR          (0x11)  // we'll use this for IP addresses
#define DEVICE_CMD_GET_TRANSPORT_PKTSIZ        (0x12)
  // 0x12-0x1F reserved for more get_transport stuff as needed
#define DEVICE_CMD_SET_TRANSPORT_ADDR          (0x21)
#define DEVICE_CMD_SET_TRANSPORT_PKTSIZ        (0x22)
#define DEVICE_CMD_SAVE_TRANSPORT_ADDR         (0x23)
  // 0x22-0x2F reserved for more set_transport stuff as needed
#define DEVICE_CMD_GET_CHANNEL_INFO            (0x30)  // unimplemented // how many, are they TX/RX, etc
  // 0x31-0x4F reserved for channel parameter get and set
#define DEVICE_CMD_GET_TIMING_INFO             (0x50)  // unimplemented // what timing inputs are available
#define DEVICE_CMD_GET_TIMING_STATUS           (0x51)  // PPS present/absent; 10 MHz locked/unlocked
#define DEVICE_CMD_GET_TIMING_REF              (0x52)  // unimplemented // current sources of 10 MHz and PPS
#define DEVICE_CMD_SET_TIMING_REF              (0x53)  // unimplemented // set sources of 10 MHz and PPS
//#define DEVICE_CMD_READ_DAC_REG                (0x60)
//#define DEVICE_CMD_WRITE_DAC_REG               (0x61)
//#define DEVICE_CMD_READ_ADC_REG                (0x62)
//#define DEVICE_CMD_WRITE_ADC_REG               (0x63)
// 0x64-0xFD reserved for stuff we haven't thought of yet
#define DEVICE_CMD_STOP                        (0xFE)  // unimplemented // stop all streaming and return to wait state within session
#define DEVICE_CMD_RESET                       (0xFF)  // unimplemented // do a power on reset (closing session and returning to idle)

// Device Command Errors
#define DEVICE_CMD_ERR_BAD_CMD                 (0x01)  // generic bad stuff happened; can substitute for more detailed ones until they're implemented
#define DEVICE_CMD_ERR_BUSY                    (0x02)  // can't accept that command because we're running
#define DEVICE_CMD_ERR_NO_SUCH_SUBDEVICE       (0x03)  // the specified subdevice doesn't exist
#define DEVICE_CMD_ERR_TIMEOUT                 (0x04)  // tried to do command but something didn't answer
#define DEVICE_CMD_ERR_BAD_PARAMETER           (0x05)  // command parameter is out of allowed range
#define DEVICE_CMD_ERR_OUT_OF_SEQUENCE         (0x06)  // command received with sequence error
#define DEVICE_CMD_ERR_DATA_BUFFER_OVERFLOW    (0x07)  // overflow data buffer
#define DEVICE_CMD_ERR_CTRL_BUFFER_OVERFLOW    (0x08)  // overflow control buffer

// Radio Commands
#define RADIO_CMD_START_NOW_UNTIL_STOP         (0x01)
#define RADIO_CMD_START_AT_TIME_UNTIL_STOP     (0x02)  // error if TIME_PRESENT is 0
#define RADIO_CMD_STOP_NOW                     (0x03)  // stops anything going on
#define RADIO_CMD_START_NOW_FOR_N_SAMPLES      (0x04)
#define RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES  (0x05)  // error if TIME_PRESENT is 0
  // 0x06-0x0F reserved
#define RADIO_CMD_GET_RF_INFO                  (0x10)  // unimplemented // available frequency, gain, sample rate, and bandwidth ranges, number of selectable I/Os
#define RADIO_CMD_GET_RF_FREQ                  (0x11)  // unimplemented
#define RADIO_CMD_GET_RF_GAIN                  (0x12)  // unimplemented
#define RADIO_CMD_GET_SAMPLE_RATE              (0x13)
#define RADIO_CMD_GET_RF_BW                    (0x14)  // unimplemented
#define RADIO_CMD_GET_RF_ENABLED               (0x15)  // unimplemented
#define RADIO_CMD_GET_RF_PORT                  (0x16)  // unimplemented
  // 0x17-0x20 reserved
#define RADIO_CMD_SET_RF_FREQ                  (0x21)
#define RADIO_CMD_SET_RF_GAIN                  (0x22)
#define RADIO_CMD_SET_SAMPLE_RATE              (0x23)  // FIXME: sets CIC interpolation/decimation rate (valid 1-128)
#define RADIO_CMD_SET_RF_BW                    (0x24)  // unimplemented
#define RADIO_CMD_SET_RF_ENABLED               (0x25)  // enables(0x1) or disables(0x0) RX/TX RF path
#define RADIO_CMD_SET_RF_PORT                  (0x26)  // FIXME: this selects input and output ports
  // 0x27-0x30 reserved
#define RADIO_CMD_GET_NUM_SUBDEVS              (0x31)  //unimplemented // return the number of subdevices
  // 0x32-0x40 reserved
#define RADIO_CMD_GET_IQ_BIAS                  (0x41)
#define RADIO_CMD_GET_IQ_CORR                  (0x42)
  // 0x43-0x50 reserved
#define RADIO_CMD_SET_IQ_BIAS                  (0x51)
#define RADIO_CMD_SET_IQ_CORR                  (0x52)
  // 0x53-0xFF reserved

// Radio Command Errors
#define RADIO_CMD_ERR_BAD_CMD                  (0x01)
#define RADIO_CMD_ERR_BUSY                     (0x02)
#define RADIO_CMD_ERR_NO_SUCH_SUBDEVICE        (0x03)
#define RADIO_CMD_ERR_TIMEOUT                  (0x04)
#define RADIO_CMD_ERR_BAD_PARAMETER            (0x05)
#define RADIO_CMD_ERR_DATA_UNDERFLOW           (0x06)
#define RADIO_CMD_ERR_DATA_OVERFLOW            (0x07)

// Capabilities
#define CAPABILITY_HAS_LOCK_DETECT             (0x00000001U)
#define CAPABILITY_HAS_EXTERNAL_REF            (0x00000002U)
#define CAPABILITY_HAS_AGC                     (0x00000004U)
#define CAPABILITY_HAS_LIMITER                 (0x00000008U)
#define CAPABILITY_HAS_LIMIT_DETECT            (0x00000010U)
#define CAPABILITY_HAS_MANUAL_IQ_OFFSET_CORR   (0x00000020U)
#define CAPABILITY_HAS_MANUAL_IQ_BALANCE_CORR  (0x00000040U)
#define CAPABILITY_HAS_AUTO_IQ_OFFSET_CORR     (0x00000080U)
#define CAPABILITY_HAS_AUTO_IQ_BALANCE_CORR    (0x00000100U)
#define CAPABILITY_HAS_TEMP_MEASURE            (0x00000200U)
#define CAPABILITY_HAS_POWER_MEASURE           (0x00000400U)
#define CAPABILITY_HAS_EXTERNAL_LO             (0x00000800U)

static constexpr size_t   MAX_DATA_LENGTH_SAMPLES  = 2048;
static constexpr size_t   MAX_PAYLOAD_LENGTH_BYTES = 32;  // maximum length of a CMD or RSP packet, excluding header

struct packet_header {
  uint8_t                  packet_type;
  uint8_t                  command;
  uint8_t                  target_subdevice;  // for cases where multiple devices exist (e.g. multiple network I/F or multichannel TX/RX)
  uint8_t                  flags;
  uint16_t                 packet_size;       // length of packet including header in bytes
  uint16_t                 sequence_counter;  // need to figure out when this resets (every new data stream, power up, ?)
};

struct time_spec_t {
  uint32_t                 nanoseconds;
  uint32_t                 seconds;
};

typedef uint64_t          stream_spec_t;

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
    stream_spec_t          stream_id;
    std::complex<int16_t>  data[MAX_DATA_LENGTH_SAMPLES];
};

class data_packet_time_stream : public packet {
  public:
    time_spec_t            time;
    stream_spec_t          stream_id;
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
     uint8_t               payload[MAX_PAYLOAD_LENGTH_BYTES] = { 0 };
};

class cmd_packet : public cmd_or_rsp_packet {};

class rsp_packet : public cmd_or_rsp_packet {};

class ack_packet : public cmd_or_rsp_packet {};

class status_packet : public cmd_or_rsp_packet {};

class stream_ctrl_packet : public cmd_packet {};

class stream_ctrl_packet_time : public stream_ctrl_packet {
  public:
    time_spec_t            time;
};

class stream_ctrl_packet_samples : public stream_ctrl_packet {
  public:
    uint64_t               n_samples = 0;
};

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

class radio_rf_info_packet : public cmd_or_rsp_packet {
  public:
    double                 min_freq = 0.0;
    double                 max_freq = 0.0;
    double                 min_gain = 0.0;
    double                 max_gain = 0.0;
    double                 min_rate = 0.0;
    double                 max_rate = 0.0;
    double                 min_bw   = 0.0;
    double                 max_bw   = 0.0;
    uint16_t               num_ports = 0;
    uint16_t               reserved = 0;
    uint32_t               capabilities = 0;
};

class radio_rf_freq_packet : public cmd_or_rsp_packet {
  public:
    double                 freq = 0.0;
    double                 rtol = 0.0;
};

class radio_rf_gain_packet : public cmd_or_rsp_packet {
  public:
    double                 gain = 0.0;
};

//FIXME: this needs to be a double, and the integer divisor needs to be figured
//       out by the MCU!!
class radio_sample_rate_packet : public cmd_or_rsp_packet {
  public:
    double                 rate = 0;
};

class radio_rf_enable_packet : public cmd_or_rsp_packet {
  public:
    uint32_t               enabled = 0; //TX/RX path enable (1 = on, 0 = off)
    uint32_t               reserved = 0;
};

class radio_iq_bias_packet : public cmd_or_rsp_packet {
  public:
    double                 i_bias = 0; // range = +/- 1 covering min to max
    double                 q_bias = 0;
};

class radio_iq_corr_packet : public cmd_or_rsp_packet {
  public:
    double                 ii_corr = 1;
    double                 iq_corr = 0;
    double                 qi_corr = 0;
    double                 qq_corr = 1;
};

#endif // VXSDR_HEADER_HPP
