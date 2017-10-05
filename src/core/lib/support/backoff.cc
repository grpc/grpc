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

#include <grpc/support/useful.h>

void gpr_backoff_init(gpr_backoff *backoff, int64_t initial_connect_timeout,
                      double multiplier, double jitter,
                      int64_t min_timeout_millis, int64_t max_timeout_millis) {
  backoff->initial_connect_timeout = initial_connect_timeout;
  backoff->multiplier = multiplier;
  backoff->jitter = jitter;
  backoff->min_timeout_millis = min_timeout_millis;
  backoff->max_timeout_millis = max_timeout_millis;
  backoff->rng_state = (uint32_t)gpr_now(GPR_CLOCK_REALTIME).tv_nsec;
}

gpr_timespec gpr_backoff_begin(gpr_backoff *backoff, gpr_timespec now) {
  backoff->current_timeout_millis = backoff->initial_connect_timeout;
  const int64_t first_timeout =
      GPR_MAX(backoff->current_timeout_millis, backoff->min_timeout_millis);
  return gpr_time_add(now, gpr_time_from_millis(first_timeout, GPR_TIMESPAN));
}

/* Generate a random number between 0 and 1. */
static double generate_uniform_random_number(uint32_t *rng_state) {
  *rng_state = (1103515245 * *rng_state + 12345) % ((uint32_t)1 << 31);
  return *rng_state / (double)((uint32_t)1 << 31);
}

gpr_timespec gpr_backoff_step(gpr_backoff *backoff, gpr_timespec now) {
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

  const gpr_timespec current_deadline = gpr_time_add(
      now, gpr_time_from_millis(backoff->current_timeout_millis, GPR_TIMESPAN));

  const gpr_timespec min_deadline = gpr_time_add(
      now, gpr_time_from_millis(backoff->min_timeout_millis, GPR_TIMESPAN));

  return gpr_time_max(current_deadline, min_deadline);
}

void gpr_backoff_reset(gpr_backoff *backoff) {
  backoff->current_timeout_millis = backoff->initial_connect_timeout;
}
