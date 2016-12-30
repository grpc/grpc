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

#include "src/core/lib/transport/bdp_estimator.h"

#include <stdlib.h>

#include <grpc/support/log.h>
#include <grpc/support/useful.h>

void grpc_bdp_estimator_init(grpc_bdp_estimator *estimator) {
  estimator->estimate = 65536;
  estimator->ping_state = GRPC_BDP_PING_UNSCHEDULED;
}

bool grpc_bdp_estimator_get_estimate(grpc_bdp_estimator *estimator,
                                     int64_t *estimate) {
  *estimate = estimator->estimate;
  return true;
}

bool grpc_bdp_estimator_add_incoming_bytes(grpc_bdp_estimator *estimator,
                                           int64_t num_bytes) {
  switch (estimator->ping_state) {
    case GRPC_BDP_PING_UNSCHEDULED:
      return true;
    case GRPC_BDP_PING_SCHEDULED:
      return false;
    case GRPC_BDP_PING_STARTED:
      estimator->accumulator += num_bytes;
      return false;
  }
  GPR_UNREACHABLE_CODE(return false);
}

void grpc_bdp_estimator_schedule_ping(grpc_bdp_estimator *estimator) {
  GPR_ASSERT(estimator->ping_state == GRPC_BDP_PING_UNSCHEDULED);
  estimator->ping_state = GRPC_BDP_PING_SCHEDULED;
}

void grpc_bdp_estimator_start_ping(grpc_bdp_estimator *estimator) {
  GPR_ASSERT(estimator->ping_state == GRPC_BDP_PING_SCHEDULED);
  estimator->ping_state = GRPC_BDP_PING_STARTED;
  estimator->accumulator = 0;
}

void grpc_bdp_estimator_complete_ping(grpc_bdp_estimator *estimator) {
  GPR_ASSERT(estimator->ping_state == GRPC_BDP_PING_STARTED);
  if (estimator->accumulator > 2 * estimator->estimate / 3) {
    estimator->estimate *= 2;
    gpr_log(GPR_DEBUG, "est --> %" PRId64, estimator->estimate);
  }
  estimator->ping_state = GRPC_BDP_PING_UNSCHEDULED;
}
