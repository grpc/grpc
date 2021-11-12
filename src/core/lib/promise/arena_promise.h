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
  return std::is_empty<Callable>::value &&
         std::is_default_constructible<Callable>::value;
}

}  // namespace arena_promise_detail

template <typename T>
class ArenaPromise {
 public:
  ArenaPromise() = default;

  template <typename Callable>
  ArenaPromise(Arena* arena,
               absl::enable_if_t<
                   !arena_promise_detail::AllowSharedAllocation<Callable>(),
                   Callable>&& callable)
      : impl_(arena->New<CallableImpl<Callable>>(
            std::forward<Callable>(callable))) {}

  template <typename Callable>
  explicit ArenaPromise(
      absl::enable_if_t<arena_promise_detail::AllowSharedAllocation<Callable>(),
                        Callable>&& callable)
      : impl_(SharedImpl<Callable>::Get()) {}

  template <typename Callable>
  explicit ArenaPromise(
      Arena*,
      absl::enable_if_t<arena_promise_detail::AllowSharedAllocation<Callable>(),
                        Callable>&& callable)
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

 private:
  class ImplInterface {
   public:
    virtual Poll<T> Poll() = 0;
    virtual ~ImplInterface() = default;
  };

  class NullImpl : public ImplInterface {
   public:
    Poll<T> Poll() override {
      abort();
      GPR_UNREACHABLE_CODE(return Pending{});
    }
  };

  template <typename Callable>
  class CallableImpl {
   public:
    explicit CallableImpl(Callable&& callable)
        : callable_(std::move(callable)) {}
    Poll<T> Poll() override { return callable_(); }

   private:
    Callable callable_;
  };

  template <typename Callable>
  class SharedImpl {
   public:
    Poll<T> Poll() override { return callable_(); }
    static SharedImpl* Get() { return &impl_; }

   private:
    SharedImpl() = default;
    ~SharedImpl() = default;

    static SharedImpl impl_;
    static Callable callable_;
  };

  static NullImpl null_impl_;
  ImplInterface* impl_ = &null_impl_;
};

template <typename T>
template <typename Callable>
typename ArenaPromise<T>::template SharedImpl<Callable>
    ArenaPromise<T>::SharedImpl<Callable>::impl_;

template <typename T>
template <typename Callable>
Callable ArenaPromise<T>::SharedImpl<Callable>::callable_;

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_PROMISE_ARENA_PROMISE_H */
