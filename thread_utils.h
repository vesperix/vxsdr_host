#ifndef THREAD_UTILS_H
#define THREAD_UTILS_H

//FIXME: need to add Windows and MacOS!
#define HAVE_PTHREADS // quick kludge for the moment

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_PTHREADS
#include <pthread.h>
#endif

inline int set_current_thread_affinity(const int cpunum)
{
#ifdef HAVE_PTHREADS
  pthread_t current_thread = pthread_self();
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpunum, &set);
  // returns 0 on success
  return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &set);
#else
  return -1;
#endif
}

inline int set_current_thread_priority_realtime(const int priority)
{
#ifdef HAVE_PTHREADS
  pthread_t current_thread = pthread_self();
  int policy = SCHED_RR;
  struct sched_param param;
  param.sched_priority = priority;
  // returns 0 on success
  return pthread_setschedparam(current_thread, policy, &param);
#else
  return -1;
#endif
}

inline int set_current_thread_priority_realtime_relative(double priority)
{
#ifdef HAVE_PTHREADS
  pthread_t current_thread = pthread_self();
  int policy = SCHED_RR;
  int pri_min = sched_get_priority_min(policy);
  int pri_max = sched_get_priority_max(policy);
  struct sched_param param;
  if(priority > 1.0) {
    priority = 1.0;
  }
  else if(priority < 0.0) {
    priority = 0.0;
  }
  param.sched_priority = lround(priority * (pri_max - pri_min)) + pri_min;
  // returns 0 on success
  return pthread_setschedparam(current_thread, policy, &param);
#else
  return -1;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* THREAD_UTILS_H */
