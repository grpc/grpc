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

template <typename T>
class ImplInterface {
 public:
  virtual Poll<T> PollOnce() = 0;
  virtual void Destroy() = 0;

 protected:
  ~ImplInterface() = default;
};

template <typename T>
class NullImpl final : public ImplInterface<T> {
 public:
  Poll<T> PollOnce() override {
    abort();
    GPR_UNREACHABLE_CODE(return Pending{});
  }
  void Destroy() override {}

  static ImplInterface<T>* Get() {
    static NullImpl<T> instance;
    return &instance;
  }
};

template <typename T, typename Callable>
class CallableImpl final : public ImplInterface<T> {
 public:
  explicit CallableImpl(Callable&& callable) : callable_(std::move(callable)) {}
  Poll<T> PollOnce() override { return callable_(); }
  void Destroy() override { this->~CallableImpl(); }

 private:
  Callable callable_;
};

template <typename T, typename Callable>
class SharedImpl final : public ImplInterface<T> {
 public:
  Poll<T> PollOnce() override { return (*static_cast<Callable*>(nullptr))(); }
  void Destroy() override {}
  static SharedImpl* Get() {
    static SharedImpl impl;
    return &impl;
  }

 private:
  SharedImpl() = default;
  ~SharedImpl() = default;
};

template <typename T, typename Callable, typename Ignored = void>
struct ChooseImplForCallable;

template <typename T, typename Callable>
struct ChooseImplForCallable<
    T, Callable, absl::enable_if_t<!AllowSharedAllocation<Callable>(), void>> {
  static ImplInterface<T>* Make(Arena* arena, Callable&& callable) {
    return arena->template New<CallableImpl<T, Callable>>(
        std::forward<Callable>(callable));
  }
};

template <typename T, typename Callable>
struct ChooseImplForCallable<
    T, Callable, absl::enable_if_t<AllowSharedAllocation<Callable>(), void>> {
  static ImplInterface<T>* Make(Arena*, Callable&&) {
    return SharedImpl<T, Callable>::Get();
  }
};

template <typename T, typename Callable>
ImplInterface<T>* MakeImplForCallable(Arena* arena, Callable&& callable) {
  return ChooseImplForCallable<T, Callable>::Make(
      arena, std::forward<Callable>(callable));
}

}  // namespace arena_promise_detail

// A promise for which the state memory is allocated from an arena.
template <typename T>
class ArenaPromise {
 public:
  // Construct an empty, uncallable, invalid ArenaPromise.
  ArenaPromise() = default;

  // Construct an ArenaPromise that will call the given callable when polled.
  template <typename Callable>
  ArenaPromise(Arena* arena, Callable&& callable)
      : impl_(arena_promise_detail::MakeImplForCallable<T>(
            arena, std::forward<Callable>(callable))) {}

  ArenaPromise(const ArenaPromise&) = delete;
  ArenaPromise& operator=(const ArenaPromise&) = delete;
  ArenaPromise(ArenaPromise&& other) noexcept : impl_(other.impl_) {
    other.impl_ = arena_promise_detail::NullImpl<T>::Get();
  }
  ArenaPromise& operator=(ArenaPromise&& other) noexcept {
    impl_ = other.impl_;
    other.impl_ = arena_promise_detail::NullImpl<T>::Get();
    return *this;
  }

  ~ArenaPromise() { impl_->Destroy(); }

  Poll<T> operator()() { return impl_->PollOnce(); }

 private:
  arena_promise_detail::ImplInterface<T>* impl_ =
      arena_promise_detail::NullImpl<T>::Get();
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_PROMISE_ARENA_PROMISE_H */
