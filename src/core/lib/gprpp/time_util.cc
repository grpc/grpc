//
// Copyright 2021 the gRPC authors.
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

#include "src/core/lib/gprpp/time_util.h"

#include <stdint.h>
#include <time.h>

#include "absl/log/check.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

namespace grpc_core {

gpr_timespec ToGprTimeSpec(absl::Duration duration) {
  if (duration == absl::InfiniteDuration()) {
    return gpr_inf_future(GPR_TIMESPAN);
  } else if (duration == -absl::InfiniteDuration()) {
    return gpr_inf_past(GPR_TIMESPAN);
  } else {
    int64_t s = absl::IDivDuration(duration, absl::Seconds(1), &duration);
    int64_t n = absl::IDivDuration(duration, absl::Nanoseconds(1), &duration);
    return gpr_time_add(gpr_time_from_seconds(s, GPR_TIMESPAN),
                        gpr_time_from_nanos(n, GPR_TIMESPAN));
  }
}

gpr_timespec ToGprTimeSpec(absl::Time time) {
  if (time == absl::InfiniteFuture()) {
    return gpr_inf_future(GPR_CLOCK_REALTIME);
  } else if (time == absl::InfinitePast()) {
    return gpr_inf_past(GPR_CLOCK_REALTIME);
  } else {
    timespec ts = absl::ToTimespec(time);
    gpr_timespec out;
    out.tv_sec = static_cast<decltype(out.tv_sec)>(ts.tv_sec);
    out.tv_nsec = static_cast<decltype(out.tv_nsec)>(ts.tv_nsec);
    out.clock_type = GPR_CLOCK_REALTIME;
    return out;
  }
}

absl::Duration ToAbslDuration(gpr_timespec ts) {
  CHECK(ts.clock_type == GPR_TIMESPAN);
  if (gpr_time_cmp(ts, gpr_inf_future(GPR_TIMESPAN)) == 0) {
    return absl::InfiniteDuration();
  } else if (gpr_time_cmp(ts, gpr_inf_past(GPR_TIMESPAN)) == 0) {
    return -absl::InfiniteDuration();
  } else {
    return absl::Seconds(ts.tv_sec) + absl::Nanoseconds(ts.tv_nsec);
  }
}

absl::Time ToAbslTime(gpr_timespec ts) {
  CHECK(ts.clock_type != GPR_TIMESPAN);
  gpr_timespec rts = gpr_convert_clock_type(ts, GPR_CLOCK_REALTIME);
  if (gpr_time_cmp(rts, gpr_inf_future(GPR_CLOCK_REALTIME)) == 0) {
    return absl::InfiniteFuture();
  } else if (gpr_time_cmp(rts, gpr_inf_past(GPR_CLOCK_REALTIME)) == 0) {
    return absl::InfinitePast();
  } else {
    return absl::UnixEpoch() + absl::Seconds(rts.tv_sec) +
           absl::Nanoseconds(rts.tv_nsec);
  }
}

}  // namespace grpc_core
