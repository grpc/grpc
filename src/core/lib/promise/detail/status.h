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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_DETAIL_STATUS_H
#define GRPC_SRC_CORE_LIB_PROMISE_DETAIL_STATUS_H

#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

// Helpers for dealing with absl::Status/StatusOr generically

namespace grpc_core {
namespace promise_detail {

// Convert with a move the input status to an absl::Status.
template <typename T>
absl::Status IntoStatus(absl::StatusOr<T>* status) {
  return std::move(status->status());
}

// Convert with a move the input status to an absl::Status.
inline absl::Status IntoStatus(absl::Status* status) {
  return std::move(*status);
}

}  // namespace promise_detail

// Return true if the status represented by the argument is ok, false if not.
// By implementing this function for other, non-absl::Status types, those types
// can participate in TrySeq as result types that affect control flow.
inline bool IsStatusOk(const absl::Status& status) { return status.ok(); }

template <typename To, typename From>
struct StatusCastImpl;

template <typename To>
struct StatusCastImpl<To, To> {
  static To Cast(To&& t) { return std::move(t); }
};

template <typename To>
struct StatusCastImpl<To, const To&> {
  static To Cast(const To& t) { return t; }
};

template <typename T>
struct StatusCastImpl<absl::StatusOr<T>, absl::Status> {
  static absl::StatusOr<T> Cast(absl::Status&& t) { return std::move(t); }
};

template <typename T>
struct StatusCastImpl<absl::StatusOr<T>, const absl::Status&> {
  static absl::StatusOr<T> Cast(const absl::Status& t) { return t; }
};

template <typename To, typename From>
To StatusCast(From&& from) {
  return StatusCastImpl<To, From>::Cast(std::forward<From>(from));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_DETAIL_STATUS_H
