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

#ifndef GRPC_CORE_LIB_TRANSPORT_BDP_ESTIMATOR_H
#define GRPC_CORE_LIB_TRANSPORT_BDP_ESTIMATOR_H

#include <grpc/support/time.h>
#include <stdbool.h>
#include <stdint.h>
#include "src/core/lib/debug/trace.h"

#define GRPC_BDP_SAMPLES 16
#define GRPC_BDP_MIN_SAMPLES_FOR_ESTIMATE 3

extern grpc_tracer_flag grpc_bdp_estimator_trace;

typedef enum {
  GRPC_BDP_PING_UNSCHEDULED,
  GRPC_BDP_PING_SCHEDULED,
  GRPC_BDP_PING_STARTED
} grpc_bdp_estimator_ping_state;

typedef struct grpc_bdp_estimator {
  grpc_bdp_estimator_ping_state ping_state;
  int64_t accumulator;
  int64_t estimate;
  gpr_timespec ping_start_time;
  double bw_est;
  const char *name;
} grpc_bdp_estimator;

void grpc_bdp_estimator_init(grpc_bdp_estimator *estimator, const char *name);

// Returns true if a reasonable estimate could be obtained
bool grpc_bdp_estimator_get_estimate(grpc_bdp_estimator *estimator,
                                     int64_t *estimate);
// Returns true if a reasonable estimate could be obtained
bool grpc_bdp_estimator_get_bw(grpc_bdp_estimator *estimator, double *bw);
// Returns true if the user should schedule a ping
bool grpc_bdp_estimator_add_incoming_bytes(grpc_bdp_estimator *estimator,
                                           int64_t num_bytes);
// Schedule a ping: call in response to receiving a true from
// grpc_bdp_estimator_add_incoming_bytes once a ping has been scheduled by a
// transport (but not necessarily started)
void grpc_bdp_estimator_schedule_ping(grpc_bdp_estimator *estimator);
// Start a ping: call after calling grpc_bdp_estimator_schedule_ping and once
// the ping is on the wire
void grpc_bdp_estimator_start_ping(grpc_bdp_estimator *estimator);
// Completes a previously started ping
void grpc_bdp_estimator_complete_ping(grpc_bdp_estimator *estimator);

#endif /* GRPC_CORE_LIB_TRANSPORT_BDP_ESTIMATOR_H */
