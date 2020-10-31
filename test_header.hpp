#ifndef TEST_HEADER_HPP
#define TEST_HEADER_HPP

#include <cstdint>
#include <complex>

static constexpr int   TX_SIGNAL_ID = 0xDACD;
static constexpr int   RX_SIGNAL_ID = 0xADCD;
static constexpr int   TX_ACK_ID    = 0xDACA;

static constexpr size_t DATA_LENGTH = 256;

typedef struct {
  uint32_t         sequence_cnt;
  uint16_t         id;
  uint16_t         pad;
} packet_header;

typedef struct {
  packet_header    hdr;
  uint32_t         dac_ddr_used;
  uint32_t         dac_ddr_size;
} ack_packet;

typedef struct {
  packet_header    hdr;
  complex<int16_t> data[DATA_LENGTH];
} data_packet;

#endif /* TEST_HEADER_HPP */