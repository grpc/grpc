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

#ifndef GRPC_CORE_LIB_BACKOFF_BACKOFF_H
#define GRPC_CORE_LIB_BACKOFF_BACKOFF_H

#include "src/core/lib/iomgr/exec_ctx.h"

typedef struct {
  /// const:  how long to wait after the first failure before retrying
  grpc_millis initial_connect_timeout;
  /// const: factor with which to multiply backoff after a failed retry
  double multiplier;
  /// const: amount to randomize backoffs
  double jitter;
  /// const: minimum time between retries in milliseconds
  grpc_millis min_timeout_millis;
  /// const: maximum time between retries in milliseconds
  grpc_millis max_timeout_millis;

  /// random number generator
  uint32_t rng_state;

  /// current retry timeout in milliseconds
  int64_t current_timeout_millis;
} grpc_backoff;

/// Initialize backoff machinery - does not need to be destroyed
void grpc_backoff_init(grpc_backoff *backoff,
                       grpc_millis initial_connect_timeout, double multiplier,
                       double jitter, grpc_millis min_timeout_millis,
                       grpc_millis max_timeout_millis);

/// Begin retry loop: returns a timespec for the NEXT retry
grpc_millis grpc_backoff_begin(grpc_exec_ctx *exec_ctx, grpc_backoff *backoff);
/// Step a retry loop: returns a timespec for the NEXT retry
grpc_millis grpc_backoff_step(grpc_exec_ctx *exec_ctx, grpc_backoff *backoff);
/// Reset the backoff, so the next grpc_backoff_step will be a
/// grpc_backoff_begin
/// instead
void grpc_backoff_reset(grpc_backoff *backoff);

#endif /* GRPC_CORE_LIB_BACKOFF_BACKOFF_H */
