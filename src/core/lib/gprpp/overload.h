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

#ifndef GRPC_CORE_LIB_GPRPP_OVERLOAD_H
#define GRPC_CORE_LIB_GPRPP_OVERLOAD_H

#include <utility>

namespace grpc_core {

template <typename... Cases>
struct OverloadType;
// Compose one overload with N more -- use inheritance to leverage using and the
// empty base class optimization.
template <typename Case, typename... Cases>
struct OverloadType<Case, Cases...> : public Case,
                                      public OverloadType<Cases...> {
  explicit OverloadType(Case c, Cases... cases)
      : Case(std::move(c)), OverloadType<Cases...>(std::move(cases)...) {}
  using Case::operator();
  using OverloadType<Cases...>::operator();
};
// Overload of a single case is just that case itself
template <typename Case>
struct OverloadType<Case> : public Case {
  explicit OverloadType(Case c) : Case(std::move(c)) {}
  using Case::operator();
};

// For callables A, B, C, ..., with different argument lists, return a callable
// that is callable with all argument lists by building the sum type of A, B,
// C,...
template <typename... Cases>
OverloadType<Cases...> Overload(Cases... cases) {
  return OverloadType<Cases...>(std::move(cases)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_OVERLOAD_H
