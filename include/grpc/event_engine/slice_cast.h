// Copyright 2022 gRPC authors.
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

#ifndef GRPC_EVENT_ENGINE_SLICE_CAST_H
#define GRPC_EVENT_ENGINE_SLICE_CAST_H

namespace grpc_event_engine {
namespace experimental {

// Opt-in trait class for slice conversions.
template <typename Result, typename T>
struct ConstRefSliceCastable;

template <typename A>
struct ConstRefSliceCastable<A, A> {};

template <typename Result, typename T>
const Result& SliceCast(const T& value, ConstRefSliceCastable<Result, T> = {}) {
  static_assert(sizeof(Result) == sizeof(T), "size mismatch");
  return reinterpret_cast<const Result&>(value);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif
