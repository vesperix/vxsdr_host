#include <complex>
#include <cstdint>

#ifndef VXSDR_HEADER_H
#define VXSDR_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#include <stdint.h>

/*
  VXSDR packet definitions
  Version 0.9.2 30 Oct 2021
*/

// Packet Types
//   TX data is sent only from a host to a radio
#define PACKET_TYPE_TX_SIGNAL_DATA     (0x00U)
//   RX data is sent only from a radio to a host
#define PACKET_TYPE_RX_SIGNAL_DATA     (0x01U)
//   Commands are sent only from a host to a radio, and may
//   have responses or acks, depending on the command
#define PACKET_TYPE_DEVICE_CMD         (0x02U)
#define PACKET_TYPE_TX_RADIO_CMD       (0x03U)
#define PACKET_TYPE_RX_RADIO_CMD       (0x04U)
//   Async messages are sent by a radio only, and are not
//   responses to a host message
#define PACKET_TYPE_ASYNC_MSG          (0x05U)

// indicators are applied to each packet type to indicate responses, errors, and acks
// they occupy the top 2 bits of the packet type
#define PACKET_ERR_INDICATOR           (0x40U)
#define PACKET_RSP_INDICATOR           (0x60U)
#define PACKET_ACK_INDICATOR           (0x80U)
#define PACKET_INDICATOR_MASK          (0xC0U)
#define PACKET_TYPE_MASK               (0x3FU)

// convenience macros to turn a command into an err, rsp, or ack
#define PACKET_TYPE_MAKE_ERR(byte)     (((byte)&PACKET_TYPE_MASK) | PACKET_ERR_INDICATOR)
#define PACKET_TYPE_MAKE_RSP(byte)     (((byte)&PACKET_TYPE_MASK) | PACKET_RSP_INDICATOR)
#define PACKET_TYPE_MAKE_ACK(byte)     (((byte)&PACKET_TYPE_MASK) | PACKET_ACK_INDICATOR)

//   Errors, responses, and acks are sent by a radio only,
//   and only in response to a host packet
#define PACKET_TYPE_DEVICE_CMD_ERR     PACKET_TYPE_MAKE_ERR(PACKET_TYPE_DEVICE_CMD)
#define PACKET_TYPE_TX_RADIO_CMD_ERR   PACKET_TYPE_MAKE_ERR(PACKET_TYPE_TX_RADIO_CMD)
#define PACKET_TYPE_RX_RADIO_CMD_ERR   PACKET_TYPE_MAKE_ERR(PACKET_TYPE_RX_RADIO_CMD)

#define PACKET_TYPE_DEVICE_CMD_RSP     PACKET_TYPE_MAKE_RSP(PACKET_TYPE_DEVICE_CMD)
#define PACKET_TYPE_TX_RADIO_CMD_RSP   PACKET_TYPE_MAKE_RSP(PACKET_TYPE_TX_RADIO_CMD)
#define PACKET_TYPE_RX_RADIO_CMD_RSP   PACKET_TYPE_MAKE_RSP(PACKET_TYPE_RX_RADIO_CMD)

#define PACKET_TYPE_TX_SIGNAL_DATA_ACK PACKET_TYPE_MAKE_ACK(PACKET_TYPE_TX_SIGNAL_DATA)
#define PACKET_TYPE_RX_SIGNAL_DATA_ACK PACKET_TYPE_MAKE_ACK(PACKET_TYPE_RX_SIGNAL_DATA)
#define PACKET_TYPE_DEVICE_CMD_ACK     PACKET_TYPE_MAKE_ACK(PACKET_TYPE_DEVICE_CMD)
#define PACKET_TYPE_TX_RADIO_CMD_ACK   PACKET_TYPE_MAKE_ACK(PACKET_TYPE_TX_RADIO_CMD)
#define PACKET_TYPE_RX_RADIO_CMD_ACK   PACKET_TYPE_MAKE_ACK(PACKET_TYPE_RX_RADIO_CMD)

