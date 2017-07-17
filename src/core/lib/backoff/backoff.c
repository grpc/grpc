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

void grpc_backoff_init(grpc_backoff *backoff, int64_t initial_connect_timeout,
                       double multiplier, double jitter,
                       int64_t min_timeout_millis, int64_t max_timeout_millis) {
  backoff->initial_connect_timeout = initial_connect_timeout;
  backoff->multiplier = multiplier;
  backoff->jitter = jitter;
  backoff->min_timeout_millis = min_timeout_millis;
  backoff->max_timeout_millis = max_timeout_millis;
  backoff->rng_state = (uint32_t)gpr_now(GPR_CLOCK_REALTIME).tv_nsec;
}

grpc_millis grpc_backoff_begin(grpc_exec_ctx *exec_ctx, grpc_backoff *backoff) {
  backoff->current_timeout_millis = backoff->initial_connect_timeout;
  const int64_t first_timeout =
      GPR_MAX(backoff->current_timeout_millis, backoff->min_timeout_millis);
  return grpc_exec_ctx_now(exec_ctx) + first_timeout;
}

/* Generate a random number between 0 and 1. */
static double generate_uniform_random_number(uint32_t *rng_state) {
  *rng_state = (1103515245 * *rng_state + 12345) % ((uint32_t)1 << 31);
  return *rng_state / (double)((uint32_t)1 << 31);
}

grpc_millis grpc_backoff_step(grpc_exec_ctx *exec_ctx, grpc_backoff *backoff) {
  const double new_timeout_millis =
      backoff->multiplier * (double)backoff->current_timeout_millis;
  backoff->current_timeout_millis =
      GPR_MIN((int64_t)new_timeout_millis, backoff->max_timeout_millis);

  const double jitter_range_width = backoff->jitter * new_timeout_millis;
  const double jitter =
      (2 * generate_uniform_random_number(&backoff->rng_state) - 1) *
      jitter_range_width;

  backoff->current_timeout_millis =
      (int64_t)((double)(backoff->current_timeout_millis) + jitter);

  const grpc_millis current_deadline =
      grpc_exec_ctx_now(exec_ctx) + backoff->current_timeout_millis;

  const grpc_millis min_deadline =
      grpc_exec_ctx_now(exec_ctx) + backoff->min_timeout_millis;

  return GPR_MAX(current_deadline, min_deadline);
}

void grpc_backoff_reset(grpc_backoff *backoff) {
  backoff->current_timeout_millis = backoff->initial_connect_timeout;
}
