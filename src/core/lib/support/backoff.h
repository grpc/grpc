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

#ifndef GRPC_CORE_LIB_SUPPORT_BACKOFF_H
#define GRPC_CORE_LIB_SUPPORT_BACKOFF_H

#include <grpc/support/time.h>

typedef struct {
  /// const:  how long to wait after the first failure before retrying
  int64_t initial_connect_timeout;
  /// const: factor with which to multiply backoff after a failed retry
  double multiplier;
  /// const: amount to randomize backoffs
  double jitter;
  /// const: minimum time between retries in milliseconds
  int64_t min_timeout_millis;
  /// const: maximum time between retries in milliseconds
  int64_t max_timeout_millis;

  /// random number generator
  uint32_t rng_state;

  /// current retry timeout in milliseconds
  int64_t current_timeout_millis;
} gpr_backoff;

/// Initialize backoff machinery - does not need to be destroyed
void gpr_backoff_init(gpr_backoff *backoff, int64_t initial_connect_timeout,
                      double multiplier, double jitter,
                      int64_t min_timeout_millis, int64_t max_timeout_millis);

/// Begin retry loop: returns a timespec for the NEXT retry
gpr_timespec gpr_backoff_begin(gpr_backoff *backoff, gpr_timespec now);
/// Step a retry loop: returns a timespec for the NEXT retry
gpr_timespec gpr_backoff_step(gpr_backoff *backoff, gpr_timespec now);
/// Reset the backoff, so the next gpr_backoff_step will be a gpr_backoff_begin
/// instead
void gpr_backoff_reset(gpr_backoff *backoff);

#endif /* GRPC_CORE_LIB_SUPPORT_BACKOFF_H */
