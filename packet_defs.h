#ifndef PACKET_DEFS_H
#define PACKET_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdint.h>

/*
  VXSDR packet definitions
  Version 0.9.1 26 Oct 2021
*/

// Packet Types
//   TX data is sent only from a host to a radio
#define PACKET_TYPE_TX_SIGNAL_DATA             (0x00)
//   RX data is sent only from a radio to a host
#define PACKET_TYPE_RX_SIGNAL_DATA             (0x01)
//   Commands are sent only from a host to a radio, and may
//   have responses or acks, depending on the command
#define PACKET_TYPE_DEVICE_CMD                 (0x02)
#define PACKET_TYPE_TX_RADIO_CMD               (0x03)
#define PACKET_TYPE_RX_RADIO_CMD               (0x04)
//   Async messages are sent by a radio only, and are not
//   responses to a host message
#define  PACKET_TYPE_ASYNC_MSG                 (0x05)
//   Errors, responses, and acks are sent by a radio only,
//   and only in response to a host packet
#define PACKET_TYPE_DEVICE_CMD_ERR             (0x0A)
#define PACKET_TYPE_TX_RADIO_CMD_ERR           (0x0B)
#define PACKET_TYPE_RX_RADIO_CMD_ERR           (0x0C)
#define PACKET_TYPE_DEVICE_CMD_RSP             (0x0D)
#define PACKET_TYPE_TX_RADIO_CMD_RSP           (0x0E)
#define PACKET_TYPE_RX_RADIO_CMD_RSP           (0x0F)
#define PACKET_TYPE_TX_SIGNAL_DATA_ACK         (0x80)
#define PACKET_TYPE_RX_SIGNAL_DATA_ACK         (0x81)
#define PACKET_TYPE_DEVICE_CMD_ACK             (0x82)
#define PACKET_TYPE_TX_RADIO_CMD_ACK           (0x83)
#define PACKET_TYPE_RX_RADIO_CMD_ACK           (0x84)
// nothing should ever send this
#define PACKET_TYPE_INVALID_PACKET             (0xFF)

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
#define DEVICE_CMD_GET_BUFFER_INFO             (0x06)  // info on how many buffers and their purpose
#define DEVICE_CMD_GET_BUFFER_USE              (0x07)  // asks for buffer info
#define DEVICE_CMD_OPEN_SESSION                (0x08)  // unimplemented
#define DEVICE_CMD_CLOSE_SESSION               (0x09)  // unimplemented
  // 0x0A-0x0F reserved
#define DEVICE_CMD_GET_TRANSPORT_INFO          (0x10)  // unimplemented // how many interfaces, what kind
#define DEVICE_CMD_GET_TRANSPORT_ADDR          (0x11)
#define DEVICE_CMD_GET_MAX_PAYLOAD             (0x12)
#define DEVICE_CMD_CLEAR_DATA_BUFFER           (0x13)
  // 0x14-0x1F reserved for more get_transport stuff as needed
#define DEVICE_CMD_SET_TRANSPORT_ADDR          (0x21)
#define DEVICE_CMD_SET_MAX_PAYLOAD             (0x22)
#define DEVICE_CMD_SAVE_TRANSPORT_ADDR         (0x23)
  // 0x22-0x2F reserved for more set_transport stuff as needed
#define DEVICE_CMD_GET_CHANNEL_INFO            (0x30)  // unimplemented // how many, are they TX/RX, etc
  // 0x31-0x4F reserved for channel parameter get and set
#define DEVICE_CMD_GET_TIMING_INFO             (0x50)  // unimplemented // what timing inputs are available
#define DEVICE_CMD_GET_TIMING_STATUS           (0x51)  // PPS present/absent; 10 MHz locked/unlocked
#define DEVICE_CMD_GET_TIMING_REF              (0x52)  // unimplemented // current sources of 10 MHz and PPS
#define DEVICE_CMD_SET_TIMING_REF              (0x53)  // unimplemented // set sources of 10 MHz and PPS
  // these may be removed before release
#define DEVICE_CMD_READ_DAC_REG                (0x60)
#define DEVICE_CMD_WRITE_DAC_REG               (0x61)
#define DEVICE_CMD_READ_ADC_REG                (0x62)
#define DEVICE_CMD_WRITE_ADC_REG               (0x63)
  // 0x64-0x9F reserved
#define DEVICE_CMD_APP_UPDATE_MODE_SET         (0xA0)
#define DEVICE_CMD_APP_UPDATE_DATA             (0xA1)
#define DEVICE_CMD_APP_UPDATE_DONE             (0xA2)
// 0xA3-0xFD reserved for stuff we haven't thought of yet
#define DEVICE_CMD_STOP                        (0xFE)  // stop all streaming and return to wait state within session
#define DEVICE_CMD_RESET                       (0xFF)  // do a power on reset (closing session and returning to idle)

