//
//
// Copyright 2015 gRPC authors.
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

#if defined(GPR_POSIX_SYNC) && !defined(GPR_ABSEIL_SYNC) && \
    !defined(GPR_CUSTOM_SYNC)

#include <errno.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <time.h>

#include "absl/log/check.h"
#include "src/core/util/crash.h"

void gpr_mu_init(gpr_mu* mu) {
#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_mutex_init(&mu->mutex, nullptr), 0);
  mu->leak_checker = static_cast<int*>(malloc(sizeof(*mu->leak_checker)));
  CHECK_NE(mu->leak_checker, nullptr);
#else
  CHECK_EQ(pthread_mutex_init(mu, nullptr), 0);
#endif
}

void gpr_mu_destroy(gpr_mu* mu) {
#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_mutex_destroy(&mu->mutex), 0);
  free(mu->leak_checker);
#else
  CHECK_EQ(pthread_mutex_destroy(mu), 0);
#endif
}

void gpr_mu_lock(gpr_mu* mu) {
#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_mutex_lock(&mu->mutex), 0);
#else
  CHECK_EQ(pthread_mutex_lock(mu), 0);
#endif
}

void gpr_mu_unlock(gpr_mu* mu) {
#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_mutex_unlock(&mu->mutex), 0);
#else
  CHECK_EQ(pthread_mutex_unlock(mu), 0);
#endif
}

int gpr_mu_trylock(gpr_mu* mu) {
  int err = 0;
#ifdef GRPC_ASAN_ENABLED
  err = pthread_mutex_trylock(&mu->mutex);
#else
  err = pthread_mutex_trylock(mu);
#endif
  CHECK(err == 0 || err == EBUSY);
  return err == 0;
}

//----------------------------------------

void gpr_cv_init(gpr_cv* cv) {
  pthread_condattr_t attr;
  CHECK_EQ(pthread_condattr_init(&attr), 0);
#if GPR_LINUX
  CHECK_EQ(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC), 0);
#endif  // GPR_LINUX

#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_cond_init(&cv->cond_var, &attr), 0);
  cv->leak_checker = static_cast<int*>(malloc(sizeof(*cv->leak_checker)));
  CHECK_NE(cv->leak_checker, nullptr);
#else
  CHECK_EQ(pthread_cond_init(cv, &attr), 0);
#endif
}

void gpr_cv_destroy(gpr_cv* cv) {
#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_cond_destroy(&cv->cond_var), 0);
  free(cv->leak_checker);
#else
  CHECK_EQ(pthread_cond_destroy(cv), 0);
#endif
}

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
    abs_deadline = gpr_convert_clock_type(abs_deadline, GPR_CLOCK_MONOTONIC);
#else
    abs_deadline = gpr_convert_clock_type(abs_deadline, GPR_CLOCK_REALTIME);
    abs_deadline = gpr_time_max(abs_deadline, gpr_now(abs_deadline.clock_type));
#endif  // GPR_LINUX
    abs_deadline_ts.tv_sec = static_cast<time_t>(abs_deadline.tv_sec);
    abs_deadline_ts.tv_nsec = abs_deadline.tv_nsec;
#ifdef GRPC_ASAN_ENABLED
    err = pthread_cond_timedwait(&cv->cond_var, &mu->mutex, &abs_deadline_ts);
#else
    err = pthread_cond_timedwait(cv, mu, &abs_deadline_ts);
#endif
  }
  CHECK(err == 0 || err == ETIMEDOUT || err == EAGAIN);
  return err == ETIMEDOUT;
}

void gpr_cv_signal(gpr_cv* cv) {
#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_cond_signal(&cv->cond_var), 0);
#else
  CHECK_EQ(pthread_cond_signal(cv), 0);
#endif
}

void gpr_cv_broadcast(gpr_cv* cv) {
#ifdef GRPC_ASAN_ENABLED
  CHECK_EQ(pthread_cond_broadcast(&cv->cond_var), 0);
#else
  CHECK_EQ(pthread_cond_broadcast(cv), 0);
#endif
}

//----------------------------------------

void gpr_once_init(gpr_once* once, void (*init_function)(void)) {
  CHECK_EQ(pthread_once(once, init_function), 0);
}

#endif  // defined(GPR_POSIX_SYNC) && !defined(GPR_ABSEIL_SYNC) &&
        // !defined(GPR_CUSTOM_SYNC)
