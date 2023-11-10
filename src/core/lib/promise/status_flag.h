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

#include "src/core/lib/promise/detail/status.h"

namespace grpc_core {

// A boolean representing whether an operation succeeded (true) or failed
// (false).
class StatusFlag {
 public:
  explicit StatusFlag(bool value) : value_(value) {}

  bool ok() const { return value_; }

 private:
  bool value_;
};

inline bool IsStatusOk(const StatusFlag& flag) { return flag.ok(); }

template <>
struct StatusCastImpl<absl::Status, StatusFlag> {
  static absl::Status Cast(StatusFlag flag) {
    return flag.ok() ? absl::OkStatus() : absl::CancelledError();
  }
};

struct Failure {};

// A value if an operation was successful, or a failure flag if not.
template <typename T>
class ValueOrFailure {
 public:
  explicit ValueOrFailure(T value) : value_(std::move(value)) {}
  explicit ValueOrFailure(Failure) {}

  static ValueOrFailure FromOptional(absl::optional<T> value) {
    return ValueOrFailure{std::move(value)};
  }

  bool ok() const { return value_.has_value(); }

  const T& value() const { return value_.value(); }
  T& value() { return value_.value(); }
  const T& operator*() const { return *value_; }
  T& operator*() { return *value_; }

 private:
  absl::optional<T> value_;
};

template <typename T>
inline bool IsStatusOk(const ValueOrFailure<T>& value) {
  return value.ok();
}

template <typename T>
struct StatusCastImpl<absl::Status, ValueOrFailure<T>> {
  static absl::Status Cast(const ValueOrFailure<T> flag) {
    return flag.ok() ? absl::OkStatus() : absl::CancelledError();
  }
};

template <typename T>
struct StatusCastImpl<absl::StatusOr<T>, ValueOrFailure<T>> {
  static absl::StatusOr<T> Cast(ValueOrFailure<T> value) {
    return value.ok() ? absl::StatusOr<T>(std::move(value.value()))
                      : absl::CancelledError();
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_STATUS_FLAG_H