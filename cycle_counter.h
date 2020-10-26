#ifndef CYCLE_COUNTER_H
#define CYCLE_COUNTER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

inline unsigned long long read_cycle_counter()
{
#ifndef __x86_64__
#error("TSC is not available")
  return 0;
#else
  return __rdtsc();
#endif /* __X86_64__ */
}

#ifdef __cplusplus
}
#endif

#endif /* CYCLE_COUNTER_H */
