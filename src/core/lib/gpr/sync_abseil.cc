/*
 *
 * Copyright 2020 gRPC authors.
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

#if defined(GPR_ABSEIL_SYNC) && !defined(GPR_CUSTOM_SYNC)

#include <grpc/support/alloc.h>

#include <errno.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <time.h>
#include "src/core/lib/profiling/timers.h"

#include "absl/base/call_once.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#ifdef GPR_LOW_LEVEL_COUNTERS
gpr_atm gpr_mu_locks = 0;
gpr_atm gpr_counter_atm_cas = 0;
gpr_atm gpr_counter_atm_add = 0;
#endif

void gpr_mu_init(gpr_mu* mu) {
  static_assert(sizeof(gpr_mu) == sizeof(absl::Mutex),
                "gpr_mu and Mutex must be the same size");
  new (mu) absl::Mutex;
}

void gpr_mu_destroy(gpr_mu* mu) {
  reinterpret_cast<absl::Mutex*>(mu)->~Mutex();
}

void gpr_mu_lock(gpr_mu* mu) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GPR_TIMER_SCOPE("gpr_mu_lock", 0);
  reinterpret_cast<absl::Mutex*>(mu)->Lock();
}

void gpr_mu_unlock(gpr_mu* mu) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GPR_TIMER_SCOPE("gpr_mu_unlock", 0);
  reinterpret_cast<absl::Mutex*>(mu)->Unlock();
}

int gpr_mu_trylock(gpr_mu* mu) {
  GPR_TIMER_SCOPE("gpr_mu_trylock", 0);
  int ret = reinterpret_cast<absl::Mutex*>(mu)->TryLock() == true;
  return ret;
}

/*----------------------------------------*/

void gpr_cv_init(gpr_cv* cv) {
  static_assert(sizeof(gpr_cv) == sizeof(absl::CondVar),
                "gpr_cv and CondVar must be the same size");
  new (cv) absl::CondVar;
}

void gpr_cv_destroy(gpr_cv* cv) {
  reinterpret_cast<absl::CondVar*>(cv)->~CondVar();
}

int gpr_cv_wait(gpr_cv* cv, gpr_mu* mu, gpr_timespec abs_deadline) {
  GPR_TIMER_SCOPE("gpr_cv_wait", 0);
  if (gpr_time_cmp(abs_deadline, gpr_inf_future(abs_deadline.clock_type)) ==
      0) {
    reinterpret_cast<absl::CondVar*>(cv)->Wait(
        reinterpret_cast<absl::Mutex*>(mu));
    return 0;
  }
  abs_deadline = gpr_convert_clock_type(abs_deadline, GPR_CLOCK_REALTIME);
  timespec ts = {static_cast<decltype(ts.tv_sec)>(abs_deadline.tv_sec),
                 static_cast<decltype(ts.tv_nsec)>(abs_deadline.tv_nsec)};
  int ret = reinterpret_cast<absl::CondVar*>(cv)->WaitWithDeadline(
                reinterpret_cast<absl::Mutex*>(mu),
                absl::TimeFromTimespec(ts)) == true;
  return ret;
}

void gpr_cv_signal(gpr_cv* cv) {
  GPR_TIMER_MARK("gpr_cv_signal", 0);
  reinterpret_cast<absl::CondVar*>(cv)->Signal();
}

void gpr_cv_broadcast(gpr_cv* cv) {
  GPR_TIMER_MARK("gpr_cv_broadcast", 0);
  reinterpret_cast<absl::CondVar*>(cv)->SignalAll();
}

/*----------------------------------------*/

void gpr_once_init(gpr_once* once, void (*init_function)(void)) {
  static_assert(sizeof(gpr_once) == sizeof(absl::once_flag),
                "gpr_once and absl::once_flag must be the same size");
  absl::call_once(*reinterpret_cast<absl::once_flag*>(once), init_function);
}

#endif /* defined(GPR_ABSEIL_SYNC) && !defined(GPR_CUSTOM_SYNC) */
