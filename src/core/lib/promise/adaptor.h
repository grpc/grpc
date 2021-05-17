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

#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace adaptor_detail {

template <typename T>
struct IsPoll {
  static constexpr bool value = false;
};
template <typename T>
struct IsPoll<Poll<T>> {
  static constexpr bool value = true;
};

template <typename Arg, typename F, typename Ignored = void>
class Factory;

template <typename Arg, typename F>
class Factory<Arg, F,
              typename std::enable_if<IsPoll<decltype(
                  std::declval<F>()(std::declval<Arg>()))>::value>::type> {
 public:
  class Promise {
   public:
    Promise(F f, Arg arg) : f_(std::move(f)), arg_(std::move(arg)) {}

    using Result = decltype(std::declval<F>()(std::declval<Arg>()));

    Result operator()() { return f_(arg_); }

   private:
    F f_;
    Arg arg_;
  };

  Promise Once(Arg arg) { return Promise(std::move(f_), std::move(arg)); }
  Promise Repeated(Arg arg) { return Promise(f_, std::move(arg)); }

  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  F f_;
};

template <typename Arg, typename F>
class Factory<Arg, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()())>::value>::type> {
 public:
  using Promise = F;
  Promise Once(Arg arg) { return std::move(f_); }
  Promise Repeated(Arg arg) { return f_; }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  F f_;
};

template <typename F>
class Factory<void, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()())>::value>::type> {
 public:
  using Promise = F;
  Promise Once() { return std::move(f_); }
  Promise Repeated() { return f_; }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  F f_;
};

template <typename Arg, typename F>
class Factory<Arg, F,
              typename std::enable_if<IsPoll<decltype(
                  std::declval<F>()(std::declval<Arg>())())>::value>::type> {
 public:
  using Promise = decltype(std::declval<F>()(std::declval<Arg>()));
  Promise Once(Arg arg) { return f_(std::move(arg)); }
  Promise Repeated(Arg arg) { return f_(std::move(arg)); }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  F f_;
};

template <typename Arg, typename F>
class Factory<Arg, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()()())>::value>::type> {
 public:
  using Promise = decltype(std::declval<F>()());
  Promise Once(Arg arg) { return f_(); }
  Promise Repeated(Arg arg) { return f_(); }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  F f_;
};

template <typename F>
class Factory<void, F,
              typename std::enable_if<
                  IsPoll<decltype(std::declval<F>()()())>::value>::type> {
 public:
  using Promise = decltype(std::declval<F>()());
  Promise Once() { return f_(); }
  Promise Repeated() { return f_(); }
  explicit Factory(F f) : f_(std::move(f)) {}

 private:
  F f_;
};

}  // namespace adaptor_detail

}  // namespace grpc_core

#endif
