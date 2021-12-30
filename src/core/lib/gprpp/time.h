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

#ifndef GRPC_CORE_LIB_GPRPP_TIME_H
#define GRPC_CORE_LIB_GPRPP_TIME_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <cstdint>
#include <limits>
#include <string>

#include <grpc/support/time.h>

#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

class Timestamp {
 public:
  constexpr Timestamp() = default;
  explicit Timestamp(gpr_timespec t);

  static Timestamp FromCycleCounterRoundUp(gpr_cycle_counter c);

  static constexpr Timestamp FromMillisecondsAfterProcessEpoch(int64_t millis) {
    return Timestamp(millis);
  }

  static constexpr Timestamp ProcessEpoch() { return Timestamp(0); }

  static constexpr Timestamp InfFuture() {
    return Timestamp(std::numeric_limits<int64_t>::max());
  }

  static constexpr Timestamp InfPast() {
    return Timestamp(std::numeric_limits<int64_t>::min());
  }

  constexpr bool operator==(Timestamp other) const {
    return millis_ == other.millis_;
  }
  constexpr bool operator!=(Timestamp other) const {
    return millis_ != other.millis_;
  }
  constexpr bool operator<(Timestamp other) const {
    return millis_ < other.millis_;
  }
  constexpr bool operator<=(Timestamp other) const {
    return millis_ <= other.millis_;
  }
  constexpr bool operator>(Timestamp other) const {
    return millis_ > other.millis_;
  }
  constexpr bool operator>=(Timestamp other) const {
    return millis_ >= other.millis_;
  }

  bool is_zero() const { return millis_ == 0; }

  uint64_t milliseconds_after_process_epoch() const { return millis_; }

  gpr_timespec as_timespec(gpr_clock_type type) const;

 private:
  explicit constexpr Timestamp(int64_t millis) : millis_(millis) {}

  int64_t millis_ = 0;
};

class Duration {
 public:
  constexpr Duration() : millis_(0) {}

  static Duration FromTimespec(gpr_timespec t);
  static Duration FromSecondsAndNanoseconds(int64_t seconds, int32_t nanos);
  static Duration FromSecondsAsDouble(double seconds);

  static constexpr Duration Zero() { return Duration(0); }

  // Smallest representatable positive duration.
  static constexpr Duration Epsilon() { return Duration(1); }

  static constexpr Duration NegativeInfinity() {
    return Duration(std::numeric_limits<int64_t>::min());
  }

  static constexpr Duration Infinity() {
    return Duration(std::numeric_limits<int64_t>::max());
  }

  static constexpr Duration Hours(int64_t hours) { return Minutes(hours * 60); }

  static constexpr Duration Minutes(int64_t minutes) {
    return Seconds(minutes * 60);
  }

  static constexpr Duration Seconds(int64_t seconds) {
    return Milliseconds(seconds * GPR_MS_PER_SEC);
  }

  static constexpr Duration Milliseconds(int64_t millis) {
    return Duration(millis);
  }

  static constexpr Duration MicrosecondsRoundDown(int64_t micros) {
    return Duration(micros / GPR_US_PER_MS);
  }

  static constexpr Duration NanosecondsRoundDown(int64_t nanos) {
    return Duration(nanos / GPR_NS_PER_MS);
  }

  static constexpr Duration MicrosecondsRoundUp(int64_t micros) {
    return Duration(micros / GPR_US_PER_MS + (micros % GPR_US_PER_MS != 0));
  }

  static constexpr Duration NanosecondsRoundUp(int64_t nanos) {
    return Duration(nanos / GPR_NS_PER_MS + (nanos % GPR_NS_PER_MS != 0));
  }

  constexpr bool operator==(Duration other) const {
    return millis_ == other.millis_;
  }
  constexpr bool operator!=(Duration other) const {
    return millis_ != other.millis_;
  }
  constexpr bool operator<(Duration other) const {
    return millis_ < other.millis_;
  }
  constexpr bool operator<=(Duration other) const {
    return millis_ <= other.millis_;
  }
  constexpr bool operator>(Duration other) const {
    return millis_ > other.millis_;
  }
  constexpr bool operator>=(Duration other) const {
    return millis_ >= other.millis_;
  }
  Duration& operator/=(int64_t divisor) {
    millis_ /= divisor;
    return *this;
  }
  Duration& operator+=(Duration other) {
    millis_ += other.millis_;
    return *this;
  }

  constexpr int64_t millis() const { return millis_; }
  double seconds() const { return static_cast<double>(millis_) / 1000.0; }

  gpr_timespec as_timespec() const;

  std::string ToString() const;

 private:
  explicit constexpr Duration(int64_t millis) : millis_(millis) {}

  int64_t millis_;
};

inline Duration operator+(Duration lhs, Duration rhs) {
  return Duration::Milliseconds(SaturatingAdd(lhs.millis(), rhs.millis()));
}

inline Duration operator-(Duration lhs, Duration rhs) {
  return Duration::Milliseconds(SaturatingAdd(lhs.millis(), -rhs.millis()));
}

inline Timestamp operator+(Timestamp lhs, Duration rhs) {
  return Timestamp::FromMillisecondsAfterProcessEpoch(
      SaturatingAdd(lhs.milliseconds_after_process_epoch(), rhs.millis()));
}

inline Timestamp operator-(Timestamp lhs, Duration rhs) {
  return Timestamp::FromMillisecondsAfterProcessEpoch(
      SaturatingAdd(lhs.milliseconds_after_process_epoch(), -rhs.millis()));
}

inline Timestamp operator+(Duration lhs, Timestamp rhs) { return rhs + lhs; }

inline Duration operator-(Timestamp lhs, Timestamp rhs) {
  return Duration::Milliseconds(
      SaturatingAdd(lhs.milliseconds_after_process_epoch(),
                    -rhs.milliseconds_after_process_epoch()));
}

inline Duration operator*(Duration lhs, double rhs) {
  return Duration::Milliseconds(static_cast<int64_t>(lhs.millis() * rhs));
}

inline Duration operator*(double lhs, Duration rhs) { return rhs * lhs; }

inline Duration Duration::FromSecondsAndNanoseconds(int64_t seconds,
                                                    int32_t nanos) {
  return Seconds(seconds) + NanosecondsRoundDown(nanos);
}

inline Duration Duration::FromSecondsAsDouble(double seconds) {
  return Milliseconds(static_cast<int64_t>(seconds * 1000.0));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_TIME_H
