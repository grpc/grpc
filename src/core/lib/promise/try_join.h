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

#ifndef GRPC_CORE_LIB_PROMISE_TRY_JOIN_H
#define GRPC_CORE_LIB_PROMISE_TRY_JOIN_H

#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace try_join_detail {

struct Empty {};

template <typename T>
T IntoResult(absl::StatusOr<T>* status) {
  return std::move(**status);
}

inline Empty IntoResult(absl::Status* status) { return Empty{}; }

// Implementation of TryJoin combinator.
template <typename... Promises>
class TryJoin;

#include "try_join_switch.h"

}  // namespace try_join_detail

// Run all promises.
// If any fail, cancel the rest and return the failure.
// If all succeed, return Ok(tuple-of-results).
template <typename... Promises>
try_join_detail::TryJoin<Promises...> TryJoin(Promises... promises) {
  return try_join_detail::TryJoin<Promises...>(std::move(promises)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_TRY_JOIN_H