// nothing should ever send this
#define PACKET_TYPE_INVALID_PACKET     (0xFFU)

// Flags
#define FLAGS_REQUEST_ACK              (0x80U)
#define FLAGS_TIME_PRESENT             (0x40U)
#define FLAGS_STREAM_ID_PRESENT        (0x20U)
//     bits 3-7: reserved for additional settings

// Device Commands
#define DEVICE_CMD_HELLO                      (0x00)
#define DEVICE_CMD_SET_TIME_NOW               (0x01)  // error if TIME_PRESENT is 0
#define DEVICE_CMD_SET_TIME_NEXT_PPS          (0x02)  // error if TIME_PRESENT is 0
#define DEVICE_CMD_GET_TIME                   (0x03)
#define DEVICE_CMD_GET_STATUS                 (0x04)
#define DEVICE_CMD_CLEAR_STATUS               (0x05)  // resets error flags and counters
#define DEVICE_CMD_GET_BUFFER_INFO            (0x06)  // info on how many buffers and their purpose
#define DEVICE_CMD_GET_BUFFER_USE             (0x07)  // asks for buffer info
// 0x09-0x0F reserved
#define DEVICE_CMD_GET_TRANSPORT_INFO         (0x10)  // unimplemented // how many interfaces, what kind
#define DEVICE_CMD_GET_TRANSPORT_ADDR         (0x11)
#define DEVICE_CMD_GET_MAX_PAYLOAD            (0x12)
#define DEVICE_CMD_CLEAR_DATA_BUFFER          (0x13)
// 0x14-0x1F reserved for more get_transport stuff as needed
#define DEVICE_CMD_DISCOVER                   (0x20)
#define DEVICE_CMD_SET_TRANSPORT_ADDR         (0x21)
#define DEVICE_CMD_SET_MAX_PAYLOAD            (0x22)
#define DEVICE_CMD_SAVE_TRANSPORT_ADDR        (0x23)
// 0x24-0x2F reserved for more set_transport stuff as needed
#define DEVICE_CMD_GET_CHANNEL_INFO           (0x30)  // unimplemented // how many, are they TX/RX, etc
// 0x31-0x4F reserved for channel parameter get and set
#define DEVICE_CMD_GET_TIMING_INFO            (0x50)  // unimplemented // what timing inputs are available
#define DEVICE_CMD_GET_TIMING_STATUS          (0x51)  // PPS present/absent; 10 MHz locked/unlocked
#define DEVICE_CMD_GET_TIMING_REF             (0x52)  // unimplemented // current sources of 10 MHz and PPS
#define DEVICE_CMD_SET_TIMING_REF             (0x53)  // unimplemented // set sources of 10 MHz and PPS
#define DEVICE_CMD_GET_TIMING_RESOLUTION      (0x54)
            // these may be removed before release
#define DEVICE_CMD_READ_DAC_REG               (0x60)
#define DEVICE_CMD_WRITE_DAC_REG              (0x61)
#define DEVICE_CMD_READ_ADC_REG               (0x62)
#define DEVICE_CMD_WRITE_ADC_REG              (0x63)
// 0x64-0x9F reserved
#define DEVICE_CMD_APP_UPDATE_MODE_SET        (0xA0)
#define DEVICE_CMD_APP_UPDATE_DATA            (0xA1)
#define DEVICE_CMD_APP_UPDATE_DONE            (0xA2)
// 0xA3-0xFD reserved for stuff we haven't thought of yet
#define DEVICE_CMD_STOP                       (0xFE)  // stop all streaming and return to wait state within session
#define DEVICE_CMD_RESET                      (0xFF)  // do a power on reset (closing session and returning to idle)

