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

#include <grpc/support/time.h>

namespace grpc_core {

class Timestamp {
 public:
  constexpr Timestamp() : millis_(0) {}
  explicit Timestamp(gpr_timespec t);

  static constexpr Timestamp FromMiillisecondsAfterProcessEpoch(
      int64_t millis) {
    return Timestamp(millis);
  }

  static constexpr Timestamp InfFuture() {
    return Timestamp(std::numeric_limits<int64_t>::max());
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

 private:
  explicit constexpr Timestamp(int64_t millis) : millis_(millis) {}

  int64_t millis_;
};

class Duration {
 public:
  constexpr Duration() : millis_(0) {}

  static constexpr Duration Milliseconds(int64_t millis) {
    return Duration(millis);
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

  constexpr int64_t millis() const { return millis_; }

 private:
  explicit constexpr Duration(int64_t millis) : millis_(millis) {}

  int64_t millis_;
};

inline Duration operator+(Duration lhs, Duration rhs) {
  return Duration::Milliseconds(lhs.millis() + rhs.millis());
}

inline Duration operator-(Duration lhs, Duration rhs) {
  return Duration::Milliseconds(lhs.millis() - rhs.millis());
}

inline Timestamp operator+(Timestamp lhs, Duration rhs) {
  return Timestamp::FromMiillisecondsAfterProcessEpoch(
      lhs.milliseconds_after_process_epoch() + rhs.millis());
}

inline Timestamp operator-(Timestamp lhs, Duration rhs) {
  return Timestamp::FromMiillisecondsAfterProcessEpoch(
      lhs.milliseconds_after_process_epoch() - rhs.millis());
}

inline Timestamp operator+(Duration lhs, Timestamp rhs) { return rhs + lhs; }

inline Duration operator-(Timestamp lhs, Timestamp rhs) {
  return Duration::Milliseconds(lhs.milliseconds_after_process_epoch() -
                                rhs.milliseconds_after_process_epoch());
}

inline Duration operator*(Duration lhs, double rhs) {
  return Duration::Milliseconds(static_cast<int64_t>(lhs.millis() * rhs));
}

inline Duration operator*(double lhs, Duration rhs) { return rhs * lhs; }

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_TIME_H
