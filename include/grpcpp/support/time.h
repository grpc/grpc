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

#ifndef GRPCPP_SUPPORT_TIME_H
#define GRPCPP_SUPPORT_TIME_H

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/grpc_types.h>
#include <grpcpp/support/config.h>

#include <chrono>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc {

/// If you are trying to use CompletionQueue::AsyncNext with a time class that
/// isn't either gpr_timespec or std::chrono::system_clock::time_point, you
/// will most likely be looking at this comment as your compiler will have
/// fired an error below. In order to fix this issue, you have two potential
/// solutions:

///   1. Use gpr_timespec or std::chrono::system_clock::time_point instead
///   2. Specialize the TimePoint class with whichever time class that you
///      want to use here. See below for two examples of how to do this.
///
template <typename T>
class TimePoint {
 public:
  // If you see the error with methods below, you may need either
  // i) using the existing types having a conversion class such as
  // gpr_timespec and std::chrono::system_clock::time_point or
  // ii) writing a new TimePoint<YourType> to address your case.
  TimePoint(const T& /*time*/) = delete;
  gpr_timespec raw_time() = delete;
};

template <>
class TimePoint<gpr_timespec> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  TimePoint(const gpr_timespec& time) : time_(time) {}
  gpr_timespec raw_time() { return time_; }

 private:
  gpr_timespec time_;
};

template <>
class TimePoint<absl::Time> {
 public:
  explicit TimePoint(absl::Time time) : time_(TimeToGprTimespec(time)) {}

  gpr_timespec raw_time() const { return time_; }

 private:
  static gpr_timespec TimeToGprTimespec(absl::Time time) {
    if (time == absl::InfiniteFuture()) {
      return gpr_inf_future(GPR_CLOCK_REALTIME);
    }
    if (time == absl::InfinitePast()) {
      return gpr_inf_past(GPR_CLOCK_REALTIME);
    }

    gpr_timespec spec;
    timespec t = absl::ToTimespec(time);
    spec.tv_sec = t.tv_sec;
    spec.tv_nsec = static_cast<int32_t>(t.tv_nsec);
    spec.clock_type = GPR_CLOCK_REALTIME;
    return spec;
  }

  const gpr_timespec time_;
};

}  // namespace grpc

namespace grpc {

// from and to should be absolute time.
void Timepoint2Timespec(const std::chrono::system_clock::time_point& from,
                        gpr_timespec* to);
void TimepointHR2Timespec(
    const std::chrono::high_resolution_clock::time_point& from,
    gpr_timespec* to);

std::chrono::system_clock::time_point Timespec2Timepoint(gpr_timespec t);

template <>
class TimePoint<std::chrono::system_clock::time_point> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  TimePoint(const std::chrono::system_clock::time_point& time) {
    Timepoint2Timespec(time, &time_);
  }
  gpr_timespec raw_time() const { return time_; }

 private:
  gpr_timespec time_;
};

// Converts gpr timespec to absl::Time
absl::Time TimeFromGprTimespec(gpr_timespec time);

// Converts absl::Time to gpr timespec with clock type set to REALTIME
gpr_timespec GprTimeSpecFromTime(absl::Time);

// Converts gpr timespec to absl::Duration. The gpr timespec clock type
// should be TIMESPAN.
absl::Duration DurationFromGprTimespec(gpr_timespec time);

// Converts absl::Duration to gpr timespec with clock type set to TIMESPAM
gpr_timespec GprTimeSpecFromDuration(absl::Duration);

// Creates an absolute-time deadline from now + dur.
inline gpr_timespec DeadlineFromDuration(absl::Duration dur) {
  if (absl::time_internal::IsInfiniteDuration(dur)) {
    if (dur > absl::ZeroDuration()) {
      return gpr_inf_future(GPR_CLOCK_MONOTONIC);
    } else {
      return gpr_inf_past(GPR_CLOCK_MONOTONIC);
    }
  }

  timespec t = absl::ToTimespec(dur);
  gpr_timespec span;
  span.tv_sec = t.tv_sec;
  span.tv_nsec = static_cast<int32_t>(t.tv_nsec);
  span.clock_type = GPR_TIMESPAN;
  return gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), span);
}

}  // namespace grpc

#endif  // GRPCPP_SUPPORT_TIME_H
