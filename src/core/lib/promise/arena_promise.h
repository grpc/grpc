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

#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

namespace arena_promise_detail {

// Type erased promise stored in the arena.
template <typename T>
class ImplInterface {
 public:
  // Poll the promise, once.
  virtual Poll<T> PollOnce() = 0;
  // Destroy the underlying callable object if there is one.
  // Since we don't delete (the arena owns the memory) but we may need to call a
  // destructor, we expose this for when the ArenaPromise object is destroyed.
  virtual void Destroy() = 0;

 protected:
  ~ImplInterface() = default;
};

// Implementation of ImplInterface for an empty object.
// Used when an empty ArenaPromise is created, or when the ArenaPromise is moved
// from. Since in either case these objects should not be polled, we simply
// crash if it is.
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

 private:
  ~NullImpl() = default;
};

// Implementation of ImplInterface for a callable object.
template <typename T, typename Callable>
class CallableImpl final : public ImplInterface<T> {
 public:
  explicit CallableImpl(Callable&& callable) : callable_(std::move(callable)) {}
  // Forward polls to the callable object.
  Poll<T> PollOnce() override { return callable_(); }
  // Destroy destructs the callable object.
  void Destroy() override { this->~CallableImpl(); }

 private:
  // Should only be called by Destroy().
  ~CallableImpl() = default;

  Callable callable_;
};

// If a callable object is empty we can substitute any instance of that callable
// for the one we call (for how could we tell the difference)?
// Since this corresponds to a lambda with no fields, and we expect these to be
// reasonably common, we can elide the arena allocation entirely and simply poll
// a global shared instance.
// (this comes up often when the promise only accesses context data from the
// containing activity).
template <typename T, typename Callable>
class SharedImpl final : public ImplInterface<T>, private Callable {
 public:
  // Call the callable, or at least an exact duplicate of it - if you have no
  // members, all your instances look the same.
  Poll<T> PollOnce() override { return Callable::operator()(); }
  // Nothing to destroy.
  void Destroy() override {}
  // Return a pointer to the shared instance - these are singletons, and are
  // needed just to get the vtable in place.
  static SharedImpl* Get(Callable&& callable) {
    static_assert(sizeof(SharedImpl) == sizeof(void*),
                  "SharedImpl should be pointer sized");
    static SharedImpl impl(std::forward<Callable>(callable));
    return &impl;
  }

 private:
  explicit SharedImpl(Callable&& callable)
      : Callable(std::forward<Callable>(callable)) {}
  ~SharedImpl() = default;
};

// Redirector type: given a callable type, expose a Make() function that creates
// the appropriate underlying implementation.
template <typename T, typename Callable, typename Ignored = void>
struct ChooseImplForCallable;

template <typename T, typename Callable>
struct ChooseImplForCallable<
    T, Callable, absl::enable_if_t<!std::is_empty<Callable>::value>> {
  static ImplInterface<T>* Make(Callable&& callable) {
    return GetContext<Arena>()->template New<CallableImpl<T, Callable>>(
        std::forward<Callable>(callable));
  }
};

template <typename T, typename Callable>
struct ChooseImplForCallable<
    T, Callable, absl::enable_if_t<std::is_empty<Callable>::value>> {
  static ImplInterface<T>* Make(Callable&& callable) {
    return SharedImpl<T, Callable>::Get(std::forward<Callable>(callable));
  }
};

// Wrap ChooseImplForCallable with a friend approachable syntax.
template <typename T, typename Callable>
ImplInterface<T>* MakeImplForCallable(Callable&& callable) {
  return ChooseImplForCallable<T, Callable>::Make(
      std::forward<Callable>(callable));
}

}  // namespace arena_promise_detail

// A promise for which the state memory is allocated from an arena.
template <typename T>
class ArenaPromise {
 public:
  // Construct an empty, uncallable, invalid ArenaPromise.
  ArenaPromise() = default;

  // Construct an ArenaPromise that will call the given callable when polled.
  template <typename Callable,
            typename Ignored =
                absl::enable_if_t<!std::is_same<Callable, ArenaPromise>::value>>
  explicit ArenaPromise(Callable&& callable)
      : impl_(arena_promise_detail::MakeImplForCallable<T>(
            std::forward<Callable>(callable))) {}

  // ArenaPromise is not copyable.
  ArenaPromise(const ArenaPromise&) = delete;
  ArenaPromise& operator=(const ArenaPromise&) = delete;
  // ArenaPromise is movable.
  ArenaPromise(ArenaPromise&& other) noexcept : impl_(other.impl_) {
    other.impl_ = arena_promise_detail::NullImpl<T>::Get();
  }
  ArenaPromise& operator=(ArenaPromise&& other) noexcept {
    impl_ = other.impl_;
    other.impl_ = arena_promise_detail::NullImpl<T>::Get();
    return *this;
  }

  // Destruction => call Destroy on the underlying impl object.
  ~ArenaPromise() { impl_->Destroy(); }

  // Expose the promise interface: a call operator that returns Poll<T>.
  Poll<T> operator()() { return impl_->PollOnce(); }

 private:
  // Underlying impl object.
  arena_promise_detail::ImplInterface<T>* impl_ =
      arena_promise_detail::NullImpl<T>::Get();
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_PROMISE_ARENA_PROMISE_H */
