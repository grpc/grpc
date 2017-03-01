/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/transport/bdp_estimator.h"

#include <stdlib.h>

#include <grpc/support/log.h>
#include <grpc/support/useful.h>

int grpc_bdp_estimator_trace = 0;

void grpc_bdp_estimator_init(grpc_bdp_estimator *estimator, const char *name) {
  estimator->estimate = 65536;
  estimator->ping_state = GRPC_BDP_PING_UNSCHEDULED;
  estimator->name = name;
}

bool grpc_bdp_estimator_get_estimate(grpc_bdp_estimator *estimator,
                                     int64_t *estimate) {
  *estimate = estimator->estimate;
  return true;
}

bool grpc_bdp_estimator_add_incoming_bytes(grpc_bdp_estimator *estimator,
                                           int64_t num_bytes) {
  estimator->accumulator += num_bytes;
  switch (estimator->ping_state) {
    case GRPC_BDP_PING_UNSCHEDULED:
      return true;
    case GRPC_BDP_PING_SCHEDULED:
      return false;
    case GRPC_BDP_PING_STARTED:
      return false;
  }
  GPR_UNREACHABLE_CODE(return false);
}

void grpc_bdp_estimator_schedule_ping(grpc_bdp_estimator *estimator) {
  if (grpc_bdp_estimator_trace) {
    gpr_log(GPR_DEBUG, "bdp[%s]:sched acc=%" PRId64 " est=%" PRId64,
            estimator->name, estimator->accumulator, estimator->estimate);
  }
  GPR_ASSERT(estimator->ping_state == GRPC_BDP_PING_UNSCHEDULED);
  estimator->ping_state = GRPC_BDP_PING_SCHEDULED;
  estimator->accumulator = 0;
}

void grpc_bdp_estimator_start_ping(grpc_bdp_estimator *estimator) {
  if (grpc_bdp_estimator_trace) {
    gpr_log(GPR_DEBUG, "bdp[%s]:start acc=%" PRId64 " est=%" PRId64,
            estimator->name, estimator->accumulator, estimator->estimate);
  }
  GPR_ASSERT(estimator->ping_state == GRPC_BDP_PING_SCHEDULED);
  estimator->ping_state = GRPC_BDP_PING_STARTED;
  estimator->accumulator = 0;
}

void grpc_bdp_estimator_complete_ping(grpc_bdp_estimator *estimator) {
  if (grpc_bdp_estimator_trace) {
    gpr_log(GPR_DEBUG, "bdp[%s]:complete acc=%" PRId64 " est=%" PRId64,
            estimator->name, estimator->accumulator, estimator->estimate);
  }
  GPR_ASSERT(estimator->ping_state == GRPC_BDP_PING_STARTED);
  if (estimator->accumulator > 2 * estimator->estimate / 3) {
    estimator->estimate *= 2;
    if (grpc_bdp_estimator_trace) {
      gpr_log(GPR_DEBUG, "bdp[%s]: estimate increased to %" PRId64,
              estimator->name, estimator->estimate);
    }
  }
  estimator->ping_state = GRPC_BDP_PING_UNSCHEDULED;
  estimator->accumulator = 0;
}
