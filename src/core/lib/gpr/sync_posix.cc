/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <grpc/support/alloc.h>

#ifdef GPR_POSIX_SYNC

#include <errno.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <time.h>
#include "src/core/lib/profiling/timers.h"

#ifdef GPR_LOW_LEVEL_COUNTERS
gpr_atm gpr_mu_locks = 0;
gpr_atm gpr_counter_atm_cas = 0;
gpr_atm gpr_counter_atm_add = 0;
#endif

void gpr_mu_init(gpr_mu* mu) {
#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_mutex_init(&mu->mutex, nullptr) == 0);
  mu->leak_checker = static_cast<int*>(malloc(sizeof(*mu->leak_checker)));
  GPR_ASSERT(mu->leak_checker != nullptr);
#else
  GPR_ASSERT(pthread_mutex_init(mu, nullptr) == 0);
#endif
}

void gpr_mu_destroy(gpr_mu* mu) {
#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_mutex_destroy(&mu->mutex) == 0);
  free(mu->leak_checker);
#else
  GPR_ASSERT(pthread_mutex_destroy(mu) == 0);
#endif
}

void gpr_mu_lock(gpr_mu* mu) {
#ifdef GPR_LOW_LEVEL_COUNTERS
  GPR_ATM_INC_COUNTER(gpr_mu_locks);
#endif
  GPR_TIMER_SCOPE("gpr_mu_lock", 0);
#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_mutex_lock(&mu->mutex) == 0);
#else
  GPR_ASSERT(pthread_mutex_lock(mu) == 0);
#endif
}

void gpr_mu_unlock(gpr_mu* mu) {
  GPR_TIMER_SCOPE("gpr_mu_unlock", 0);
#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_mutex_unlock(&mu->mutex) == 0);
#else
  GPR_ASSERT(pthread_mutex_unlock(mu) == 0);
#endif
}

int gpr_mu_trylock(gpr_mu* mu) {
  GPR_TIMER_SCOPE("gpr_mu_trylock", 0);
  int err = 0;
#ifdef GRPC_ASAN_ENABLED
  err = pthread_mutex_trylock(&mu->mutex);
#else
  err = pthread_mutex_trylock(mu);
#endif
  GPR_ASSERT(err == 0 || err == EBUSY);
  return err == 0;
}

/*----------------------------------------*/

void gpr_cv_init(gpr_cv* cv) {
  pthread_condattr_t attr;
  GPR_ASSERT(pthread_condattr_init(&attr) == 0);
#if GPR_LINUX
  GPR_ASSERT(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0);
#endif  // GPR_LINUX

#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_cond_init(&cv->cond_var, &attr) == 0);
  cv->leak_checker = static_cast<int*>(malloc(sizeof(*cv->leak_checker)));
  GPR_ASSERT(cv->leak_checker != nullptr);
#else
  GPR_ASSERT(pthread_cond_init(cv, &attr) == 0);
#endif
}

void gpr_cv_destroy(gpr_cv* cv) {
#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_cond_destroy(&cv->cond_var) == 0);
  free(cv->leak_checker);
#else
  GPR_ASSERT(pthread_cond_destroy(cv) == 0);
#endif
}

#define gpr_convert_clock_type_debug(t, clock_type, now1, now2, add_result, \
                                     sub_result)                            \
  gpr_convert_clock_type((t), (clock_type))

int gpr_cv_wait(gpr_cv* cv, gpr_mu* mu, gpr_timespec abs_deadline) {
  int err = 0;
  if (gpr_time_cmp(abs_deadline, gpr_inf_future(abs_deadline.clock_type)) ==
      0) {
#ifdef GRPC_ASAN_ENABLED
    err = pthread_cond_wait(&cv->cond_var, &mu->mutex);
#else
    err = pthread_cond_wait(cv, mu);
#endif
  } else {
    struct timespec abs_deadline_ts;
#if GPR_LINUX
    abs_deadline = gpr_convert_clock_type_debug(
        abs_deadline, GPR_CLOCK_MONOTONIC, now1, now2, add_result, sub_result);
#else
    abs_deadline = gpr_convert_clock_type_debug(
        abs_deadline, GPR_CLOCK_REALTIME, now1, now2, add_result, sub_result);
#endif  // GPR_LINUX
    abs_deadline_ts.tv_sec = static_cast<time_t>(abs_deadline.tv_sec);
    abs_deadline_ts.tv_nsec = abs_deadline.tv_nsec;
#ifdef GRPC_ASAN_ENABLED
    err = pthread_cond_timedwait(&cv->cond_var, &mu->mutex, &abs_deadline_ts);
#else
    err = pthread_cond_timedwait(cv, mu, &abs_deadline_ts);
#endif
  }
  GPR_ASSERT(err == 0 || err == ETIMEDOUT || err == EAGAIN);
  return err == ETIMEDOUT;
}

void gpr_cv_signal(gpr_cv* cv) {
#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_cond_signal(&cv->cond_var) == 0);
#else
  GPR_ASSERT(pthread_cond_signal(cv) == 0);
#endif
}

void gpr_cv_broadcast(gpr_cv* cv) {
#ifdef GRPC_ASAN_ENABLED
  GPR_ASSERT(pthread_cond_broadcast(&cv->cond_var) == 0);
#else
  GPR_ASSERT(pthread_cond_broadcast(cv) == 0);
#endif
}

/*----------------------------------------*/

void gpr_once_init(gpr_once* once, void (*init_function)(void)) {
  GPR_ASSERT(pthread_once(once, init_function) == 0);
}

#endif /* GRP_POSIX_SYNC */
