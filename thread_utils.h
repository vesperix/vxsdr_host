#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

int set_current_thread_affinity(const int cpunum)
{

  return 0;
}

int set_current_thread_priority(const int priority)
{

  return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* THREAD_UTILS_H */