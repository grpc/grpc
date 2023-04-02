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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_CONTEXT_H
#define GRPC_SRC_CORE_LIB_PROMISE_CONTEXT_H

#include <grpc/support/port_platform.h>

#include <type_traits>
#include <utility>

#include <grpc/support/log.h>

namespace grpc_core {

// To avoid accidentally creating context types, we require an explicit
// specialization of this template per context type. The specialization need
// not contain any members, only exist.
// The reason for avoiding this is that context types each use a thread local.
template <typename T>
struct ContextType;  // IWYU pragma: keep

namespace promise_detail {

template <typename T>
class Context : public ContextType<T> {
 public:
  explicit Context(T* p) : old_(current_) { current_ = p; }
  ~Context() { current_ = old_; }
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  static T* get() { return current_; }

 private:
  T* const old_;
  static thread_local T* current_;
};

template <typename T>
thread_local T* Context<T>::current_;

template <typename T, typename F>
class WithContext {
 public:
  WithContext(F f, T* context) : context_(context), f_(std::move(f)) {}

  decltype(std::declval<F>()()) operator()() {
    Context<T> ctx(context_);
    return f_();
  }

 private:
  T* context_;
  F f_;
};

}  // namespace promise_detail

// Return true if a context of type T is currently active.
template <typename T>
bool HasContext() {
  return promise_detail::Context<T>::get() != nullptr;
}

// Retrieve the current value of a context, or abort if the value is unset.
template <typename T>
T* GetContext() {
  auto* p = promise_detail::Context<T>::get();
  GPR_ASSERT(p != nullptr);
  return p;
}

// Given a promise and a context, return a promise that has that context set.
template <typename T, typename F>
promise_detail::WithContext<T, F> WithContext(F f, T* context) {
  return promise_detail::WithContext<T, F>(std::move(f), context);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_CONTEXT_H
