// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_STATUS_FLAG_H
#define GRPC_SRC_CORE_LIB_PROMISE_STATUS_FLAG_H

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/promise/detail/status.h"

namespace grpc_core {

struct Failure {
  template <typename Sink>
  friend void AbslStringify(Sink& sink, Failure) {
    sink.Append("failed");
  }
};
struct Success {
  template <typename Sink>
  friend void AbslStringify(Sink& sink, Success) {
    sink.Append("ok");
  }
};

inline bool IsStatusOk(Failure) { return false; }
inline bool IsStatusOk(Success) { return true; }

template <>
struct StatusCastImpl<absl::Status, Success> {
  static absl::Status Cast(Success) { return absl::OkStatus(); }
};

template <>
struct StatusCastImpl<absl::Status, const Success&> {
  static absl::Status Cast(Success) { return absl::OkStatus(); }
};

template <>
struct StatusCastImpl<absl::Status, Failure> {
  static absl::Status Cast(Failure) { return absl::CancelledError(); }
};

template <typename T>
struct StatusCastImpl<absl::StatusOr<T>, Failure> {
  static absl::StatusOr<T> Cast(Failure) { return absl::CancelledError(); }
};

// A boolean representing whether an operation succeeded (true) or failed
// (false).
class StatusFlag {
 public:
  StatusFlag() : value_(true) {}
  explicit StatusFlag(bool value) : value_(value) {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  StatusFlag(Failure) : value_(false) {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  StatusFlag(Success) : value_(true) {}

  bool ok() const { return value_; }

  bool operator==(StatusFlag other) const { return value_ == other.value_; }
  std::string ToString() const { return value_ ? "ok" : "failed"; }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, StatusFlag flag) {
    if (flag.ok()) {
      sink.Append("ok");
    } else {
      sink.Append("failed");
    }
  }

 private:
  bool value_;
};

inline bool operator==(StatusFlag flag, Failure) { return !flag.ok(); }
inline bool operator==(Failure, StatusFlag flag) { return !flag.ok(); }
inline bool operator==(StatusFlag flag, Success) { return flag.ok(); }
inline bool operator==(Success, StatusFlag flag) { return flag.ok(); }

inline bool operator!=(StatusFlag flag, Failure) { return flag.ok(); }
inline bool operator!=(Failure, StatusFlag flag) { return flag.ok(); }
inline bool operator!=(StatusFlag flag, Success) { return !flag.ok(); }
inline bool operator!=(Success, StatusFlag flag) { return !flag.ok(); }

inline bool IsStatusOk(const StatusFlag& flag) { return flag.ok(); }

template <>
struct StatusCastImpl<absl::Status, StatusFlag> {
  static absl::Status Cast(StatusFlag flag) {
    return flag.ok() ? absl::OkStatus() : absl::CancelledError();
  }
};

template <>
struct StatusCastImpl<absl::Status, StatusFlag&> {
  static absl::Status Cast(StatusFlag flag) {
    return flag.ok() ? absl::OkStatus() : absl::CancelledError();
  }
};

template <>
struct StatusCastImpl<absl::Status, const StatusFlag&> {
  static absl::Status Cast(StatusFlag flag) {
    return flag.ok() ? absl::OkStatus() : absl::CancelledError();
  }
};

template <typename T>
struct FailureStatusCastImpl<absl::StatusOr<T>, StatusFlag> {
  static absl::StatusOr<T> Cast(StatusFlag flag) {
    GPR_DEBUG_ASSERT(!flag.ok());
    return absl::CancelledError();
  }
};

template <typename T>
struct FailureStatusCastImpl<absl::StatusOr<T>, StatusFlag&> {
  static absl::StatusOr<T> Cast(StatusFlag flag) {
    GPR_DEBUG_ASSERT(!flag.ok());
    return absl::CancelledError();
  }
};

template <typename T>
struct FailureStatusCastImpl<absl::StatusOr<T>, const StatusFlag&> {
  static absl::StatusOr<T> Cast(StatusFlag flag) {
    GPR_DEBUG_ASSERT(!flag.ok());
    return absl::CancelledError();
  }
};

// A value if an operation was successful, or a failure flag if not.
template <typename T>
class ValueOrFailure {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  ValueOrFailure(T value) : value_(std::move(value)) {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  ValueOrFailure(Failure) {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  ValueOrFailure(StatusFlag status) { GPR_ASSERT(!status.ok()); }

  static ValueOrFailure FromOptional(absl::optional<T> value) {
    return ValueOrFailure{std::move(value)};
  }

  bool ok() const { return value_.has_value(); }
  StatusFlag status() const { return StatusFlag(ok()); }

  const T& value() const { return value_.value(); }
  T& value() { return value_.value(); }
  const T& operator*() const { return *value_; }
  T& operator*() { return *value_; }

  bool operator==(const ValueOrFailure& other) const {
    return value_ == other.value_;
  }

 private:
  absl::optional<T> value_;
};

template <typename T>
inline bool IsStatusOk(const ValueOrFailure<T>& value) {
  return value.ok();
}

template <typename T>
inline T TakeValue(ValueOrFailure<T>&& value) {
  return std::move(value.value());
}

template <typename T>
struct StatusCastImpl<absl::StatusOr<T>, ValueOrFailure<T>> {
  static absl::StatusOr<T> Cast(ValueOrFailure<T> value) {
    return value.ok() ? absl::StatusOr<T>(std::move(value.value()))
                      : absl::CancelledError();
  }
};

template <typename T>
struct StatusCastImpl<ValueOrFailure<T>, Failure> {
  static ValueOrFailure<T> Cast(Failure) {
    return ValueOrFailure<T>(Failure{});
  }
};

template <typename T>
struct StatusCastImpl<ValueOrFailure<T>, StatusFlag&> {
  static ValueOrFailure<T> Cast(StatusFlag f) {
    GPR_ASSERT(!f.ok());
    return ValueOrFailure<T>(Failure{});
  }
};

template <typename T>
struct StatusCastImpl<ValueOrFailure<T>, StatusFlag> {
  static ValueOrFailure<T> Cast(StatusFlag f) {
    GPR_ASSERT(!f.ok());
    return ValueOrFailure<T>(Failure{});
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_STATUS_FLAG_H