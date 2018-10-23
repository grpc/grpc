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
void (*g_grpc_debug_timer_manager_stats)(
    int64_t timer_manager_init_count, int64_t timer_manager_shutdown_count,
    int64_t fork_count, int64_t timer_wait_err, int64_t timer_cv_value,
    int64_t timer_mu_value, int64_t abstime_sec_value,
    int64_t abstime_nsec_value) = nullptr;
int64_t g_timer_manager_init_count = 0;
int64_t g_timer_manager_shutdown_count = 0;
int64_t g_fork_count = 0;
int64_t g_timer_wait_err = 0;
int64_t g_timer_cv_value = 0;
int64_t g_timer_mu_value = 0;
int64_t g_abstime_sec_value = -1;
int64_t g_abstime_nsec_value = -1;
#endif  // GRPC_DEBUG_TIMER_MANAGER

#ifdef GPR_LOW_LEVEL_COUNTERS
gpr_atm gpr_mu_locks = 0;
gpr_atm gpr_counter_atm_cas = 0;
gpr_atm gpr_counter_atm_add = 0;
#endif

void gpr_mu_init(gpr_mu* mu) {
  GPR_ASSERT(pthread_mutex_init(mu, nullptr) == 0);
}

void gpr_mu_destroy(gpr_mu* mu) { GPR_ASSERT(pthread_mutex_destroy(mu) == 0); }

void gpr_mu_lock(gpr_mu* mu) {
#ifdef GPR_LOW_LEVEL_COUNTERS
  GPR_ATM_INC_COUNTER(gpr_mu_locks);
#endif
  GPR_TIMER_SCOPE("gpr_mu_lock", 0);
  GPR_ASSERT(pthread_mutex_lock(mu) == 0);
}

void gpr_mu_unlock(gpr_mu* mu) {
  GPR_TIMER_SCOPE("gpr_mu_unlock", 0);
  GPR_ASSERT(pthread_mutex_unlock(mu) == 0);
}

int gpr_mu_trylock(gpr_mu* mu) {
  GPR_TIMER_SCOPE("gpr_mu_trylock", 0);
  int err = pthread_mutex_trylock(mu);
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
  GPR_ASSERT(pthread_cond_init(cv, &attr) == 0);
}

void gpr_cv_destroy(gpr_cv* cv) { GPR_ASSERT(pthread_cond_destroy(cv) == 0); }

int gpr_cv_wait(gpr_cv* cv, gpr_mu* mu, gpr_timespec abs_deadline) {
  int err = 0;
  if (gpr_time_cmp(abs_deadline, gpr_inf_future(abs_deadline.clock_type)) ==
      0) {
    err = pthread_cond_wait(cv, mu);
  } else {
    struct timespec abs_deadline_ts;
#if GPR_LINUX
    abs_deadline = gpr_convert_clock_type(abs_deadline, GPR_CLOCK_MONOTONIC);
#else
    abs_deadline = gpr_convert_clock_type(abs_deadline, GPR_CLOCK_REALTIME);
#endif  // GPR_LINUX
    abs_deadline_ts.tv_sec = static_cast<time_t>(abs_deadline.tv_sec);
    abs_deadline_ts.tv_nsec = abs_deadline.tv_nsec;
    err = pthread_cond_timedwait(cv, mu, &abs_deadline_ts);
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
      g_grpc_debug_timer_manager_stats(
          g_timer_manager_init_count, g_timer_manager_shutdown_count,
          g_fork_count, g_timer_wait_err, g_timer_cv_value, g_timer_mu_value,
          g_abstime_sec_value, g_abstime_nsec_value);
    }
  }
#endif
  GPR_ASSERT(err == 0 || err == ETIMEDOUT || err == EAGAIN);
  return err == ETIMEDOUT;
}

void gpr_cv_signal(gpr_cv* cv) { GPR_ASSERT(pthread_cond_signal(cv) == 0); }

void gpr_cv_broadcast(gpr_cv* cv) {
  GPR_ASSERT(pthread_cond_broadcast(cv) == 0);
}

/*----------------------------------------*/

void gpr_once_init(gpr_once* once, void (*init_function)(void)) {
  GPR_ASSERT(pthread_once(once, init_function) == 0);
}

#endif /* GRP_POSIX_SYNC */
