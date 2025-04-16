//
//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#if defined(GPR_ABSEIL_SYNC) && !defined(GPR_CUSTOM_SYNC)

#include <errno.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <time.h>

#include "absl/base/call_once.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/core/util/time_util.h"

void gpr_mu_init(gpr_mu* mu) {
  static_assert(sizeof(gpr_mu) == sizeof(absl::Mutex),
                "gpr_mu and Mutex must be the same size");
  new (mu) absl::Mutex;
}

void gpr_mu_destroy(gpr_mu* mu) {
  reinterpret_cast<absl::Mutex*>(mu)->~Mutex();
}

void gpr_mu_lock(gpr_mu* mu) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  reinterpret_cast<absl::Mutex*>(mu)->Lock();
}

void gpr_mu_unlock(gpr_mu* mu) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  reinterpret_cast<absl::Mutex*>(mu)->Unlock();
}

int gpr_mu_trylock(gpr_mu* mu) {
  return reinterpret_cast<absl::Mutex*>(mu)->TryLock();
}

//----------------------------------------

void gpr_cv_init(gpr_cv* cv) {
  static_assert(sizeof(gpr_cv) == sizeof(absl::CondVar),
                "gpr_cv and CondVar must be the same size");
  new (cv) absl::CondVar;
}

void gpr_cv_destroy(gpr_cv* cv) {
  reinterpret_cast<absl::CondVar*>(cv)->~CondVar();
}

int gpr_cv_wait(gpr_cv* cv, gpr_mu* mu, gpr_timespec abs_deadline) {
  absl::CondVar* const c = reinterpret_cast<absl::CondVar*>(cv);
  absl::Mutex* const m = reinterpret_cast<absl::Mutex*>(mu);
  if (gpr_time_cmp(abs_deadline, gpr_inf_future(abs_deadline.clock_type)) ==
      0) {
    c->Wait(m);
    return 0;
  }
  // Use WaitWithTimeout if possible instead of WaitWithDeadline hoping that
  // it's going to use a monotonic clock.
  if (abs_deadline.clock_type == GPR_TIMESPAN) {
    return c->WaitWithTimeout(m, grpc_core::ToAbslDuration(abs_deadline));
  } else if (abs_deadline.clock_type == GPR_CLOCK_MONOTONIC) {
    absl::Duration duration = grpc_core::ToAbslDuration(
        gpr_time_sub(abs_deadline, gpr_now(GPR_CLOCK_MONOTONIC)));
    return c->WaitWithTimeout(m, duration);
  } else {
    return c->WaitWithDeadline(m, grpc_core::ToAbslTime(abs_deadline));
  }
}

void gpr_cv_signal(gpr_cv* cv) {
  reinterpret_cast<absl::CondVar*>(cv)->Signal();
}

void gpr_cv_broadcast(gpr_cv* cv) {
  reinterpret_cast<absl::CondVar*>(cv)->SignalAll();
}

//----------------------------------------

void gpr_once_init(gpr_once* once, void (*init_function)(void)) {
  static_assert(sizeof(gpr_once) == sizeof(absl::once_flag),
                "gpr_once and absl::once_flag must be the same size");
  absl::call_once(*reinterpret_cast<absl::once_flag*>(once), init_function);
}

#endif  // defined(GPR_ABSEIL_SYNC) && !defined(GPR_CUSTOM_SYNC)
