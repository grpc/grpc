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

// For debug of the timer manager crash only.
// TODO (mxyan): remove after bug is fixed.
#ifdef GRPC_DEBUG_TIMER_MANAGER
#include <string.h>
void (*g_grpc_debug_timer_manager_stats)(
    int64_t timer_manager_init_count, int64_t timer_manager_shutdown_count,
    int64_t fork_count, int64_t timer_wait_err, int64_t timer_cv_value,
    int64_t timer_mu_value, int64_t abstime_sec_value,
    int64_t abstime_nsec_value, int64_t abs_deadline_sec_value,
    int64_t abs_deadline_nsec_value, int64_t now1_sec_value,
    int64_t now1_nsec_value, int64_t now2_sec_value, int64_t now2_nsec_value,
    int64_t add_result_sec_value, int64_t add_result_nsec_value,
    int64_t sub_result_sec_value, int64_t sub_result_nsec_value,
    int64_t next_value, int64_t start_time_sec,
    int64_t start_time_nsec) = nullptr;
int64_t g_timer_manager_init_count = 0;
int64_t g_timer_manager_shutdown_count = 0;
int64_t g_fork_count = 0;
int64_t g_timer_wait_err = 0;
int64_t g_timer_cv_value = 0;
int64_t g_timer_mu_value = 0;
int64_t g_abstime_sec_value = -1;
int64_t g_abstime_nsec_value = -1;
int64_t g_abs_deadline_sec_value = -1;
int64_t g_abs_deadline_nsec_value = -1;
int64_t g_now1_sec_value = -1;
int64_t g_now1_nsec_value = -1;
int64_t g_now2_sec_value = -1;
int64_t g_now2_nsec_value = -1;
int64_t g_add_result_sec_value = -1;
int64_t g_add_result_nsec_value = -1;
int64_t g_sub_result_sec_value = -1;
int64_t g_sub_result_nsec_value = -1;
int64_t g_next_value = -1;
int64_t g_start_time_sec = -1;
int64_t g_start_time_nsec = -1;
#endif  // GRPC_DEBUG_TIMER_MANAGER

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

// For debug of the timer manager crash only.
// TODO (mxyan): remove after bug is fixed.
#ifdef GRPC_DEBUG_TIMER_MANAGER
static gpr_timespec gpr_convert_clock_type_debug_timespec(
    gpr_timespec t, gpr_clock_type clock_type, gpr_timespec& now1,
    gpr_timespec& now2, gpr_timespec& add_result, gpr_timespec& sub_result) {
  if (t.clock_type == clock_type) {
    return t;
  }

  if (t.tv_sec == INT64_MAX || t.tv_sec == INT64_MIN) {
    t.clock_type = clock_type;
    return t;
  }

  if (clock_type == GPR_TIMESPAN) {
    return gpr_time_sub(t, gpr_now(t.clock_type));
  }

  if (t.clock_type == GPR_TIMESPAN) {
    return gpr_time_add(gpr_now(clock_type), t);
  }

  now1 = gpr_now(t.clock_type);
  sub_result = gpr_time_sub(t, now1);
  now2 = gpr_now(clock_type);
  add_result = gpr_time_add(now2, sub_result);
  return add_result;
}

#define gpr_convert_clock_type_debug(t, clock_type, now1, now2, add_result, \
                                     sub_result)                            \
  gpr_convert_clock_type_debug_timespec((t), (clock_type), (now1), (now2),  \
                                        (add_result), (sub_result))
#else
#define gpr_convert_clock_type_debug(t, clock_type, now1, now2, add_result, \
                                     sub_result)                            \
  gpr_convert_clock_type((t), (clock_type))
#endif

int gpr_cv_wait(gpr_cv* cv, gpr_mu* mu, gpr_timespec abs_deadline) {
  int err = 0;
#ifdef GRPC_DEBUG_TIMER_MANAGER
  // For debug of the timer manager crash only.
  // TODO (mxyan): remove after bug is fixed.
  gpr_timespec abs_deadline_copy;
  abs_deadline_copy.tv_sec = abs_deadline.tv_sec;
  abs_deadline_copy.tv_nsec = abs_deadline.tv_nsec;
  gpr_timespec now1;
  gpr_timespec now2;
  gpr_timespec add_result;
  gpr_timespec sub_result;
  memset(&now1, 0, sizeof(now1));
  memset(&now2, 0, sizeof(now2));
  memset(&add_result, 0, sizeof(add_result));
  memset(&sub_result, 0, sizeof(sub_result));
#endif
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

#ifdef GRPC_DEBUG_TIMER_MANAGER
    // For debug of the timer manager crash only.
    // TODO (mxyan): remove after bug is fixed.
    if (GPR_UNLIKELY(!(err == 0 || err == ETIMEDOUT || err == EAGAIN))) {
      g_abstime_sec_value = abs_deadline_ts.tv_sec;
      g_abstime_nsec_value = abs_deadline_ts.tv_nsec;
    }
#endif
  }

#ifdef GRPC_DEBUG_TIMER_MANAGER
  // For debug of the timer manager crash only.
  // TODO (mxyan): remove after bug is fixed.
  if (GPR_UNLIKELY(!(err == 0 || err == ETIMEDOUT || err == EAGAIN))) {
    if (g_grpc_debug_timer_manager_stats) {
      g_timer_wait_err = err;
      g_timer_cv_value = (int64_t)cv;
      g_timer_mu_value = (int64_t)mu;
      g_abs_deadline_sec_value = abs_deadline_copy.tv_sec;
      g_abs_deadline_nsec_value = abs_deadline_copy.tv_nsec;
      g_now1_sec_value = now1.tv_sec;
      g_now1_nsec_value = now1.tv_nsec;
      g_now2_sec_value = now2.tv_sec;
      g_now2_nsec_value = now2.tv_nsec;
      g_add_result_sec_value = add_result.tv_sec;
      g_add_result_nsec_value = add_result.tv_nsec;
      g_sub_result_sec_value = sub_result.tv_sec;
      g_sub_result_nsec_value = sub_result.tv_nsec;
      g_grpc_debug_timer_manager_stats(
          g_timer_manager_init_count, g_timer_manager_shutdown_count,
          g_fork_count, g_timer_wait_err, g_timer_cv_value, g_timer_mu_value,
          g_abstime_sec_value, g_abstime_nsec_value, g_abs_deadline_sec_value,
          g_abs_deadline_nsec_value, g_now1_sec_value, g_now1_nsec_value,
          g_now2_sec_value, g_now2_nsec_value, g_add_result_sec_value,
          g_add_result_nsec_value, g_sub_result_sec_value,
          g_sub_result_nsec_value, g_next_value, g_start_time_sec,
          g_start_time_nsec);
    }
  }
#endif
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
