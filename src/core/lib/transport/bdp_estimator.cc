//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/lib/transport/bdp_estimator.h"

#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <stdlib.h>

#include <algorithm>

#include "absl/log/check.h"
#include "absl/log/log.h"

namespace grpc_core {

BdpEstimator::BdpEstimator(absl::string_view name)
    : accumulator_(0),
      estimate_(65536),
      ping_start_time_(gpr_time_0(GPR_CLOCK_MONOTONIC)),
      inter_ping_delay_(Duration::Milliseconds(100)),  // start at 100ms
      stable_estimate_count_(0),
      ping_state_(PingState::UNSCHEDULED),
      bw_est_(0),
      name_(name) {}

Timestamp BdpEstimator::CompletePing() {
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  gpr_timespec dt_ts = gpr_time_sub(now, ping_start_time_);
  double dt = static_cast<double>(dt_ts.tv_sec) +
              (1e-9 * static_cast<double>(dt_ts.tv_nsec));
  double bw = dt > 0 ? (static_cast<double>(accumulator_) / dt) : 0;
  Duration start_inter_ping_delay = inter_ping_delay_;
  GRPC_TRACE_LOG(bdp_estimator, INFO)
      << "bdp[" << name_ << "]:complete acc=" << accumulator_
      << " est=" << estimate_ << " dt=" << dt << " bw=" << bw / 125000.0
      << "Mbs bw_est=" << bw_est_ / 125000.0 << "Mbs";
  CHECK(ping_state_ == PingState::STARTED);
  if (accumulator_ > 2 * estimate_ / 3 && bw > bw_est_) {
    estimate_ = std::max(accumulator_, estimate_ * 2);
    bw_est_ = bw;
    GRPC_TRACE_LOG(bdp_estimator, INFO)
        << "bdp[" << name_ << "]: estimate increased to " << estimate_;
    inter_ping_delay_ /= 2;  // if the ping estimate changes,
                             // exponentially get faster at probing
  } else if (inter_ping_delay_ < Duration::Seconds(10)) {
    stable_estimate_count_++;
    if (stable_estimate_count_ >= 2) {
      // if the ping estimate is steady, slowly ramp down the probe time
      inter_ping_delay_ += Duration::Milliseconds(
          100 + static_cast<int>(rand() * 100.0 / RAND_MAX));
    }
  }
  if (start_inter_ping_delay != inter_ping_delay_) {
    stable_estimate_count_ = 0;
    GRPC_TRACE_LOG(bdp_estimator, INFO)
        << "bdp[" << name_ << "]:update_inter_time to "
        << inter_ping_delay_.millis() << "ms";
  }
  ping_state_ = PingState::UNSCHEDULED;
  accumulator_ = 0;
  return Timestamp::Now() + inter_ping_delay_;
}

}  // namespace grpc_core
