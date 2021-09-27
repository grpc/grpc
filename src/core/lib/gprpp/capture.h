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

#ifndef GRPC_CORE_LIB_GPRPP_CAPTURE_H
#define GRPC_CORE_LIB_GPRPP_CAPTURE_H

#include <grpc/impl/codegen/port_platform.h>

#include <tuple>
#include <utility>

#include "absl/utility/utility.h"

namespace grpc_core {

namespace detail {

template <typename F, typename... Captures>
class Capture {
 public:
  explicit Capture(F f, Captures... captures)
      : f_(std::move(f)), captures_(std::move(captures)...) {}

  template <typename... Args>
  decltype(std::declval<F>()(static_cast<Captures*>(nullptr)...,
                             std::declval<Args>()...))
  operator()(Args... args) {
    auto f = &f_;
    return absl::apply(
        [f, &args...](Captures&... captures) {
          return (*f)(&captures..., std::move(args)...);
        },
        captures_);
  }

 private:
  GPR_NO_UNIQUE_ADDRESS F f_;
  GPR_NO_UNIQUE_ADDRESS std::tuple<Captures...> captures_;
};

}  // namespace detail

// C++11 helper - best explained by usage:
//
// BigThing big_thing;
// auto f = Capture(
//             [](BigThing* c, int a, int b) { /*...*/ },
//             std::move(big_thing));
//
// results in: f being a callable that takes arguments (int a, int b), and
// captures the original value of big_thing by move. Each call, a pointer to
// each captured thing is inserted into the argument list at the beginning so it
// can be manipulated.
//
// Captured values are mutable, and it's the users responsibility to ensure,
// should this callable be invoked from different threads, that proper locking
// is implemented.
template <typename F, typename... Captures>
detail::Capture<F, Captures...> Capture(F f, Captures... captures) {
  return detail::Capture<F, Captures...>(std::move(f), std::move(captures)...);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_CAPTURE_H
