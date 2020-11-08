#ifndef VRT_HEADER_HPP
#define VRT_HEADER_HPP

#include <cstdint>

#include <boost/endian/arithmetic.hpp>
#include <boost/static_assert.hpp>

/*
  NOTE: VITA49 is big endian; any types longer than 1 byte will need swapping
  on little-endian systems. The struct below uses boost endian types to
  transparently convert to big endian where needed (i.e. for any elements bigger
  than a byte)
*/

// Constants used in VITA49 packets
namespace VITA49 {
  namespace PACKET_TYPE {
    constexpr unsigned SIGNAL_PKT_WITHOUT_STREAM_ID      = 0;
    constexpr unsigned SIGNAL_PKT_WITH_STREAM_ID         = 1;
    constexpr unsigned EXTENSION_PKT_WITHOUT_STREAM_ID   = 2;
    constexpr unsigned EXTENSION_PKT_WITH_STREAM_ID      = 3;
    constexpr unsigned CONTEXT_PKT                       = 4;
    constexpr unsigned EXTENSION_CONTEXT_PKT             = 5;
    constexpr unsigned COMMAND_PKT                       = 6;
    constexpr unsigned EXTENSION_COMMAND_PKT             = 7;
  };
  namespace INDICATORS {
    namespace SIGNAL {
      constexpr unsigned TRAILER_PRESENT                 = 4;
      constexpr unsigned NOT_VITA_49_0                   = 2;
      constexpr unsigned SPECTRAL_DATA                   = 1;
    };
    namespace CONTEXT {
      constexpr unsigned NOT_VITA_49_0                   = 2;
      constexpr unsigned TIMESTAMP_APPROXIMATE           = 1;
    };
    namespace CONTROL {
      constexpr unsigned REQUEST_ACK                     = 4;
      constexpr unsigned CANCELLATION                    = 1;
    };
  };
  namespace INTEGER_TIME_FMT {
    constexpr unsigned NOT_PRESENT                       = 0;
    constexpr unsigned GPS_TIME                          = 1;
    constexpr unsigned UTC_TIME                          = 2;
    constexpr unsigned OTHER_TIME                        = 3;
  };
  namespace FRACTIONAL_TIME_FMT {
    constexpr unsigned NOT_PRESENT                       = 0;
    constexpr unsigned SAMPLE_COUNT                      = 1;
    constexpr unsigned PICOSECONDS                       = 2;
    constexpr unsigned FREE_RUNNING                      = 3;
  };
};

#if defined(__GNUC__)  // g++, clang, and intel define this
typedef struct __attribute__((packed)) {
#else if defined(_MSC_VER) // msvc defines this (and so does clang???)
#pragma pack(push, 1)
typedef struct {
#endif
  uint8_t         packet_type            :  4;
  /* allowed values:
    0000 (0) - signal data packet without stream identifier
    0001 (1) - signal data packet with stream identifier
    0010 (2) - extension data packet without stream identifier
    0011 (3) - extension data packet with stream identifier
    0100 (4) - context packet
    0101 (5) - extension context packet
    0110 (6) - command packet
    0111 (7) - extension command packet
    [all other vlaues are reserved]
  */
  uint8_t         class_present          :  1;
  /* allowed values:
    0 - class id not present in packet
    1 - class id present in packet
  */
  uint8_t         indicators             :  3;
  /* allowed values:
      for signal data packets:
        bit 2: indicates trailer present
        bit 1: indicates packet is not VITA 49.0
        bit 0: indicates signal data is spectrum data (not time series)
      for context packets:
        bit 2: reserved - must be 0
        bit 1: indicates packet is not VITA 49.0
        bit 0: indicates timestamp mode
      for control packets:
        bit 2: requests packet be acknowledged
        bit 1: reserved - must be 0
        bit 0: indicates a cancellation packet
  */
  uint8_t         integer_time_format    :  2;
  /* allowed values:
    00 (0) - no integer timestamp present in packet
    01 (1) - integer timestamp indicates UTC time
    10 (2) - integer timestamp indicates GPS time
    11 (3) - integer timestamp indicates other time
  */
  uint8_t         fractional_time_format :  2;
  /* allowed values:
    00 (0) - no fractional timestamp present in packet
    01 (1) - fractional timestamp indcates sample count at second boundary
    10 (2) - fractional timestamp indicates picoseconds
    11 (3) - fractional timestamp is free running count
  */
  uint8_t         packet_count           :  4;
  /* legacy 4-bit counter field = packet_count % 16; not reliable for
     detecting out-of-order packets */
  big_uint16_t    packet_size_words      : 16;
  /* the total number of 32-bit words in the packet, including the header */
} vrt_packet_preamble;
#if defined(_MSC_VER)
#pragma pack(pop)
#endif

typedef struct {
  vrt_packet_preamble   preamble;
  big_uint32_t          integer_timestamp;
  big_uint64_t          fractional_timestamp;
} vesperix_vrt_header;

int setup_vesperix_signal_header(vesperix_vrt_header &hdr,
                                const size_t packet_count,
                                const size_t packet_data_size_cplx_samples,
                                const uint32_t integer_time,
                                const uint64_t fractional_time)
{
  const size_t header_size_words = sizeof(vesperix_vrt_header) / 4;
  // maximum number of 32-bit words in a VITA49 packet is 65535, including header
  if(packet_data_size_cplx_samples > 65535 - header_size_words)
    return 1;
  hdr.preamble.packet_type            = VITA49::PACKET_TYPE::SIGNAL_PKT_WITHOUT_STREAM_ID;
  hdr.preamble.class_present          = 0; // no class data included
  hdr.preamble.indicators             = 0; // 3 bits:
                                           //   0 - no trailer,
                                           //   0 - VITA 49.0 compliant,
                                           //   0 - time series, not spectral data
  hdr.preamble.integer_time_format    = VITA49::INTEGER_TIME_FMT::OTHER_TIME;
  hdr.preamble.fractional_time_format = VITA49::FRACTIONAL_TIME_FMT::FREE_RUNNING;
  hdr.preamble.packet_count           = packet_count % 16;
  // note that 1 complex sample (16-bit I & Q samples) occupies 1 32-bit word,
  // so the number of data words is the same as the number of complex samples
  hdr.preamble.packet_size_words      = packet_data_size_cplx_samples + header_size_words;
  hdr.integer_timestamp               = integer_time;
  hdr.fractional_timestamp            = fractional_time;
  return 0;
}

#endif /* VRT_HEADER_HPP */