// Generic Errors used for Device Commands and Radio Commands
#define ERR_NO_ERROR                          (0x00)  // no error
#define ERR_BAD_COMMAND                       (0x01)  // generic bad stuff happened; can substitute for more detailed ones until they're implemented
#define ERR_BUSY                              (0x02)  // can't accept that command because we're running
#define ERR_NO_SUCH_SUBDEVICE                 (0x03)  // the specified subdevice doesn't exist
#define ERR_TIMEOUT                           (0x04)  // tried to do command but something didn't answer
#define ERR_BAD_PARAMETER                     (0x05)  // command parameter is out of allowed range
#define ERR_NOT_SUPPORTED                     (0x06)  // this device can't do that command
#define ERR_BAD_PKT_SIZE                      (0x07)
#define ERR_FAILED                            (0x08)  // should have worked, but didn't

// Device Command Errors
#define DEVICE_CMD_ERR_BAD_CMD                ERR_BAD_COMMAND
#define DEVICE_CMD_ERR_BUSY                   ERR_BUSY
#define DEVICE_CMD_ERR_NO_SUCH_SUBDEVICE      ERR_NO_SUCH_SUBDEVICE
#define DEVICE_CMD_ERR_TIMEOUT                ERR_TIMEOUT
#define DEVICE_CMD_ERR_BAD_PARAMETER          ERR_BAD_PARAMETER
#define DEVICE_CMD_ERR_NOT_SUPPORTED          ERR_NOT_SUPPORTED
#define DEVICE_CMD_ERR_BAD_PKT_SIZE           ERR_BAD_PKT_SIZE
#define DEVICE_CMD_ERR_FAILED                 ERR_FAILED

// Radio Commands
#define RADIO_CMD_START_NOW_UNTIL_STOP        (0x01)
#define RADIO_CMD_START_AT_TIME_UNTIL_STOP    (0x02)  // error if TIME_PRESENT is 0
#define RADIO_CMD_STOP_NOW                    (0x03)  // stops anything going on
#define RADIO_CMD_START_NOW_FOR_N_SAMPLES     (0x04)
#define RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES (0x05)  // error if TIME_PRESENT is 0
// 0x06 - 0x10 reserved
#define RADIO_CMD_GET_RF_FREQ                 (0x11)
#define RADIO_CMD_GET_RF_GAIN                 (0x12)
#define RADIO_CMD_GET_SAMPLE_RATE             (0x13)
#define RADIO_CMD_GET_RF_BW                   (0x14)  // unimplemented
#define RADIO_CMD_GET_RF_ENABLED              (0x15)
#define RADIO_CMD_GET_RF_PORT                 (0x16)
#define RADIO_CMD_GET_NUM_RF_PORTS            (0x17)
#define RADIO_CMD_GET_RF_PORT_NAME            (0x18)
#define RADIO_CMD_GET_LO_INPUT                (0x19)  // unimplemented
#define RADIO_CMD_GET_LOCK_STATUS             (0x1A)
// 0x1B - 0x1F reserved
#define RADIO_CMD_GET_MASTER_CLK              (0x20)  // unimplemented
#define RADIO_CMD_SET_RF_FREQ                 (0x21)
#define RADIO_CMD_SET_RF_GAIN                 (0x22)
#define RADIO_CMD_SET_SAMPLE_RATE             (0x23)
#define RADIO_CMD_SET_RF_BW                   (0x24)  // unimplemented
#define RADIO_CMD_SET_RF_ENABLED              (0x25)
#define RADIO_CMD_SET_RF_PORT                 (0x26)
#define RADIO_CMD_SET_LO_INPUT                (0x29)  // unimplemented
// 0x2A - 0x2F reserved
#define RADIO_CMD_SET_MASTER_CLK              (0x30)  // unimplemented
#define RADIO_CMD_GET_NUM_SUBDEVS             (0x31)
#define RADIO_CMD_GET_CAPABILITIES            (0x32)
#define RADIO_CMD_GET_RF_FREQ_RANGE           (0x33)
#define RADIO_CMD_GET_RF_GAIN_RANGE           (0x34)
#define RADIO_CMD_GET_SAMPLE_RATE_RANGE       (0x35)
// 0x36 - 0x3F reserved
#define RADIO_CMD_SET_ADC_TEST_PATTERN        (0x40)
#define RADIO_CMD_GET_IQ_BIAS                 (0x41)
#define RADIO_CMD_GET_IQ_CORR                 (0x42)
// 0x43 - 0x50 reserved
#define RADIO_CMD_SET_IQ_BIAS                 (0x51)
#define RADIO_CMD_SET_IQ_CORR                 (0x52)
// 0x53 - 0xFF reserved

