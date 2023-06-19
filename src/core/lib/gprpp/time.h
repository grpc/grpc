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

#ifndef GRPC_SRC_CORE_LIB_GPRPP_TIME_H
#define GRPC_SRC_CORE_LIB_GPRPP_TIME_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <limits>
#include <ostream>
#include <string>

#include "absl/time/time.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/time_precise.h"
#include "src/core/lib/gpr/useful.h"

#define GRPC_LOG_EVERY_N_SEC(n, severity, format, ...)                   \
  do {                                                                   \
    static std::atomic<uint64_t> prev{0};                                \
    uint64_t now =                                                       \
        grpc_core::Timestamp::FromTimespec(gpr_now(GPR_CLOCK_MONOTONIC)) \
            .nanoseconds_after_process_epoch();                          \
    uint64_t prev_tsamp = prev.exchange(now);                            \
    if (prev_tsamp == 0 || now - prev_tsamp > (n)*1000000000) {          \
      gpr_log(severity, format, __VA_ARGS__);                            \
    }                                                                    \
  } while (0)

#define GRPC_LOG_EVERY_N_SEC_DELAYED(n, severity, format, ...)           \
  do {                                                                   \
    static std::atomic<uint64_t> prev{0};                                \
    uint64_t now =                                                       \
        grpc_core::Timestamp::FromTimespec(gpr_now(GPR_CLOCK_MONOTONIC)) \
            .nanoseconds_after_process_epoch();                          \
    uint64_t prev_tsamp = prev.exchange(now);                            \
    if (now - prev_tsamp > (n)*1000000000) {                             \
      gpr_log(severity, format, __VA_ARGS__);                            \
    }                                                                    \
  } while (0)

namespace grpc_core {

namespace time_detail {

inline int64_t NanosAdd(int64_t a, int64_t b) {
  if (a == std::numeric_limits<int64_t>::max() ||
      b == std::numeric_limits<int64_t>::max()) {
    return std::numeric_limits<int64_t>::max();
  }
  if (a == std::numeric_limits<int64_t>::min() ||
      b == std::numeric_limits<int64_t>::min()) {
    return std::numeric_limits<int64_t>::min();
  }
  return SaturatingAdd(a, b);
}

constexpr inline int64_t NanosMul(int64_t millis, int64_t mul) {
  return millis >= std::numeric_limits<int64_t>::max() / mul
             ? std::numeric_limits<int64_t>::max()
         : millis <= std::numeric_limits<int64_t>::min() / mul
             ? std::numeric_limits<int64_t>::min()
             : millis * mul;
}

}  // namespace time_detail

class Duration;

// Timestamp represents a discrete point in time.
class Timestamp {
 public:
  // Base interface for time providers.
  class Source {
   public:
    // Return the current time.
    virtual Timestamp Now() = 0;
    virtual void InvalidateCache() {}

   protected:
    // We don't delete through this interface, so non-virtual dtor is fine.
    ~Source() = default;
  };

  class ScopedSource : public Source {
   public:
    ScopedSource() : previous_(thread_local_time_source_) {
      thread_local_time_source_ = this;
    }
    ScopedSource(const ScopedSource&) = delete;
    ScopedSource& operator=(const ScopedSource&) = delete;
    void InvalidateCache() override { previous_->InvalidateCache(); }

   protected:
    ~ScopedSource() { thread_local_time_source_ = previous_; }
    Source* previous() const { return previous_; }

   private:
    Source* const previous_;
  };

  constexpr Timestamp() = default;
  // Constructs a Timestamp from a gpr_timespec.
  static Timestamp FromTimespec(gpr_timespec t);

  // Construct a Timestamp from a gpr_cycle_counter.
  static Timestamp FromCycleCounter(gpr_cycle_counter c);

  static Timestamp Now() { return thread_local_time_source_->Now(); }

  static constexpr Timestamp FromNanosecondsAfterProcessEpoch(int64_t nanos) {
    return Timestamp(nanos);
  }

  static constexpr Timestamp ProcessEpoch() { return Timestamp(0); }

  static constexpr Timestamp InfFuture() {
    return Timestamp(std::numeric_limits<int64_t>::max());
  }

