#ifdef __cplusplus
#include <type_traits>
#endif // __cplusplus

#include "vxsdr_header.h"

#define CHECK_SIZE_EQUALS(x, n)     static_assert(sizeof(x) == n, "sizeof(" #x ") != " #n)

void compile_time_size_checks() {
  CHECK_SIZE_EQUALS(packet_header, 8);
  CHECK_SIZE_EQUALS(time_spec_t, 8);
  CHECK_SIZE_EQUALS(stream_spec_t, 8);
  CHECK_SIZE_EQUALS(capabilities_t, 8);
  CHECK_SIZE_EQUALS(header_only_packet, 8);
  CHECK_SIZE_EQUALS(one_uint32_packet, 16);
  CHECK_SIZE_EQUALS(error_packet, 16);
  CHECK_SIZE_EQUALS(two_uint32_packet, 16);
  CHECK_SIZE_EQUALS(four_uint32_packet, 24);
  CHECK_SIZE_EQUALS(six_uint32_packet, 32);
  CHECK_SIZE_EQUALS(one_double_packet, 16);
  CHECK_SIZE_EQUALS(two_double_packet, 24);
  CHECK_SIZE_EQUALS(four_double_packet, 40);
  CHECK_SIZE_EQUALS(one_uint64_packet, 16);
  CHECK_SIZE_EQUALS(name_packet, (MAX_PAYLOAD_LENGTH_BYTES + 8));
  CHECK_SIZE_EQUALS(async_msg_packet, 8);
}

#ifdef __cplusplus

// do additional checks on the host side using type traits

#endif // __cplusplus