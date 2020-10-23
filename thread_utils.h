#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

#define HAVE_PTHREADS // quick kludge for the moment

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_PTHREADS
//#define _GNU_SOURCE
#include <pthread.h>
#endif
/*
int set_current_thread_affinity(const int cpunum)
{
#ifdef HAVE_PTHREADS
  pthread_t current_thread = pthread_self();
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpunum, &set);
  return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &set);
#else
  return -1;
#endif
}
*/
int set_current_thread_priority_realtime(const int priority)
{
#ifdef HAVE_PTHREADS
  pthread_t current_thread = pthread_self();
  int policy = SCHED_RR;
  struct sched_param param;
  param.sched_priority = priority;
  return pthread_setschedparam(current_thread, policy, &param);
#else
  return -1;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* THREAD_UTILS_H */
