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

template <typename T>
struct IsPoll {
  static constexpr bool value() { return false; }
};
template <typename T>
struct IsPoll<Poll<T>> {
  static constexpr bool value() { return true; }
};

template <typename A, typename F>
struct ArgIsCompatibleWithFunctionError {
  static constexpr bool value() { return false; }
};

template <typename A, typename F, typename Ignored = void>
class Factory {
 public:
  using Arg = A;
  class Promise {
   public:
    struct X {};
    Poll<X> operator()();
  };
  Factory(F f) {
    static_assert(ArgIsCompatibleWithFunctionError<A, F>::value(),
                  "Factory method is incompatible with argument passed");
  }
  Promise Once(Arg arg);
  Promise Repeated(Arg arg);
};

template <typename A, typename F>
class Factory<A, F,
              typename std::enable_if<IsPoll<decltype(
                  std::declval<F>()(std::declval<A>()))>::value()>::type> {
 public:
  using Arg = A;
  class Promise {
   public:
    Promise(F f, Arg arg) : f_(std::move(f)), arg_(std::move(arg)) {}

    using Result = decltype(std::declval<F>()(std::declval<Arg>()));

    Result operator()() { return f_(arg_); }

   private:
    [[no_unique_address]] F f_;
    [[no_unique_address]] Arg arg_;
  };

  Promise Once(Arg arg) { return Promise(std::move(f_), std::move(arg)); }
  Promise Repeated(Arg arg) { return Promise(f_, std::move(arg)); }

  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  [[no_unique_address]] F f_;
};

template <typename A, typename F>
class Factory<A, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()())>::value()>::type> {
 public:
  using Promise = F;
  using Arg = A;
  Promise Once(Arg arg) { return std::move(f_); }
  Promise Repeated(Arg arg) { return f_; }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  [[no_unique_address]] F f_;
};

template <typename F>
class Factory<void, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()())>::value()>::type> {
 public:
  using Arg = void;
  using Promise = F;
  Promise Once() { return std::move(f_); }
  Promise Repeated() { return f_; }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  [[no_unique_address]] F f_;
};

template <typename A, typename F>
class Factory<A, F,
              typename std::enable_if<IsPoll<decltype(
                  std::declval<F>()(std::declval<A>())())>::value()>::type> {
 public:
  using Arg = A;
  using Promise = decltype(std::declval<F>()(std::declval<Arg>()));
  Promise Once(Arg arg) { return f_(std::move(arg)); }
  Promise Repeated(Arg arg) { return f_(std::move(arg)); }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  [[no_unique_address]] F f_;
};

template <typename A, typename F>
class Factory<A, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()()())>::value()>::type> {
 public:
  using Arg = A;
  using Promise = decltype(std::declval<F>()());
  Promise Once(Arg arg) { return f_(); }
  Promise Repeated(Arg arg) { return f_(); }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  [[no_unique_address]] F f_;
};

template <typename F>
class Factory<void, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()()())>::value()>::type> {
 public:
  using Arg = void;
  using Promise = decltype(std::declval<F>()());
  Promise Once() { return f_(); }
  Promise Repeated() { return f_(); }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  [[no_unique_address]] F f_;
};

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
