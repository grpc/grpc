// Copyright 2021 gRPC authors.
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

#include "src/core/lib/gprpp/time.h"

#include <limits>

#include <grpc/impl/codegen/gpr_types.h>

namespace grpc_core {

namespace {

gpr_timespec StartTime() {
  static gpr_timespec start_time = gpr_now(GPR_CLOCK_MONOTONIC);
  return start_time;
}

gpr_timespec MillisecondsAsTimespec(int64_t millis, gpr_clock_type clock_type) {
  // special-case infinities as grpc_core::Timestamp can be 32bit on some
  // platforms while gpr_time_from_millis always takes an int64_t.
  if (millis == std::numeric_limits<int64_t>::max()) {
    return gpr_inf_future(clock_type);
  }
  if (millis == std::numeric_limits<int64_t>::min()) {
    return gpr_inf_past(clock_type);
  }

  if (clock_type == GPR_TIMESPAN) {
    return gpr_time_from_millis(millis, GPR_TIMESPAN);
  }
  return gpr_time_add(gpr_convert_clock_type(StartTime(), clock_type),
                      gpr_time_from_millis(millis, GPR_TIMESPAN));
}

int64_t TimespanToMillisRoundUp(gpr_timespec ts) {
  double x = GPR_MS_PER_SEC * static_cast<double>(ts.tv_sec) +
             static_cast<double>(ts.tv_nsec) / GPR_NS_PER_MS +
             static_cast<double>(GPR_NS_PER_SEC - 1) /
                 static_cast<double>(GPR_NS_PER_SEC);
  if (x < 0) return 0;
  if (x >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
    return std::numeric_limits<int64_t>::max();
  }
  return static_cast<int64_t>(x);
}

}  // namespace

Timestamp::Timestamp(gpr_timespec ts)
    : millis_(TimespanToMillisRoundUp(gpr_time_sub(ts, StartTime()))) {}

gpr_timespec Timestamp::as_timespec(gpr_clock_type clock_type) const {
  return MillisecondsAsTimespec(millis_, clock_type);
}

gpr_timespec Duration::as_timespec() const {
  return MillisecondsAsTimespec(millis_, GPR_TIMESPAN);
}

}  // namespace grpc_core