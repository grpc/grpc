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

/* Win32 code for gpr time support. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_TIME

#include <limits.h>
#include <process.h>
#include <sys/timeb.h>

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/time_precise.h"

static LARGE_INTEGER g_start_time;
static double g_time_scale;

void gpr_time_init(void) {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&g_start_time);
  g_time_scale = 1.0 / (double)frequency.QuadPart;
}

static gpr_timespec now_impl(gpr_clock_type clock) {
  gpr_timespec now_tv;
  LONGLONG diff;
  struct _timeb now_tb;
  LARGE_INTEGER timestamp;
  double now_dbl;
  now_tv.clock_type = clock;
  switch (clock) {
    case GPR_CLOCK_REALTIME:
      _ftime_s(&now_tb);
      now_tv.tv_sec = (int64_t)now_tb.time;
      now_tv.tv_nsec = now_tb.millitm * 1000000;
      break;
    case GPR_CLOCK_MONOTONIC:
    case GPR_CLOCK_PRECISE:
      QueryPerformanceCounter(&timestamp);
      diff = timestamp.QuadPart - g_start_time.QuadPart;
      now_dbl = (double)diff * g_time_scale;
      now_tv.tv_sec = (int64_t)now_dbl;
      now_tv.tv_nsec = (int32_t)((now_dbl - (double)now_tv.tv_sec) * 1e9);
      break;
    case GPR_TIMESPAN:
      abort();
      break;
  }
  return now_tv;
}

gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type) = now_impl;

gpr_timespec gpr_now(gpr_clock_type clock_type) {
  return gpr_now_impl(clock_type);
}

void gpr_sleep_until(gpr_timespec until) {
  gpr_timespec now;
  gpr_timespec delta;
  int64_t sleep_millis;

  for (;;) {
    /* We could simplify by using clock_nanosleep instead, but it might be
     * slightly less portable. */
    now = gpr_now(until.clock_type);
    if (gpr_time_cmp(until, now) <= 0) {
      return;
    }

    delta = gpr_time_sub(until, now);
    sleep_millis =
        delta.tv_sec * GPR_MS_PER_SEC + delta.tv_nsec / GPR_NS_PER_MS;
    GPR_ASSERT((sleep_millis >= 0) && (sleep_millis <= INT_MAX));
    Sleep((DWORD)sleep_millis);
  }
}

#endif /* GPR_WINDOWS_TIME */
