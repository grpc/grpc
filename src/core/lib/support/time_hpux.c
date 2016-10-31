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
#include "src/core/lib/support/time_precise.h"

#ifdef GPR_HPUX_TIME

#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/core/lib/support/block_annotate.h"

static struct timespec timespec_from_gpr(gpr_timespec gts) {
  struct timespec rv;
  if (sizeof(time_t) < sizeof(int64_t)) {
    /* fine to assert, as this is only used in gpr_sleep_until */
    GPR_ASSERT(gts.tv_sec <= INT32_MAX && gts.tv_sec >= INT32_MIN);
  }
  rv.tv_sec = (time_t)gts.tv_sec;
  rv.tv_nsec = gts.tv_nsec;
  return rv;
}

static gpr_timespec gpr_from_timespec(struct timespec ts,
                                      gpr_clock_type clock_type) {
  /*
   * timespec.tv_sec can have smaller size than gpr_timespec.tv_sec,
   * but we are only using this function to implement gpr_now
   * so there's no need to handle "infinity" values.
   */
  gpr_timespec rv;
  rv.tv_sec = ts.tv_sec;
  rv.tv_nsec = (int32_t)ts.tv_nsec;
  rv.clock_type = clock_type;
  return rv;
}

void gpr_time_init(void) { gpr_precise_clock_init(); }

static gpr_timespec now_impl(gpr_clock_type clock_type) {
  GPR_ASSERT(clock_type != GPR_TIMESPAN);
  if (clock_type == GPR_CLOCK_REALTIME) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return gpr_from_timespec(now, clock_type);
  }
  double usec = (double) gethrtime();
  gpr_timespec ret;
  ret.clock_type = clock_type;
  ret.tv_sec = (int64_t)(usec / 1e9);
  ret.tv_nsec = (int32_t)(usec - (double) clk->tv_sec * 1e9);
  return ret;
}

gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type) = now_impl;

gpr_timespec gpr_now(gpr_clock_type clock_type) {
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
    GRPC_SCHEDULING_START_BLOCKING_REGION;
    ns_result = nanosleep(&delta_ts, NULL);
    GRPC_SCHEDULING_END_BLOCKING_REGION;
    if (ns_result == 0) {
      break;
    }
  }
}

#endif /* GPR_HPUX_TIME */
