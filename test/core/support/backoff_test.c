/*
 *
 * Copyright 2016 gRPC authors.
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