// Device Command Errors
#define DEVICE_CMD_ERR_BAD_CMD                 (0x01)  // generic bad stuff happened; can substitute for more detailed ones until they're implemented
#define DEVICE_CMD_ERR_BUSY                    (0x02)  // can't accept that command because we're running
#define DEVICE_CMD_ERR_NO_SUCH_SUBDEVICE       (0x03)  // the specified subdevice doesn't exist
#define DEVICE_CMD_ERR_TIMEOUT                 (0x04)  // tried to do command but something didn't answer
#define DEVICE_CMD_ERR_BAD_PARAMETER           (0x05)  // command parameter is out of allowed range
#define DEVICE_CMD_ERR_OUT_OF_SEQUENCE         (0x06)  // command received with sequence error
#define DEVICE_CMD_ERR_CTRL_BUFFER_OVERFLOW    (0x07)  // overflow control buffer

// Radio Commands
#define RADIO_CMD_START_NOW_UNTIL_STOP         (0x01)
#define RADIO_CMD_START_AT_TIME_UNTIL_STOP     (0x02)  // error if TIME_PRESENT is 0
#define RADIO_CMD_STOP_NOW                     (0x03)  // stops anything going on
#define RADIO_CMD_START_NOW_FOR_N_SAMPLES      (0x04)
#define RADIO_CMD_START_AT_TIME_FOR_N_SAMPLES  (0x05)  // error if TIME_PRESENT is 0
  // 0x06-0x0F reserved
#define RADIO_CMD_GET_RF_INFO                  (0x10)  // unimplemented // available frequency, gain, sample rate, and bandwidth ranges, number of selectable I/Os
#define RADIO_CMD_GET_RF_FREQ                  (0x11)
#define RADIO_CMD_GET_RF_GAIN                  (0x12)
#define RADIO_CMD_GET_SAMPLE_RATE              (0x13)
#define RADIO_CMD_GET_RF_BW                    (0x14)  // unimplemented
#define RADIO_CMD_GET_RF_ENABLED               (0x15)  // unimplemented
#define RADIO_CMD_GET_RF_PORT                  (0x16)  // unimplemented
  // 0x17-0x20 reserved
#define RADIO_CMD_SET_RF_FREQ                  (0x21)
#define RADIO_CMD_SET_RF_GAIN                  (0x22)
#define RADIO_CMD_SET_SAMPLE_RATE              (0x23)
#define RADIO_CMD_SET_RF_BW                    (0x24)  // unimplemented
#define RADIO_CMD_SET_RF_ENABLED               (0x25)
#define RADIO_CMD_SET_RF_PORT                  (0x26)  // FIXME: this selects input and output ports
  // 0x27-0x30 reserved
#define RADIO_CMD_GET_NUM_SUBDEVS              (0x31)  // unimplemented - return the number of subdevices
#define RADIO_CMD_GET_CAPABILITIES             (0x32)  // unimplemented
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

// Async messages
#define ASYNC_NO_ERROR                         (0x00)
#define ASYNC_DATA_UNDERFLOW                   (0x01)
#define ASYNC_DATA_OVERFLOW                    (0x02)
#define ASYNC_OVER_TEMP                        (0x03)  // too hot
#define ASYNC_POWER_ERROR                      (0x04)  // voltage low/current high
#define ASYNC_FREQ_ERROR                       (0x05)  // loss of lock, etc
  // 0x06-0xFF reserved

// Capabilities
#define CAPABILITY_HAS_LOCK_DETECT             (0x00000001U)
#define CAPABILITY_HAS_EXTERNAL_REF            (0x00000002U)
#define CAPABILITY_HAS_EXTERNAL_PPS            (0x00000004U)
#define CAPABILITY_HAS_AGC                     (0x00000008U)
#define CAPABILITY_HAS_LIMITER                 (0x00000010U)
#define CAPABILITY_HAS_LIMIT_DETECT            (0x00000020U)
#define CAPABILITY_HAS_MANUAL_IQ_BIAS_CORR     (0x00000040U)
#define CAPABILITY_HAS_MANUAL_IQ_BALANCE_CORR  (0x00000080U)
#define CAPABILITY_HAS_AUTO_IQ_BIAS_CORR       (0x00000100U)
#define CAPABILITY_HAS_AUTO_IQ_BALANCE_CORR    (0x00000200U)
#define CAPABILITY_HAS_TEMP_MEASURE            (0x00000400U)
#define CAPABILITY_HAS_POWER_MEASURE           (0x00000800U)
#define CAPABILITY_HAS_EXTERNAL_LO             (0x00001000U)

#define MAX_DATA_LENGTH_SAMPLES                (2048UL)
#define MAX_PAYLOAD_LENGTH_BYTES               (32UL)  // maximum length of a CMD or RSP packet, excluding header

typedef struct {
  uint8_t                  packet_type;
  uint8_t                  command;
  uint8_t                  target_subdevice;  // for cases where multiple devices exist (e.g. multiple network I/F or multichannel TX/RX)
  uint8_t                  flags;
  uint16_t                 packet_size;       // length of packet including header in bytes
  uint16_t                 sequence_counter;  // need to figure out when this resets (every new data stream, power up, ?)
} packet_header;

typedef struct {
  // FIXME: temporary workaround for order in FPGA and MCU code
  uint32_t                 nanoseconds;
  uint32_t                 seconds;
} time_spec_t;

typedef uint64_t          stream_spec_t;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // PACKET_DEFS_H