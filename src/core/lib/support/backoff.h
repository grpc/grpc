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

#ifndef GRPC_CORE_LIB_SUPPORT_BACKOFF_H
#define GRPC_CORE_LIB_SUPPORT_BACKOFF_H

#include <grpc/support/time.h>

typedef struct {
  /// const: multiplier between retry attempts
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
void gpr_backoff_init(gpr_backoff *backoff, double multiplier, double jitter,
                      int64_t min_timeout_millis, int64_t max_timeout_millis);

/// Begin retry loop: returns a timespec for the NEXT retry
gpr_timespec gpr_backoff_begin(gpr_backoff *backoff, gpr_timespec now);
/// Step a retry loop: returns a timespec for the NEXT retry
gpr_timespec gpr_backoff_step(gpr_backoff *backoff, gpr_timespec now);
/// Reset the backoff, so the next gpr_backoff_step will be a gpr_backoff_begin
/// instead
void gpr_backoff_reset(gpr_backoff *backoff);

#endif /* GRPC_CORE_LIB_SUPPORT_BACKOFF_H */
