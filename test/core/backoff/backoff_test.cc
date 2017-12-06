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

#include "src/core/lib/backoff/backoff.h"

#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "test/core/util/test_config.h"

static void test_constant_backoff(void) {
  grpc_backoff backoff;
  const grpc_millis initial_backoff = 200;
  const double multiplier = 1.0;
  const double jitter = 0.0;
  const grpc_millis min_connect_timeout = 100;
  const grpc_millis max_backoff = 1000;
  grpc_backoff_init(&backoff, initial_backoff, multiplier, jitter,
                    min_connect_timeout, max_backoff);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_backoff_result next_deadlines = grpc_backoff_begin(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline - grpc_exec_ctx_now(&exec_ctx) ==
             initial_backoff);
  GPR_ASSERT(next_deadlines.next_attempt_start_time -
                 grpc_exec_ctx_now(&exec_ctx) ==
             initial_backoff);
  for (int i = 0; i < 10000; i++) {
    next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
    GPR_ASSERT(next_deadlines.current_deadline - grpc_exec_ctx_now(&exec_ctx) ==
               initial_backoff);
    GPR_ASSERT(next_deadlines.next_attempt_start_time -
                   grpc_exec_ctx_now(&exec_ctx) ==
               initial_backoff);
    exec_ctx.now = next_deadlines.current_deadline;
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_min_connect(void) {
  grpc_backoff backoff;
  const grpc_millis initial_backoff = 100;
  const double multiplier = 1.0;
  const double jitter = 0.0;
  const grpc_millis min_connect_timeout = 200;
  const grpc_millis max_backoff = 1000;
  grpc_backoff_init(&backoff, initial_backoff, multiplier, jitter,
                    min_connect_timeout, max_backoff);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_backoff_result next = grpc_backoff_begin(&exec_ctx, &backoff);
  // Because the min_connect_timeout > initial_backoff, current_deadline is used
  // as the deadline for the current attempt.
  GPR_ASSERT(next.current_deadline - grpc_exec_ctx_now(&exec_ctx) ==
             min_connect_timeout);
  // ... while, if the current attempt fails, the next one will happen after
  // initial_backoff.
  GPR_ASSERT(next.next_attempt_start_time - grpc_exec_ctx_now(&exec_ctx) ==
             initial_backoff);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_jitter_backoff(void) {
  grpc_backoff backoff;
  const grpc_millis initial_backoff = 2;
  const double multiplier = 2.0;
  const double jitter = 0.0;
  const grpc_millis min_connect_timeout = 1;
  const grpc_millis max_backoff = 513;
  grpc_backoff_init(&backoff, initial_backoff, multiplier, jitter,
                    min_connect_timeout, max_backoff);
  // x_1 = 2
  // x_n = 2**i + x_{i-1} ( = 2**(n+1) - 2 )
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  exec_ctx.now = 0;
  exec_ctx.now_is_valid = true;
  grpc_backoff_result next_deadlines = grpc_backoff_begin(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline ==
             next_deadlines.next_attempt_start_time);
  GPR_ASSERT(next_deadlines.current_deadline == 2);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 6);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 14);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 30);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 62);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 126);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 254);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 510);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 1022);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  // Hit the maximum timeout. From this point onwards, retries will increase
  // only by max timeout.
  GPR_ASSERT(next_deadlines.current_deadline == 1535);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 2048);
  exec_ctx.now = next_deadlines.current_deadline;
  next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline == 2561);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_jitter_backoff(void) {
  const grpc_millis initial_backoff = 500;
  grpc_millis current_backoff = initial_backoff;
  const grpc_millis max_backoff = 1000;
  const grpc_millis min_connect_timeout = 100;
  const double multiplier = 1.0;
  const double jitter = 0.1;
  grpc_backoff backoff;
  grpc_backoff_init(&backoff, initial_backoff, multiplier, jitter,
                    min_connect_timeout, max_backoff);

  backoff.rng_state = 0;  // force consistent PRNG

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_backoff_result next_deadlines = grpc_backoff_begin(&exec_ctx, &backoff);
  GPR_ASSERT(next_deadlines.current_deadline - grpc_exec_ctx_now(&exec_ctx) ==
             initial_backoff);
  GPR_ASSERT(next_deadlines.next_attempt_start_time -
                 grpc_exec_ctx_now(&exec_ctx) ==
             initial_backoff);

  grpc_millis expected_next_lower_bound =
      (grpc_millis)((double)current_backoff * (1 - jitter));
  grpc_millis expected_next_upper_bound =
      (grpc_millis)((double)current_backoff * (1 + jitter));

  for (int i = 0; i < 10000; i++) {
    next_deadlines = grpc_backoff_step(&exec_ctx, &backoff);
    // next-now must be within (jitter*100)% of the current backoff (which
    // increases by * multiplier up to max_backoff).
    const grpc_millis timeout_millis =
        next_deadlines.current_deadline - grpc_exec_ctx_now(&exec_ctx);
    GPR_ASSERT(timeout_millis >= expected_next_lower_bound);
    GPR_ASSERT(timeout_millis <= expected_next_upper_bound);
    current_backoff = GPR_MIN(
        (grpc_millis)((double)current_backoff * multiplier), max_backoff);
    expected_next_lower_bound =
        (grpc_millis)((double)current_backoff * (1 - jitter));
    expected_next_upper_bound =
        (grpc_millis)((double)current_backoff * (1 + jitter));
    exec_ctx.now = next_deadlines.current_deadline;
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  gpr_time_init();

  test_constant_backoff();
  test_min_connect();
  test_no_jitter_backoff();
  test_jitter_backoff();

  return 0;
}
