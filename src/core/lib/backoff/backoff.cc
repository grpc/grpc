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

#include <grpc/support/useful.h>

void grpc_backoff_init(grpc_backoff* backoff, grpc_millis initial_backoff,
                       double multiplier, double jitter,
                       grpc_millis min_connect_timeout,
                       grpc_millis max_backoff) {
  backoff->initial_backoff = initial_backoff;
  backoff->multiplier = multiplier;
  backoff->jitter = jitter;
  backoff->min_connect_timeout = min_connect_timeout;
  backoff->max_backoff = max_backoff;
  backoff->rng_state = (uint32_t)gpr_now(GPR_CLOCK_REALTIME).tv_nsec;
}

grpc_backoff_result grpc_backoff_begin(grpc_exec_ctx* exec_ctx,
                                       grpc_backoff* backoff) {
  backoff->current_backoff = backoff->initial_backoff;
  const grpc_millis initial_timeout =
      GPR_MAX(backoff->initial_backoff, backoff->min_connect_timeout);
  const grpc_millis now = grpc_exec_ctx_now(exec_ctx);
  const grpc_backoff_result result = {now + initial_timeout,
                                      now + backoff->current_backoff};
  return result;
}

/* Generate a random number between 0 and 1. */
static double generate_uniform_random_number(uint32_t* rng_state) {
  *rng_state = (1103515245 * *rng_state + 12345) % ((uint32_t)1 << 31);
  return *rng_state / (double)((uint32_t)1 << 31);
}

static double generate_uniform_random_number_between(uint32_t* rng_state,
                                                     double a, double b) {
  if (a == b) return a;
  if (a > b) GPR_SWAP(double, a, b);  // make sure a < b
  const double range = b - a;
  return a + generate_uniform_random_number(rng_state) * range;
}

grpc_backoff_result grpc_backoff_step(grpc_exec_ctx* exec_ctx,
                                      grpc_backoff* backoff) {
  backoff->current_backoff = (grpc_millis)(GPR_MIN(
      backoff->current_backoff * backoff->multiplier, backoff->max_backoff));
  const double jitter = generate_uniform_random_number_between(
      &backoff->rng_state, -backoff->jitter * backoff->current_backoff,
      backoff->jitter * backoff->current_backoff);
  const grpc_millis current_timeout =
      GPR_MAX((grpc_millis)(backoff->current_backoff + jitter),
              backoff->min_connect_timeout);
  const grpc_millis next_timeout = GPR_MIN(
      (grpc_millis)(backoff->current_backoff + jitter), backoff->max_backoff);
  const grpc_millis now = grpc_exec_ctx_now(exec_ctx);
  const grpc_backoff_result result = {now + current_timeout,
                                      now + next_timeout};
  return result;
}

void grpc_backoff_reset(grpc_backoff* backoff) {
  backoff->current_backoff = backoff->initial_backoff;
}
