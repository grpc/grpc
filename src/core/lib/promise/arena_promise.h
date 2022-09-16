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

#include <stdlib.h>

#include <new>
#include <utility>

#include "absl/meta/type_traits.h"

#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

namespace arena_promise_detail {

template <typename T>
struct Vtable {
  // Poll the promise, once.
  Poll<T> (*poll_once)(void** arg);
  // Destroy the underlying callable object if there is one.
  // Since we don't delete (the arena owns the memory) but we may need to call a
  // destructor, we expose this for when the ArenaPromise object is destroyed.
  void (*destroy)(void** arg);
};

template <typename T>
struct VtableAndArg {
  const Vtable<T>* vtable;
  void* arg;
};

// Implementation of Vtable for an empty object.
// Used when an empty ArenaPromise is created, or when the ArenaPromise is moved
// from. Since in either case these objects should not be polled, we simply
// crash if it is.
template <typename T>
inline const Vtable<T>* null_impl() {
  static const Vtable<T> vtable = {[](void**) -> Poll<T> {
                                     abort();
                                     GPR_UNREACHABLE_CODE(return Pending{});
                                   },
                                   [](void**) {}};
  return &vtable;
}

// Implementation of ImplInterface for a callable object.
template <typename T, typename Callable>
inline const Vtable<T>* allocated_callable_impl() {
  static const Vtable<T> vtable = {
      [](void** arg) -> Poll<T> {
        return poll_cast<T>((*static_cast<Callable*>(*arg))());
      },
      [](void** arg) { static_cast<Callable*>(*arg)->~Callable(); }};
  return &vtable;
}

// Implementation of ImplInterface for a small callable object (one that fits
// within the void* arg)
template <typename T, typename Callable>
inline const Vtable<T>* inlined_callable_impl() {
  static const Vtable<T> vtable = {
      [](void** arg) -> Poll<T> {
        return poll_cast<T>((*reinterpret_cast<Callable*>(arg))());
      },
      [](void** arg) { reinterpret_cast<Callable*>(arg)->~Callable(); }};
  return &vtable;
}

// If a callable object is empty we can substitute any instance of that callable
// for the one we call (for how could we tell the difference)?
// Since this corresponds to a lambda with no fields, and we expect these to be
// reasonably common, we can elide the arena allocation entirely and simply poll
// a global shared instance.
// (this comes up often when the promise only accesses context data from the
// containing activity).
template <typename T, typename Callable>
inline const Vtable<T>* shared_callable_impl(Callable&& callable) {
  static Callable instance = std::forward<Callable>(callable);
  static const Vtable<T> vtable = {[](void**) -> Poll<T> { return instance(); },
                                   [](void**) {}};
  return &vtable;
}

// Redirector type: given a callable type, expose a Make() function that creates
// the appropriate underlying implementation.
template <typename T, typename Callable, typename Ignored = void>
struct ChooseImplForCallable;

template <typename T, typename Callable>
struct ChooseImplForCallable<
    T, Callable,
    absl::enable_if_t<!std::is_empty<Callable>::value &&
                      (sizeof(Callable) > sizeof(void*))>> {
  static void Make(Callable&& callable, VtableAndArg<T>* out) {
    *out = {allocated_callable_impl<T, Callable>(),
            GetContext<Arena>()->template New<Callable>(
                std::forward<Callable>(callable))};
  }
};

template <typename T, typename Callable>
struct ChooseImplForCallable<
    T, Callable,
    absl::enable_if_t<!std::is_empty<Callable>::value &&
                      (sizeof(Callable) <= sizeof(void*))>> {
  static void Make(Callable&& callable, VtableAndArg<T>* out) {
    out->vtable = inlined_callable_impl<T, Callable>();
    new (&out->arg) Callable(std::forward<Callable>(callable));
  }
};

template <typename T, typename Callable>
struct ChooseImplForCallable<
    T, Callable, absl::enable_if_t<std::is_empty<Callable>::value>> {
  static void Make(Callable&& callable, VtableAndArg<T>* out) {
    out->vtable =
        shared_callable_impl<T, Callable>(std::forward<Callable>(callable));
  }
};

// Wrap ChooseImplForCallable with a friend approachable syntax.
template <typename T, typename Callable>
void MakeImplForCallable(Callable&& callable, VtableAndArg<T>* out) {
  ChooseImplForCallable<T, Callable>::Make(std::forward<Callable>(callable),
                                           out);
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
  // NOLINTNEXTLINE(google-explicit-constructor)
  ArenaPromise(Callable&& callable) {
    arena_promise_detail::MakeImplForCallable<T>(
        std::forward<Callable>(callable), &vtable_and_arg_);
  }

  // ArenaPromise is not copyable.
  ArenaPromise(const ArenaPromise&) = delete;
  ArenaPromise& operator=(const ArenaPromise&) = delete;
  // ArenaPromise is movable.
  ArenaPromise(ArenaPromise&& other) noexcept
      : vtable_and_arg_(other.vtable_and_arg_) {
    other.vtable_and_arg_.vtable = arena_promise_detail::null_impl<T>();
  }
  ArenaPromise& operator=(ArenaPromise&& other) noexcept {
    vtable_and_arg_.vtable->destroy(&vtable_and_arg_.arg);
    vtable_and_arg_ = other.vtable_and_arg_;
    other.vtable_and_arg_.vtable = arena_promise_detail::null_impl<T>();
    return *this;
  }

  // Destruction => call Destroy on the underlying impl object.
  ~ArenaPromise() { vtable_and_arg_.vtable->destroy(&vtable_and_arg_.arg); }

  // Expose the promise interface: a call operator that returns Poll<T>.
  Poll<T> operator()() {
    return vtable_and_arg_.vtable->poll_once(&vtable_and_arg_.arg);
  }

  bool has_value() const {
    return vtable_and_arg_.vtable != arena_promise_detail::null_impl<T>();
  }

 private:
  // Underlying impl object.
  arena_promise_detail::VtableAndArg<T> vtable_and_arg_ = {
      arena_promise_detail::null_impl<T>(), nullptr};
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_PROMISE_ARENA_PROMISE_H */