// Radio Command Errors
#define RADIO_CMD_ERR_BAD_CMD                 ERR_BAD_COMMAND
#define RADIO_CMD_ERR_BUSY                    ERR_BUSY
#define RADIO_CMD_ERR_NO_SUCH_SUBDEVICE       ERR_NO_SUCH_SUBDEVICE
#define RADIO_CMD_ERR_TIMEOUT                 ERR_TIMEOUT
#define RADIO_CMD_ERR_BAD_PARAMETER           ERR_BAD_PARAMETER
#define RADIO_CMD_ERR_NOT_SUPPORTED           ERR_NOT_SUPPORTED
#define RADIO_CMD_ERR_BAD_PKT_SIZE            ERR_BAD_PKT_SIZE
#define RADIO_CMD_ERR_FAILED                  ERR_FAILED

// Async messages
#define ASYNC_NO_ERROR                        (0x00)
#define ASYNC_DATA_UNDERFLOW                  (0x01)
#define ASYNC_DATA_OVERFLOW                   (0x02)
#define ASYNC_OVER_TEMP                       (0x03)  // too hot
#define ASYNC_POWER_ERROR                     (0x04)  // voltage low/current high
#define ASYNC_FREQ_ERROR                      (0x05)  // loss of lock, etc
#define ASYNC_OUT_OF_SEQUENCE                 (0x06)
#define ASYNC_CTRL_OVERFLOW                   (0x07)
#define ASYNC_PPS_TIMEOUT                     (0x08)
// 0x06-0xFF reserved

// Capabilities
#define CAPABILITY_HAS_LOCK_DETECT            (0x1ULL)
#define CAPABILITY_HAS_EXTERNAL_REF           (0x2ULL)
#define CAPABILITY_HAS_EXTERNAL_PPS           (0x4ULL)
#define CAPABILITY_HAS_AGC                    (0x8ULL)
#define CAPABILITY_HAS_LIMITER                (0x10ULL)
#define CAPABILITY_HAS_LIMIT_DETECT           (0x20ULL)
#define CAPABILITY_HAS_MANUAL_IQ_BIAS_CORR    (0x40ULL)
#define CAPABILITY_HAS_MANUAL_IQ_BALANCE_CORR (0x80ULL)
#define CAPABILITY_HAS_AUTO_IQ_BIAS_CORR      (0x100ULL)
#define CAPABILITY_HAS_AUTO_IQ_BALANCE_CORR   (0x200ULL)
#define CAPABILITY_HAS_TEMP_MEASURE           (0x400ULL)
#define CAPABILITY_HAS_POWER_MEASURE          (0x800ULL)
#define CAPABILITY_HAS_EXTERNAL_LO_INPUT      (0x1000ULL)
#define CAPABILITY_HAS_ADJUSTABLE_RF_BW       (0x2000ULL)
#define CAPABILITY_HAS_ADJUSTABLE_IF_BW       (0x4000ULL)
#define CAPABILITY_HAS_ADJUSTABLE_MASTER_CLK  (0x8000ULL)

#define MAX_DATA_LENGTH_SAMPLES               (2048UL)
#define MAX_PAYLOAD_LENGTH_BYTES              (32UL)  // maximum length of a CMD or RSP packet, excluding header

#define PACKED                                __attribute__((packed))

typedef struct PACKED {
    uint8_t packet_type;
    uint8_t command;
    uint8_t target_subdevice;  // for cases where multiple devices exist (e.g. multiple network I/F or multichannel TX/RX)
    uint8_t flags;
    uint16_t packet_size;       // length of packet including header in bytes
    uint16_t sequence_counter;  // need to figure out when this resets (every new data stream, power up, ?)
} packet_header;

