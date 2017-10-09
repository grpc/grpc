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

#include <stdbool.h>
#include <stdint.h>

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"

extern grpc_tracer_flag grpc_bdp_estimator_trace;

namespace grpc_core {

class BdpEstimator {
 public:
  explicit BdpEstimator(const char *name);
  ~BdpEstimator();

  // Returns true if a reasonable estimate could be obtained
  bool EstimateBdp(int64_t *estimate_out) const {
    *estimate_out = estimate_;
    return true;
  }
  bool EstimateBandwidth(double *bw_out) const {
    *bw_out = bw_est_;
    return true;
  }

  void AddIncomingBytes(int64_t num_bytes) { accumulator_ += num_bytes; }

  // Returns true if the user should schedule a ping
  bool NeedPing(grpc_exec_ctx *exec_ctx) const {
    switch (ping_state_) {
      case PingState::UNSCHEDULED:
        return grpc_exec_ctx_now(exec_ctx) >= next_ping_scheduled_;
      case PingState::SCHEDULED:
      case PingState::STARTED:
        return false;
    }
    GPR_UNREACHABLE_CODE(return false);
  }

  // Schedule a ping: call in response to receiving a true from
  // grpc_bdp_estimator_add_incoming_bytes once a ping has been scheduled by a
  // transport (but not necessarily started)
  void SchedulePing() {
    if (GRPC_TRACER_ON(grpc_bdp_estimator_trace)) {
      gpr_log(GPR_DEBUG, "bdp[%s]:sched acc=%" PRId64 " est=%" PRId64, name_,
              accumulator_, estimate_);
    }
    GPR_ASSERT(ping_state_ == PingState::UNSCHEDULED);
    ping_state_ = PingState::SCHEDULED;
    accumulator_ = 0;
  }

  // Start a ping: call after calling grpc_bdp_estimator_schedule_ping and
  // once
  // the ping is on the wire
  void StartPing() {
    if (GRPC_TRACER_ON(grpc_bdp_estimator_trace)) {
      gpr_log(GPR_DEBUG, "bdp[%s]:start acc=%" PRId64 " est=%" PRId64, name_,
              accumulator_, estimate_);
    }
    GPR_ASSERT(ping_state_ == PingState::SCHEDULED);
    ping_state_ = PingState::STARTED;
    accumulator_ = 0;
    ping_start_time_ = gpr_now(GPR_CLOCK_MONOTONIC);
  }

  // Completes a previously started ping
  void CompletePing(grpc_exec_ctx *exec_ctx);

 private:
  enum class PingState { UNSCHEDULED, SCHEDULED, STARTED };

  PingState ping_state_;
  int64_t accumulator_;
  int64_t estimate_;
  // when was the current ping started?
  gpr_timespec ping_start_time_;
  // when should the next ping start?
  grpc_millis next_ping_scheduled_;
  int inter_ping_delay_;
  int stable_estimate_count_;
  double bw_est_;
  const char *name_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_TRANSPORT_BDP_ESTIMATOR_H */
