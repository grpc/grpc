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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /// const:  how long to wait after the first failure before retrying
  grpc_millis initial_backoff;

  /// const: factor with which to multiply backoff after a failed retry
  double multiplier;

  /// const: amount to randomize backoffs
  double jitter;

  /// const: minimum time between retries
  grpc_millis min_connect_timeout;

  /// const: maximum time between retries
  grpc_millis max_backoff;

  /// current delay before retries
  grpc_millis current_backoff;

  /// random number generator
  uint32_t rng_state;
} grpc_backoff;

typedef struct {
  /// Deadline to be used for the current attempt.
  grpc_millis current_deadline;

  /// Deadline to be used for the next attempt, following the backoff strategy.
  grpc_millis next_attempt_start_time;
} grpc_backoff_result;

/// Initialize backoff machinery - does not need to be destroyed
void grpc_backoff_init(grpc_backoff* backoff, grpc_millis initial_backoff,
                       double multiplier, double jitter,
                       grpc_millis min_connect_timeout,
                       grpc_millis max_backoff);

/// Begin retry loop: returns the deadlines to be used for the current attempt
/// and the subsequent retry, if any.
grpc_backoff_result grpc_backoff_begin(grpc_exec_ctx* exec_ctx,
                                       grpc_backoff* backoff);

/// Step a retry loop: returns the deadlines to be used for the current attempt
/// and the subsequent retry, if any.
grpc_backoff_result grpc_backoff_step(grpc_exec_ctx* exec_ctx,
                                      grpc_backoff* backoff);

/// Reset the backoff, so the next grpc_backoff_step will be a
/// grpc_backoff_begin.
void grpc_backoff_reset(grpc_backoff* backoff);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_BACKOFF_BACKOFF_H */