typedef struct PACKED {
    uint32_t seconds;
    uint32_t nanoseconds;
} time_spec_t;

typedef uint64_t stream_spec_t;

typedef uint64_t capabilities_t;

#ifdef __cplusplus
}

// The remainder is entirely C++ class definitions

class PACKED packet {
  public:
    packet_header hdr;
};

class PACKED header_only_packet : public packet {};

class PACKED async_msg_packet : public packet {};

class one_uint32_packet : public packet {
  public:
    uint32_t value1   = 0;
    uint32_t reserved = 0;
};

typedef one_uint32_packet error_packet;

class PACKED two_uint32_packet : public packet {
  public:
    uint32_t value1 = 0;
    uint32_t value2 = 0;
};

class PACKED four_uint32_packet : public packet {
  public:
    uint32_t value1 = 0;
    uint32_t value2 = 0;
    uint32_t value3 = 0;
    uint32_t value4 = 0;
};

class PACKED six_uint32_packet : public packet {
  public:
    uint32_t value1 = 0;
    uint32_t value2 = 0;
    uint32_t value3 = 0;
    uint32_t value4 = 0;
    uint32_t value5 = 0;
    uint32_t value6 = 0;
};

class PACKED eight_uint32_packet : public packet {
  public:
    uint32_t value1 = 0;
    uint32_t value2 = 0;
    uint32_t value3 = 0;
    uint32_t value4 = 0;
    uint32_t value5 = 0;
    uint32_t value6 = 0;
    uint32_t value7 = 0;
    uint32_t value8 = 0;
};

class PACKED one_double_packet : public packet {
  public:
    double value1 = 0.0;
};

class PACKED two_double_packet : public packet {
  public:
    double value1 = 0.0;
    double value2 = 0.0;
};

class PACKED four_double_packet : public packet {
  public:
    double value1 = 0.0;
    double value2 = 0.0;
    double value3 = 0.0;
    double value4 = 0.0;
};

class PACKED one_uint64_packet : public packet {
  public:
    uint64_t value1 = 0;
};

class PACKED name_packet : public packet {
  public:
    char name[MAX_PAYLOAD_LENGTH_BYTES] = {0};
};

class PACKED data_packet : public packet {
  public:
    std::complex<int16_t> data[MAX_DATA_LENGTH_SAMPLES];
};

class PACKED data_packet_time : public packet {
  public:
    time_spec_t time;
    std::complex<int16_t> data[MAX_DATA_LENGTH_SAMPLES];
};

class PACKED data_packet_stream : public packet {
  public:
    stream_spec_t stream_id;
    std::complex<int16_t> data[MAX_DATA_LENGTH_SAMPLES];
};

class PACKED data_packet_time_stream : public packet {
  public:
    time_spec_t time;
    stream_spec_t stream_id;
    std::complex<int16_t> data[MAX_DATA_LENGTH_SAMPLES];
};

class largest_data_packet : public data_packet_time_stream {};

class PACKED cmd_or_rsp_packet_time : public packet {
  public:
    time_spec_t time;
};

class PACKED cmd_or_rsp_packet_time_stream : public packet {
  public:
    time_spec_t time;
    stream_spec_t stream_id;
};

class PACKED cmd_or_rsp_packet_samples : public packet {
  public:
    uint64_t n_samples = 0;
};

class PACKED cmd_or_rsp_packet_time_samples : public packet {
  public:
    time_spec_t time;
    uint64_t n_samples = 0;
};

class PACKED largest_cmd_or_rsp_packet : public cmd_or_rsp_packet_time_stream {
    [[maybe_unused]] uint8_t payload[MAX_PAYLOAD_LENGTH_BYTES] = {0};
};

#endif  // __cplusplus

#endif  // VXSDR_HEADER_H
