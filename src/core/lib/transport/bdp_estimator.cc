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

#include "src/core/lib/transport/bdp_estimator.h"

#include <inttypes.h>
#include <stdlib.h>

#include "src/core/lib/gpr/useful.h"

grpc_core::TraceFlag grpc_bdp_estimator_trace(false, "bdp_estimator");

namespace grpc_core {

BdpEstimator::BdpEstimator(const char* name)
    : ping_state_(PingState::UNSCHEDULED),
      accumulator_(0),
      estimate_(65536),
      ping_start_time_(gpr_time_0(GPR_CLOCK_MONOTONIC)),
      inter_ping_delay_(100.0),  // start at 100ms
      stable_estimate_count_(0),
      bw_est_(0),
      name_(name) {}

grpc_millis BdpEstimator::CompletePing() {
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  gpr_timespec dt_ts = gpr_time_sub(now, ping_start_time_);
  double dt = static_cast<double>(dt_ts.tv_sec) +
              1e-9 * static_cast<double>(dt_ts.tv_nsec);
  double bw = dt > 0 ? (static_cast<double>(accumulator_) / dt) : 0;
  int start_inter_ping_delay = inter_ping_delay_;
  if (grpc_bdp_estimator_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "bdp[%s]:complete acc=%" PRId64 " est=%" PRId64
            " dt=%lf bw=%lfMbs bw_est=%lfMbs",
            name_, accumulator_, estimate_, dt, bw / 125000.0,
            bw_est_ / 125000.0);
  }
  GPR_ASSERT(ping_state_ == PingState::STARTED);
  if (accumulator_ > 2 * estimate_ / 3 && bw > bw_est_) {
    estimate_ = GPR_MAX(accumulator_, estimate_ * 2);
    bw_est_ = bw;
    if (grpc_bdp_estimator_trace.enabled()) {
      gpr_log(GPR_DEBUG, "bdp[%s]: estimate increased to %" PRId64, name_,
              estimate_);
    }
    inter_ping_delay_ /= 2;  // if the ping estimate changes,
                             // exponentially get faster at probing
  } else if (inter_ping_delay_ < 10000) {
    stable_estimate_count_++;
    if (stable_estimate_count_ >= 2) {
      inter_ping_delay_ +=
          100 + static_cast<int>(rand() * 100.0 /
                                 RAND_MAX);  // if the ping estimate is steady,
                                             // slowly ramp down the probe time
    }
  }
  if (start_inter_ping_delay != inter_ping_delay_) {
    stable_estimate_count_ = 0;
    if (grpc_bdp_estimator_trace.enabled()) {
      gpr_log(GPR_DEBUG, "bdp[%s]:update_inter_time to %dms", name_,
              inter_ping_delay_);
    }
  }
  ping_state_ = PingState::UNSCHEDULED;
  accumulator_ = 0;
  return grpc_core::ExecCtx::Get()->Now() + inter_ping_delay_;
}

}  // namespace grpc_core
