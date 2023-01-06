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

#include "src/core/lib/gpr/time_precise.h"

#ifdef GPR_POSIX_TIME

#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/crash.h"

static struct timespec timespec_from_gpr(gpr_timespec gts) {
  struct timespec rv;
  if (sizeof(time_t) < sizeof(int64_t)) {
    // fine to assert, as this is only used in gpr_sleep_until
    GPR_ASSERT(gts.tv_sec <= INT32_MAX && gts.tv_sec >= INT32_MIN);
  }
  rv.tv_sec = static_cast<time_t>(gts.tv_sec);
  rv.tv_nsec = gts.tv_nsec;
  return rv;
}

#if _POSIX_TIMERS > 0 || defined(__OpenBSD__)
static gpr_timespec gpr_from_timespec(struct timespec ts,
                                      gpr_clock_type clock_type) {
  //
  // timespec.tv_sec can have smaller size than gpr_timespec.tv_sec,
  // but we are only using this function to implement gpr_now
  // so there's no need to handle "infinity" values.
  //
  gpr_timespec rv;
  rv.tv_sec = ts.tv_sec;
  rv.tv_nsec = static_cast<int32_t>(ts.tv_nsec);
  rv.clock_type = clock_type;
  return rv;
}

/// maps gpr_clock_type --> clockid_t for clock_gettime
static const clockid_t clockid_for_gpr_clock[] = {CLOCK_MONOTONIC,
                                                  CLOCK_REALTIME};

void gpr_time_init(void) { gpr_precise_clock_init(); }

static gpr_timespec now_impl(gpr_clock_type clock_type) {
  struct timespec now;
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  if (clock_type == GPR_CLOCK_PRECISE) {
    gpr_timespec ret;
    gpr_precise_clock_now(&ret);
    return ret;
  } else {
#if defined(GPR_BACKWARDS_COMPATIBILITY_MODE) && defined(__linux__)
    // avoid ABI problems by invoking syscalls directly
    syscall(SYS_clock_gettime, clockid_for_gpr_clock[clock_type], &now);
#else
    clock_gettime(clockid_for_gpr_clock[clock_type], &now);
#endif
    return gpr_from_timespec(now, clock_type);
  }
}
#else
// For some reason Apple's OSes haven't implemented clock_gettime.

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/time.h>

static double g_time_scale = []() {
  mach_timebase_info_data_t tb = {0, 1};
  mach_timebase_info(&tb);
  return static_cast<double>(tb.numer) / static_cast<double>(tb.denom);
}();
static uint64_t g_time_start = mach_absolute_time();

void gpr_time_init(void) { gpr_precise_clock_init(); }

static gpr_timespec now_impl(gpr_clock_type clock) {
  gpr_timespec now;
  struct timeval now_tv;
  double now_dbl;

  now.clock_type = clock;
  switch (clock) {
    case GPR_CLOCK_REALTIME:
      // gettimeofday(...) function may return with a value whose tv_usec is
      // greater than 1e6 on iOS The case is resolved with the guard at end of
      // this function.
      gettimeofday(&now_tv, nullptr);
      now.tv_sec = now_tv.tv_sec;
      now.tv_nsec = now_tv.tv_usec * 1000;
      break;
    case GPR_CLOCK_MONOTONIC:
      // Add 5 seconds arbitrarily: avoids weird conditions in gprpp/time.cc
      // when there's a small number of seconds returned.
      now_dbl = 5.0e9 +
                ((double)(mach_absolute_time() - g_time_start)) * g_time_scale;
      now.tv_sec = (int64_t)(now_dbl * 1e-9);
      now.tv_nsec = (int32_t)(now_dbl - ((double)now.tv_sec) * 1e9);
      break;
    case GPR_CLOCK_PRECISE:
      gpr_precise_clock_now(&now);
      break;
    case GPR_TIMESPAN:
      abort();
  }

  // Guard the tv_nsec field in valid range for all clock types
  while (GPR_UNLIKELY(now.tv_nsec >= 1e9)) {
    now.tv_sec++;
    now.tv_nsec -= 1e9;
  }
  while (GPR_UNLIKELY(now.tv_nsec < 0)) {
    now.tv_sec--;
    now.tv_nsec += 1e9;
  }

  return now;
}
#endif

gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type) = now_impl;

gpr_timespec gpr_now(gpr_clock_type clock_type) {
  // validate clock type
  GPR_ASSERT(clock_type == GPR_CLOCK_MONOTONIC ||
             clock_type == GPR_CLOCK_REALTIME ||
             clock_type == GPR_CLOCK_PRECISE);
  gpr_timespec ts = gpr_now_impl(clock_type);
  // tv_nsecs must be in the range [0, 1e9).
  GPR_ASSERT(ts.tv_nsec >= 0 && ts.tv_nsec < 1e9);
  return ts;
}

void gpr_sleep_until(gpr_timespec until) {
  gpr_timespec now;
  gpr_timespec delta;
  struct timespec delta_ts;
  int ns_result;

  for (;;) {
    // We could simplify by using clock_nanosleep instead, but it might be
    // slightly less portable.
    now = gpr_now(until.clock_type);
    if (gpr_time_cmp(until, now) <= 0) {
      return;
    }

    delta = gpr_time_sub(until, now);
    delta_ts = timespec_from_gpr(delta);
    ns_result = nanosleep(&delta_ts, nullptr);
    if (ns_result == 0) {
      break;
    }
  }
}

#endif  // GPR_POSIX_TIME