  static constexpr Timestamp InfPast() {
    return Timestamp(std::numeric_limits<int64_t>::min());
  }

  constexpr bool operator==(Timestamp other) const {
    return nanos_ == other.nanos_;
  }
  constexpr bool operator!=(Timestamp other) const {
    return nanos_ != other.nanos_;
  }
  constexpr bool operator<(Timestamp other) const {
    return nanos_ < other.nanos_;
  }
  constexpr bool operator<=(Timestamp other) const {
    return nanos_ <= other.nanos_;
  }
  constexpr bool operator>(Timestamp other) const {
    return nanos_ > other.nanos_;
  }
  constexpr bool operator>=(Timestamp other) const {
    return nanos_ >= other.nanos_;
  }
  Timestamp& operator+=(Duration duration);

  bool is_process_epoch() const { return nanos_ == 0; }

  uint64_t nanoseconds_after_process_epoch() const { return nanos_; }

  gpr_timespec as_timespec(gpr_clock_type type) const;

  std::string ToString() const;

 private:
  explicit constexpr Timestamp(int64_t nanos) : nanos_(nanos) {}

  int64_t nanos_ = 0;
  static thread_local Timestamp::Source* thread_local_time_source_;
};

class ScopedTimeCache final : public Timestamp::ScopedSource {
 public:
  Timestamp Now() override;

  void InvalidateCache() override {
    cached_time_ = absl::nullopt;
    Timestamp::ScopedSource::InvalidateCache();
  }
  void TestOnlySetNow(Timestamp now) { cached_time_ = now; }

 private:
  absl::optional<Timestamp> cached_time_;
};

// Duration represents a span of time.
class Duration {
 public:
  constexpr Duration() noexcept : nanos_(0) {}

  static Duration FromTimespec(gpr_timespec t);
  static Duration FromSecondsAndNanoseconds(int64_t seconds, int32_t nanos);
  static Duration FromSecondsAsDouble(double seconds);
  static Duration FromNanosecondsAsDouble(double nanos);

  static constexpr Duration Zero() { return Duration(0); }

  // Smallest representatable positive duration.
  static constexpr Duration Epsilon() { return Duration(1); }

  static constexpr Duration NegativeInfinity() {
    return Duration(std::numeric_limits<int64_t>::min());
  }

  static constexpr Duration Infinity() {
    return Duration(std::numeric_limits<int64_t>::max());
  }

  static constexpr Duration Hours(int64_t hours) {
    return Minutes(time_detail::NanosMul(hours, 60));
  }

  static constexpr Duration Minutes(int64_t minutes) {
    return Seconds(time_detail::NanosMul(minutes, 60));
  }

  static constexpr Duration Seconds(int64_t seconds) {
    return Milliseconds(time_detail::NanosMul(seconds, GPR_MS_PER_SEC));
  }

  static constexpr Duration Milliseconds(int64_t millis) {
    return Duration(time_detail::NanosMul(millis, GPR_NS_PER_MS));
  }

  static constexpr Duration Microseconds(int64_t micros) {
    return Duration(time_detail::NanosMul(micros, GPR_NS_PER_US));
  }

  static constexpr Duration Nanoseconds(int64_t nanos) {
    return Duration(nanos);
  }

  constexpr bool operator==(Duration other) const {
    return nanos_ == other.nanos_;
  }
  constexpr bool operator!=(Duration other) const {
    return nanos_ != other.nanos_;
  }
  constexpr bool operator<(Duration other) const {
    return nanos_ < other.nanos_;
  }
  constexpr bool operator<=(Duration other) const {
    return nanos_ <= other.nanos_;
  }
  constexpr bool operator>(Duration other) const {
    return nanos_ > other.nanos_;
  }
  constexpr bool operator>=(Duration other) const {
    return nanos_ >= other.nanos_;
  }
  Duration& operator/=(int64_t divisor) {
    if (nanos_ == std::numeric_limits<int64_t>::max()) {
      *this = divisor < 0 ? NegativeInfinity() : Infinity();
    } else if (nanos_ == std::numeric_limits<int64_t>::min()) {
      *this = divisor < 0 ? Infinity() : NegativeInfinity();
    } else {
      nanos_ /= divisor;
    }
    return *this;
  }
  Duration& operator*=(double multiplier);
  Duration& operator+=(Duration other) {
    nanos_ += other.nanos_;
    return *this;
  }

