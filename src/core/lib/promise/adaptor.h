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

#ifndef GRPC_CORE_LIB_PROMISE_FACTORY_H
#define GRPC_CORE_LIB_PROMISE_FACTORY_H

#include "absl/status/statusor.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace adaptor_detail {

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
  [[no_unique_address]] F f_;
  [[no_unique_address]] std::tuple<Captures...> captures_;
};

}  // namespace adaptor_detail

template <typename F, typename... Captures>
adaptor_detail::Capture<F, Captures...> Capture(F f, Captures... captures) {
  return adaptor_detail::Capture<F, Captures...>(std::move(f),
                                                 std::move(captures)...);
}

template <typename T>
void Destruct(T* p) {
  p->~T();
}

template <typename T>
void Construct(T* p, T&& move_from) {
  new (p) T(std::forward<T>(move_from));
}

template <typename T>
absl::Status IntoStatus(absl::StatusOr<T>* status) {
  return std::move(status->status());
}

inline absl::Status IntoStatus(absl::Status* status) {
  return std::move(*status);
}

}  // namespace grpc_core

#endif
