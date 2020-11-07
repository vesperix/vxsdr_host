#ifndef VRT_HEADER_HPP
#define VRT_HEADER_HPP

#include <cstdint>

#include <boost/endian/arithmetic.hpp>
#include <boost/static_assert.hpp>

/*
  NOTE: VITA49 is big endian; any types longer than 1 byte will need swapping on little-endian systems
  The struct below uses boost endian types to transparently convert to big endian where needed
  (i.e. for any elements bigger than a byte)
*/

#if defined(__GNUC__)  // g++ or clang or intel define this
typedef struct __attribute__((packed)) {
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
#else if defined(_MSC_VER) // msvc defines this (and so does clang???)
#pragma pack(push, 1)
typedef struct {
  uint8_t         packet_type            :  4;
  uint8_t         class_present          :  1;
  uint8_t         indicators             :  3;
  uint8_t         integer_time_format    :  2;
  uint8_t         fractional_time_format :  2;
  uint8_t         packet_count           :  4;
  big_uint16_t    packet_size_words      : 16;
} vrt_packet_preamble;
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
  hdr.preamble.packet_type            = 0; // signal data without stream identifier
  hdr.preamble.class_present          = 0; // no class data included
  hdr.preamble.indicators             = 0; // no trailer, is VITA 49.0 compliant, not spectral data
  hdr.preamble.integer_time_format    = 3; // other (not GPS or UTC) time
  hdr.preamble.fractional_time_format = 2; // picoseconds
  hdr.preamble.packet_count           = packet_count % 16;
  // note that 1 complex sample (16-bit I & Q samples) occupies 1 32-bit word,
  // so the number of data words is the same as the number of complex samples
  hdr.preamble.packet_size_words      = packet_data_size_cplx_samples + header_size_words;
  hdr.integer_timestamp               = integer_time;
  hdr.fractional_timestamp            = fractional_time;
  return 0;
}

#endif /* VRT_HEADER_HPP */