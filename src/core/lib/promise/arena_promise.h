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

#ifndef GRPC_CORE_LIB_PROMISE_ARENA_PROMISE_H
#define GRPC_CORE_LIB_PROMISE_ARENA_PROMISE_H

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/arena.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace arena_promise_detail {

template <typename Callable>
static constexpr bool AllowSharedAllocation() {
  return std::is_empty<Callable>::value;
}

struct UsersShouldNotTypeThis {};

}  // namespace arena_promise_detail

// A promise for which the state memory is allocated from an arena.
template <typename T>
class ArenaPromise {
 public:
  // Construct an empty, uncallable, invalid ArenaPromise.
  ArenaPromise() = default;

  // Construct an ArenaPromise that will call the given callable when polled.
  template <typename Callable>
  ArenaPromise(
      absl::enable_if_t<
          !arena_promise_detail::AllowSharedAllocation<Callable>(), Arena*>
          arena,
      Callable&& callable)
      : impl_(arena->template New<CallableImpl<Callable>>(
            std::forward<Callable>(callable))) {}

  // Construct an ArenaPromise that will call the given callable when polled.
  // This variant exploits the emptiness of many callables to avoid actually
  // allocating memory for the promise.
  template <typename Callable>
  explicit ArenaPromise(
      Callable&&,
      absl::enable_if_t<arena_promise_detail::AllowSharedAllocation<Callable>(),
                        arena_promise_detail::UsersShouldNotTypeThis> =
          arena_promise_detail::UsersShouldNotTypeThis())
      : impl_(SharedImpl<Callable>::Get()) {}

  // Construct an ArenaPromise that will call the given callable when polled.
  // This variant exploits the emptiness of many callables to avoid actually
  // allocating memory for the promise.
  template <typename Callable>
  explicit ArenaPromise(
      absl::enable_if_t<arena_promise_detail::AllowSharedAllocation<Callable>(),
                        Arena*>,
      Callable&&)
      : impl_(SharedImpl<Callable>::Get()) {}

  ArenaPromise(const ArenaPromise&) = delete;
  ArenaPromise& operator=(const ArenaPromise&) = delete;
  ArenaPromise(ArenaPromise&& other) noexcept : impl_(other.impl_) {
    other.impl_ = &null_impl_;
  }
  ArenaPromise& operator=(ArenaPromise&& other) noexcept {
    impl_ = other.impl_;
    other.impl_ = &null_impl_;
    return *this;
  }

  ~ArenaPromise() { impl_->~ImplInterface(); }

  Poll<T> operator()() { return impl_->Poll(); }

 private:
  class ImplInterface {
   public:
    virtual Poll<T> Poll() = 0;
    virtual ~ImplInterface() = default;
  };

  class NullImpl final : public ImplInterface {
   public:
    Poll<T> Poll() override {
      abort();
      GPR_UNREACHABLE_CODE(return Pending{});
    }
  };

  template <typename Callable>
  class CallableImpl final : public ImplInterface {
   public:
    explicit CallableImpl(Callable&& callable)
        : callable_(std::move(callable)) {}
    Poll<T> Poll() override { return callable_(); }

   private:
    Callable callable_;
  };

  template <typename Callable>
  class SharedImpl final : public ImplInterface {
   public:
    Poll<T> Poll() override { return (*static_cast<Callable*>(nullptr))(); }
    static SharedImpl* Get() { return &impl_; }

   private:
    SharedImpl() = default;
    ~SharedImpl() = default;

    static SharedImpl impl_;
  };

  static NullImpl null_impl_;
  ImplInterface* impl_ = &null_impl_;
};

template <typename T>
typename ArenaPromise<T>::NullImpl ArenaPromise<T>::null_impl_;

template <typename T>
template <typename Callable>
typename ArenaPromise<T>::template SharedImpl<Callable>
    ArenaPromise<T>::SharedImpl<Callable>::impl_;

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_PROMISE_ARENA_PROMISE_H */
