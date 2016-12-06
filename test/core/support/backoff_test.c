/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/support/backoff.h"

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void test_constant_backoff(void) {
  gpr_backoff backoff;
  gpr_backoff_init(&backoff, 200 /* initial timeout */, 1.0 /* multiplier */,
                   0.0 /* jitter */, 100 /* min timeout */,
                   1000 /* max timeout */);

  gpr_timespec now = gpr_time_0(GPR_TIMESPAN);
  gpr_timespec next = gpr_backoff_begin(&backoff, now);
  GPR_ASSERT(gpr_time_to_millis(gpr_time_sub(next, now)) == 200);
  for (int i = 0; i < 10000; i++) {
    next = gpr_backoff_step(&backoff, now);
    GPR_ASSERT(gpr_time_to_millis(gpr_time_sub(next, now)) == 200);
    now = next;
  }
}

static void test_min_connect(void) {
  gpr_backoff backoff;
  gpr_backoff_init(&backoff, 100 /* initial timeout */, 1.0 /* multiplier */,
                   0.0 /* jitter */, 200 /* min timeout */,
                   1000 /* max timeout */);

  gpr_timespec now = gpr_time_0(GPR_TIMESPAN);
  gpr_timespec next = gpr_backoff_begin(&backoff, now);
  GPR_ASSERT(gpr_time_to_millis(gpr_time_sub(next, now)) == 200);
}

static void test_no_jitter_backoff(void) {
  gpr_backoff backoff;
  gpr_backoff_init(&backoff, 2 /* initial timeout */, 2.0 /* multiplier */,
                   0.0 /* jitter */, 1 /* min timeout */,
                   513 /* max timeout */);
  // x_1 = 2
  // x_n = 2**i + x_{i-1} ( = 2**(n+1) - 2 )
  gpr_timespec now = gpr_time_0(GPR_TIMESPAN);
  gpr_timespec next = gpr_backoff_begin(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(2, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(6, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(14, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(30, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(62, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(126, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(254, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(510, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(1022, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  // Hit the maximum timeout. From this point onwards, retries will increase
  // only by max timeout.
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(1535, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(2048, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(2561, GPR_TIMESPAN), next) == 0);
}

static void test_jitter_backoff(void) {
  const int64_t initial_timeout = 500;
  const double jitter = 0.1;
  gpr_backoff backoff;
  gpr_backoff_init(&backoff, initial_timeout, 1.0 /* multiplier */, jitter,
                   100 /* min timeout */, 1000 /* max timeout */);

  backoff.rng_state = 0;  // force consistent PRNG

  gpr_timespec now = gpr_time_0(GPR_TIMESPAN);
  gpr_timespec next = gpr_backoff_begin(&backoff, now);
  GPR_ASSERT(gpr_time_to_millis(gpr_time_sub(next, now)) == 500);

  int64_t expected_next_lower_bound =
      (int64_t)((double)initial_timeout * (1 - jitter));
  int64_t expected_next_upper_bound =
      (int64_t)((double)initial_timeout * (1 + jitter));

  for (int i = 0; i < 10000; i++) {
    next = gpr_backoff_step(&backoff, now);

    // next-now must be within (jitter*100)% of the previous timeout.
    const int64_t timeout_millis = gpr_time_to_millis(gpr_time_sub(next, now));
    GPR_ASSERT(timeout_millis >= expected_next_lower_bound);
    GPR_ASSERT(timeout_millis <= expected_next_upper_bound);

    expected_next_lower_bound =
        (int64_t)((double)timeout_millis * (1 - jitter));
    expected_next_upper_bound =
        (int64_t)((double)timeout_millis * (1 + jitter));
    now = next;
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  gpr_time_init();

  test_constant_backoff();
  test_min_connect();
  test_no_jitter_backoff();
  test_jitter_backoff();

  return 0;
}
