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

#include <grpc/support/useful.h>

void gpr_backoff_init(gpr_backoff *backoff, double multiplier, double jitter,
                      int64_t min_timeout_millis, int64_t max_timeout_millis) {
  backoff->multiplier = multiplier;
  backoff->jitter = jitter;
  backoff->min_timeout_millis = min_timeout_millis;
  backoff->max_timeout_millis = max_timeout_millis;
  backoff->rng_state = (uint32_t)gpr_now(GPR_CLOCK_REALTIME).tv_nsec;
}

gpr_timespec gpr_backoff_begin(gpr_backoff *backoff, gpr_timespec now) {
  backoff->current_timeout_millis = backoff->min_timeout_millis;
  return gpr_time_add(
      now, gpr_time_from_millis(backoff->current_timeout_millis, GPR_TIMESPAN));
}

/* Generate a random number between 0 and 1. */
static double generate_uniform_random_number(uint32_t *rng_state) {
  *rng_state = (1103515245 * *rng_state + 12345) % ((uint32_t)1 << 31);
  return *rng_state / (double)((uint32_t)1 << 31);
}

gpr_timespec gpr_backoff_step(gpr_backoff *backoff, gpr_timespec now) {
  double new_timeout_millis =
      backoff->multiplier * (double)backoff->current_timeout_millis;
  double jitter_range = backoff->jitter * new_timeout_millis;
  double jitter =
      (2 * generate_uniform_random_number(&backoff->rng_state) - 1) *
      jitter_range;
  backoff->current_timeout_millis =
      GPR_CLAMP((int64_t)(new_timeout_millis + jitter),
                backoff->min_timeout_millis, backoff->max_timeout_millis);
  return gpr_time_add(
      now, gpr_time_from_millis(backoff->current_timeout_millis, GPR_TIMESPAN));
}

void gpr_backoff_reset(gpr_backoff *backoff) {
  // forces step() to return a timeout of min_timeout_millis
  backoff->current_timeout_millis = 0;
}
