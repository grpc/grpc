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

static struct timespec timespec_from_gpr(gpr_timespec gts) {
  struct timespec rv;
  if (sizeof(time_t) < sizeof(int64_t)) {
    /* fine to assert, as this is only used in gpr_sleep_until */
    GPR_ASSERT(gts.tv_sec <= INT32_MAX && gts.tv_sec >= INT32_MIN);
  }
  rv.tv_sec = static_cast<time_t>(gts.tv_sec);
  rv.tv_nsec = gts.tv_nsec;
  return rv;
}

#if _POSIX_TIMERS > 0 || defined(__OpenBSD__)
static gpr_timespec gpr_from_timespec(struct timespec ts,
                                      gpr_clock_type clock_type) {
  /*
   * timespec.tv_sec can have smaller size than gpr_timespec.tv_sec,
   * but we are only using this function to implement gpr_now
   * so there's no need to handle "infinity" values.
   */
  gpr_timespec rv;
  rv.tv_sec = ts.tv_sec;
  rv.tv_nsec = static_cast<int32_t>(ts.tv_nsec);
  rv.clock_type = clock_type;
  return rv;
}

/** maps gpr_clock_type --> clockid_t for clock_gettime */
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
    /* avoid ABI problems by invoking syscalls directly */
    syscall(SYS_clock_gettime, clockid_for_gpr_clock[clock_type], &now);
#else
    clock_gettime(clockid_for_gpr_clock[clock_type], &now);
#endif
    return gpr_from_timespec(now, clock_type);
  }
}
#else
  /* For some reason Apple's OSes haven't implemented clock_gettime. */

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/time.h>

static double g_time_scale;
static uint64_t g_time_start;

void gpr_time_init(void) {
  mach_timebase_info_data_t tb = {0, 1};
  gpr_precise_clock_init();
  mach_timebase_info(&tb);
  g_time_scale = tb.numer;
  g_time_scale /= tb.denom;
  g_time_start = mach_absolute_time();
}

static gpr_timespec now_impl(gpr_clock_type clock) {
  gpr_timespec now;
  struct timeval now_tv;
  double now_dbl;

  now.clock_type = clock;
  switch (clock) {
    case GPR_CLOCK_REALTIME:
      gettimeofday(&now_tv, nullptr);
      now.tv_sec = now_tv.tv_sec;
      now.tv_nsec = now_tv.tv_usec * 1000;
      break;
    case GPR_CLOCK_MONOTONIC:
      now_dbl = ((double)(mach_absolute_time() - g_time_start)) * g_time_scale;
      now.tv_sec = (int64_t)(now_dbl * 1e-9);
      now.tv_nsec = (int32_t)(now_dbl - ((double)now.tv_sec) * 1e9);
      break;
    case GPR_CLOCK_PRECISE:
      gpr_precise_clock_now(&now);
      break;
    case GPR_TIMESPAN:
      abort();
  }

  return now;
}
#endif

gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type) = now_impl;

#ifdef GPR_LOW_LEVEL_COUNTERS
gpr_atm gpr_now_call_count;
#endif

gpr_timespec gpr_now(gpr_clock_type clock_type) {
#ifdef GPR_LOW_LEVEL_COUNTERS
  __atomic_fetch_add(&gpr_now_call_count, 1, __ATOMIC_RELAXED);
#endif
  return gpr_now_impl(clock_type);
}

void gpr_sleep_until(gpr_timespec until) {
  gpr_timespec now;
  gpr_timespec delta;
  struct timespec delta_ts;
  int ns_result;

  for (;;) {
    /* We could simplify by using clock_nanosleep instead, but it might be
     * slightly less portable. */
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

#endif /* GPR_POSIX_TIME */
