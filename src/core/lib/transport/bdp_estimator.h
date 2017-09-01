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
bool grpc_bdp_estimator_get_estimate(const grpc_bdp_estimator *estimator,
                                     int64_t *estimate);
// Tracks new bytes read.
bool grpc_bdp_estimator_get_bw(const grpc_bdp_estimator *estimator, double *bw);
// Returns true if the user should schedule a ping
void grpc_bdp_estimator_add_incoming_bytes(grpc_bdp_estimator *estimator,
                                           int64_t num_bytes);
// Returns true if the user should schedule a ping
bool grpc_bdp_estimator_need_ping(const grpc_bdp_estimator *estimator);
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
