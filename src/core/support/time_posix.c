/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_TIME

#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <grpc/support/time.h>

static struct timespec timespec_from_gpr(gpr_timespec gts) {
  struct timespec rv;
  rv.tv_sec = gts.tv_sec;
  rv.tv_nsec = gts.tv_nsec;
  return rv;
}

#if _POSIX_TIMERS > 0
static gpr_timespec gpr_from_timespec(struct timespec ts) {
  gpr_timespec rv;
  rv.tv_sec = ts.tv_sec;
  rv.tv_nsec = (int)ts.tv_nsec;
  return rv;
}

/** maps gpr_clock_type --> clockid_t for clock_gettime */
static clockid_t clockid_for_gpr_clock[] = {CLOCK_MONOTONIC, CLOCK_REALTIME};

void gpr_time_init(void) {}

gpr_timespec gpr_now(gpr_clock_type clock) {
  struct timespec now;
  clock_gettime(clockid_for_gpr_clock[clock], &now);
  return gpr_from_timespec(now);
}
#else
/* For some reason Apple's OSes haven't implemented clock_gettime. */

#include <sys/time.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

static double g_time_scale;
static uint64_t g_time_start;

void gpr_time_init(void) {
  mach_timebase_info_data_t tb = {0, 1};
  mach_timebase_info(&tb);
  g_time_scale = tb.numer;
  g_time_scale /= tb.denom;
  g_time_start = mach_absolute_time();
}

gpr_timespec gpr_now(gpr_clock_type clock) {
  gpr_timespec now;
  struct timeval now_tv;
  double now_dbl;

  switch (clock) {
    case GPR_CLOCK_REALTIME:
      gettimeofday(&now_tv, NULL);
      now.tv_sec = now_tv.tv_sec;
      now.tv_nsec = now_tv.tv_usec * 1000;
      break;
    case GPR_CLOCK_MONOTONIC:
      now_dbl = (mach_absolute_time() - g_time_start) * g_time_scale;
      now.tv_sec = now_dbl * 1e-9;
      now.tv_nsec = now_dbl - now.tv_sec * 1e9;
      break;
  }

  return now;
}
#endif

void gpr_sleep_until(gpr_timespec until) {
  gpr_timespec now;
  gpr_timespec delta;
  struct timespec delta_ts;

  for (;;) {
    /* We could simplify by using clock_nanosleep instead, but it might be
     * slightly less portable. */
    now = gpr_now(GPR_CLOCK_REALTIME);
    if (gpr_time_cmp(until, now) <= 0) {
      return;
    }

    delta = gpr_time_sub(until, now);
    delta_ts = timespec_from_gpr(delta);
    if (nanosleep(&delta_ts, NULL) == 0) {
      break;
    }
  }
}

#endif /* GPR_POSIX_TIME */
