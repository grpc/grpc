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

gpr_timespec gpr_now(void) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return gpr_from_timespec(now);
}
#else
/* For some reason Apple's OSes haven't implemented clock_gettime. */

#include <sys/time.h>

gpr_timespec gpr_now(void) {
  gpr_timespec now;
  struct timeval now_tv;
  gettimeofday(&now_tv, NULL);
  now.tv_sec = now_tv.tv_sec;
  now.tv_nsec = now_tv.tv_usec * 1000;
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
    now = gpr_now();
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
