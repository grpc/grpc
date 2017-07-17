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

#include "test/core/util/test_config.h"

static void test_constant_backoff(void) {
  grpc_backoff backoff;
  grpc_backoff_init(&backoff, 200 /* initial timeout */, 1.0 /* multiplier */,
                    0.0 /* jitter */, 100 /* min timeout */,
                    1000 /* max timeout */);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_millis next = grpc_backoff_begin(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 200);
  for (int i = 0; i < 10000; i++) {
    next = grpc_backoff_step(&exec_ctx, &backoff);
    GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 200);
    exec_ctx.now = next;
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_min_connect(void) {
  grpc_backoff backoff;
  grpc_backoff_init(&backoff, 100 /* initial timeout */, 1.0 /* multiplier */,
                    0.0 /* jitter */, 200 /* min timeout */,
                    1000 /* max timeout */);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_millis next = grpc_backoff_begin(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 200);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_no_jitter_backoff(void) {
  grpc_backoff backoff;
  grpc_backoff_init(&backoff, 2 /* initial timeout */, 2.0 /* multiplier */,
                    0.0 /* jitter */, 1 /* min timeout */,
                    513 /* max timeout */);
  // x_1 = 2
  // x_n = 2**i + x_{i-1} ( = 2**(n+1) - 2 )
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_millis next = grpc_backoff_begin(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 2);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 6);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 14);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 30);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 62);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 126);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 254);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 510);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 1022);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  // Hit the maximum timeout. From this point onwards, retries will increase
  // only by max timeout.
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 1535);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 2048);
  exec_ctx.now = next;
  next = grpc_backoff_step(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 2561);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_jitter_backoff(void) {
  const int64_t initial_timeout = 500;
  const double jitter = 0.1;
  grpc_backoff backoff;
  grpc_backoff_init(&backoff, initial_timeout, 1.0 /* multiplier */, jitter,
                    100 /* min timeout */, 1000 /* max timeout */);

  backoff.rng_state = 0;  // force consistent PRNG

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_millis next = grpc_backoff_begin(&exec_ctx, &backoff);
  GPR_ASSERT(next - grpc_exec_ctx_now(&exec_ctx) == 500);

  int64_t expected_next_lower_bound =
      (int64_t)((double)initial_timeout * (1 - jitter));
  int64_t expected_next_upper_bound =
      (int64_t)((double)initial_timeout * (1 + jitter));

  for (int i = 0; i < 10000; i++) {
    next = grpc_backoff_step(&exec_ctx, &backoff);

    // next-now must be within (jitter*100)% of the previous timeout.
    const int64_t timeout_millis = next - grpc_exec_ctx_now(&exec_ctx);
    GPR_ASSERT(timeout_millis >= expected_next_lower_bound);
    GPR_ASSERT(timeout_millis <= expected_next_upper_bound);

    expected_next_lower_bound =
        (int64_t)((double)timeout_millis * (1 - jitter));
    expected_next_upper_bound =
        (int64_t)((double)timeout_millis * (1 + jitter));
    exec_ctx.now = next;
  }
  grpc_exec_ctx_finish(&exec_ctx);
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