  constexpr int64_t MillisRoundDown() const { return nanos_ / GPR_NS_PER_MS; }
  constexpr int64_t MillisRoundUp() const {
    return nanos_ / GPR_NS_PER_MS + (nanos_ % GPR_NS_PER_US != 0);
  }
  constexpr int64_t nanos() const { return nanos_; }
  double seconds() const { return static_cast<double>(nanos_) / 1.0e9; }

  // NOLINTNEXTLINE: google-explicit-constructor
  operator grpc_event_engine::experimental::EventEngine::Duration() const;

  gpr_timespec as_timespec() const;
  absl::Duration as_absl_duration() const { return absl::Nanoseconds(nanos_); }

  std::string ToString() const;

  // Returns the duration in the JSON form corresponding to a
  // google.protobuf.Duration proto, as defined here:
  // https://developers.google.com/protocol-buffers/docs/proto3#json
  std::string ToJsonString() const;

 private:
  explicit constexpr Duration(int64_t nanos) : nanos_(nanos) {}

  int64_t nanos_;
};

inline Duration operator+(Duration lhs, Duration rhs) {
  return Duration::Nanoseconds(time_detail::NanosAdd(lhs.nanos(), rhs.nanos()));
}

inline Duration operator-(Duration lhs, Duration rhs) {
  return Duration::Nanoseconds(
      time_detail::NanosAdd(lhs.nanos(), -rhs.nanos()));
}

inline Timestamp operator+(Timestamp lhs, Duration rhs) {
  return Timestamp::FromNanosecondsAfterProcessEpoch(time_detail::NanosAdd(
      lhs.nanoseconds_after_process_epoch(), rhs.nanos()));
}

inline Timestamp operator-(Timestamp lhs, Duration rhs) {
  return Timestamp::FromNanosecondsAfterProcessEpoch(time_detail::NanosAdd(
      lhs.nanoseconds_after_process_epoch(), -rhs.nanos()));
}

inline Timestamp operator+(Duration lhs, Timestamp rhs) { return rhs + lhs; }

inline Duration operator-(Timestamp lhs, Timestamp rhs) {
  return Duration::Nanoseconds(
      time_detail::NanosAdd(lhs.nanoseconds_after_process_epoch(),
                            -rhs.nanoseconds_after_process_epoch()));
}

inline Duration operator*(Duration lhs, double rhs) {
  if (lhs == Duration::Infinity()) {
    return rhs < 0 ? Duration::NegativeInfinity() : Duration::Infinity();
  }
  if (lhs == Duration::NegativeInfinity()) {
    return rhs < 0 ? Duration::Infinity() : Duration::NegativeInfinity();
  }
  return Duration::FromNanosecondsAsDouble(lhs.nanos() * rhs);
}

inline Duration operator*(double lhs, Duration rhs) { return rhs * lhs; }

inline Duration operator/(Duration lhs, int64_t rhs) {
  lhs /= rhs;
  return lhs;
}

inline Duration Duration::FromSecondsAndNanoseconds(int64_t seconds,
                                                    int32_t nanos) {
  return Seconds(seconds) + Nanoseconds(nanos);
}

inline Duration Duration::FromSecondsAsDouble(double seconds) {
  return FromNanosecondsAsDouble(seconds * 1.0e9);
}

inline Duration Duration::FromNanosecondsAsDouble(double nanos) {
  if (nanos >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
    return Infinity();
  }
  if (nanos <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
    return NegativeInfinity();
  }
  return Nanoseconds(static_cast<int64_t>(nanos));
}

inline Duration& Duration::operator*=(double multiplier) {
  *this = *this * multiplier;
  return *this;
}

inline Timestamp& Timestamp::operator+=(Duration duration) {
  return *this = (*this + duration);
}

void TestOnlySetProcessEpoch(gpr_timespec epoch);

std::ostream& operator<<(std::ostream& out, Timestamp timestamp);
std::ostream& operator<<(std::ostream& out, Duration duration);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_TIME_H
