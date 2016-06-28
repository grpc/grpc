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

/* Win32 code for gpr time support. */

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS_TIME

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <limits.h>
#include <process.h>
#include <sys/timeb.h>

#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/time_precise.h"

static LARGE_INTEGER g_start_time;
static double g_time_scale;

void gpr_time_init(void) {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&g_start_time);
  g_time_scale = 1.0 / (double)frequency.QuadPart;
}

gpr_timespec gpr_now(gpr_clock_type clock) {
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
    GRPC_SCHEDULING_START_BLOCKING_REGION;
    Sleep((DWORD)sleep_millis);
    GRPC_SCHEDULING_END_BLOCKING_REGION;
  }
}

#endif /* GPR_WINDOWS_TIME */
