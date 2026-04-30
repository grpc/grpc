//
//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/time.h>
#include <grpcpp/support/time.h>

#include <chrono>
#include <cstdint>
#include <ctime>

#include "absl/log/check.h"
#include "absl/time/time.h"

// IWYU pragma: no_include <ratio>

using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::nanoseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

namespace grpc {

void Timepoint2Timespec(const system_clock::time_point& from,
                        gpr_timespec* to) {
  system_clock::duration deadline = from.time_since_epoch();
  seconds secs = duration_cast<seconds>(deadline);
  if (from == system_clock::time_point::max() ||
      secs.count() >= gpr_inf_future(GPR_CLOCK_REALTIME).tv_sec ||
      secs.count() < 0) {
    *to = gpr_inf_future(GPR_CLOCK_REALTIME);
    return;
  }
  nanoseconds nsecs = duration_cast<nanoseconds>(deadline - secs);
  to->tv_sec = static_cast<int64_t>(secs.count());
  to->tv_nsec = static_cast<int32_t>(nsecs.count());
  to->clock_type = GPR_CLOCK_REALTIME;
}

void TimepointHR2Timespec(const high_resolution_clock::time_point& from,
                          gpr_timespec* to) {
  high_resolution_clock::duration deadline = from.time_since_epoch();
  seconds secs = duration_cast<seconds>(deadline);
  if (from == high_resolution_clock::time_point::max() ||
      secs.count() >= gpr_inf_future(GPR_CLOCK_REALTIME).tv_sec ||
      secs.count() < 0) {
    *to = gpr_inf_future(GPR_CLOCK_REALTIME);
    return;
  }
  nanoseconds nsecs = duration_cast<nanoseconds>(deadline - secs);
  to->tv_sec = static_cast<int64_t>(secs.count());
  to->tv_nsec = static_cast<int32_t>(nsecs.count());
  to->clock_type = GPR_CLOCK_REALTIME;
}

system_clock::time_point Timespec2Timepoint(gpr_timespec t) {
  if (gpr_time_cmp(t, gpr_inf_future(t.clock_type)) == 0) {
    return system_clock::time_point::max();
  }
  t = gpr_convert_clock_type(t, GPR_CLOCK_REALTIME);
  system_clock::time_point tp;
  tp += duration_cast<system_clock::time_point::duration>(seconds(t.tv_sec));
  tp +=
      duration_cast<system_clock::time_point::duration>(nanoseconds(t.tv_nsec));
  return tp;
}

absl::Time TimeFromGprTimespec(gpr_timespec time) {
  if (!gpr_time_cmp(time, gpr_inf_future(time.clock_type))) {
    return absl::InfiniteFuture();
  }
  if (!gpr_time_cmp(time, gpr_inf_past(time.clock_type))) {
    return absl::InfinitePast();
  }
  time = gpr_convert_clock_type(time, GPR_CLOCK_REALTIME);
  timespec ts;
  ts.tv_sec = static_cast<decltype(ts.tv_sec)>(time.tv_sec);
  ts.tv_nsec = static_cast<decltype(ts.tv_nsec)>(time.tv_nsec);
  return absl::TimeFromTimespec(ts);
}

gpr_timespec GprTimeSpecFromTime(absl::Time time) {
  TimePoint<absl::Time> at(time);
  return at.raw_time();
}

absl::Duration DurationFromGprTimespec(gpr_timespec time) {
  CHECK_EQ(time.clock_type, GPR_TIMESPAN);
  timespec ts;
  ts.tv_sec = static_cast<decltype(ts.tv_sec)>(time.tv_sec);
  ts.tv_nsec = static_cast<decltype(ts.tv_nsec)>(time.tv_nsec);
  return absl::DurationFromTimespec(ts);
}

gpr_timespec GprTimeSpecFromDuration(absl::Duration duration) {
  if (absl::time_internal::IsInfiniteDuration(duration)) {
    if (duration > absl::ZeroDuration()) {
      return gpr_inf_future(GPR_TIMESPAN);
    } else {
      return gpr_inf_past(GPR_TIMESPAN);
    }
  }
  gpr_timespec gpr_ts;
  timespec ts = absl::ToTimespec(duration);
  gpr_ts.tv_sec = static_cast<decltype(gpr_ts.tv_sec)>(ts.tv_sec);
  gpr_ts.tv_nsec = static_cast<decltype(gpr_ts.tv_nsec)>(ts.tv_nsec);
  gpr_ts.clock_type = GPR_TIMESPAN;
  return gpr_ts;
}

}  // namespace grpc
