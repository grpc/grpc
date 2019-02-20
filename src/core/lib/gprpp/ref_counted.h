/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_GPRPP_REF_COUNTED_H
#define GRPC_CORE_LIB_GPRPP_REF_COUNTED_H

#include <grpc/support/port_platform.h>

#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <atomic>
#include <cassert>
#include <cinttypes>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

// PolymorphicRefCount enforces polymorphic destruction of RefCounted.
class PolymorphicRefCount {
 public:
  GRPC_ABSTRACT_BASE_CLASS

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  virtual ~PolymorphicRefCount() = default;
};

// NonPolymorphicRefCount does not enforce polymorphic destruction of
// RefCounted. Please refer to grpc_core::RefCounted for more details, and
// when in doubt use PolymorphicRefCount.
class NonPolymorphicRefCount {
 public:
  GRPC_ABSTRACT_BASE_CLASS

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  ~NonPolymorphicRefCount() = default;
};

// RefCount is a simple atomic ref-count.
//
// This is a C++ implementation of gpr_refcount, with inline functions. Due to
// inline functions, this class is significantly more efficient than
// gpr_refcount and should be preferred over gpr_refcount whenever possible.
//
// TODO(soheil): Remove gpr_refcount after submitting the GRFC and the paragraph
//               above.
class RefCount {
 public:
  using Value = intptr_t;

  // `init` is the initial refcount stored in this object.
  //
  // TraceFlagT is defined to accept both DebugOnlyTraceFlag and TraceFlag.
  // Note: RefCount tracing is only enabled on debug builds, even when a
  //       TraceFlag is used.
  template <typename TraceFlagT = TraceFlag>
  constexpr explicit RefCount(Value init = 1, TraceFlagT* trace_flag = nullptr)
      :
#ifndef NDEBUG
        trace_flag_(trace_flag),
#endif
        value_(init) {
  }

  // Increases the ref-count by `n`.
  void Ref(Value n = 1) {
    GPR_ATM_INC_ADD_THEN(value_.fetch_add(n, std::memory_order_relaxed));
  }
  void Ref(const DebugLocation& location, const char* reason, Value n = 1) {
#ifndef NDEBUG
    if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
      const RefCount::Value old_refs = get();
      gpr_log(GPR_INFO, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_flag_->name(), this, location.file(), location.line(),
              old_refs, old_refs + n, reason);
    }
#endif
    Ref(n);
  }

  // Similar to Ref() with an assert on the ref-count being non-zero.
  void RefNonZero() {
#ifndef NDEBUG
    const Value prior =
        GPR_ATM_INC_ADD_THEN(value_.fetch_add(1, std::memory_order_relaxed));
    assert(prior > 0);
#else
    Ref();
#endif
  }
  void RefNonZero(const DebugLocation& location, const char* reason) {
#ifndef NDEBUG
    if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
      const RefCount::Value old_refs = get();
      gpr_log(GPR_INFO, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_flag_->name(), this, location.file(), location.line(),
              old_refs, old_refs + 1, reason);
    }
#endif
    RefNonZero();
  }

  // Decrements the ref-count and returns true if the ref-count reaches 0.
  bool Unref() {
    const Value prior =
        GPR_ATM_INC_ADD_THEN(value_.fetch_sub(1, std::memory_order_acq_rel));
    GPR_DEBUG_ASSERT(prior > 0);
    return prior == 1;
  }
  bool Unref(const DebugLocation& location, const char* reason) {
#ifndef NDEBUG
    if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
      const RefCount::Value old_refs = get();
      gpr_log(GPR_INFO, "%s:%p %s:%d unref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_flag_->name(), this, location.file(), location.line(),
              old_refs, old_refs - 1, reason);
    }
#endif
    return Unref();
  }

 private:
  Value get() const { return value_.load(std::memory_order_relaxed); }

#ifndef NDEBUG
  TraceFlag* trace_flag_;
#endif
  std::atomic<Value> value_;
};

// A base class for reference-counted objects.
// New objects should be created via New() and start with a refcount of 1.
// When the refcount reaches 0, the object will be deleted via Delete().
//
// This will commonly be used by CRTP (curiously-recurring template pattern)
// e.g., class MyClass : public RefCounted<MyClass>
//
// Use PolymorphicRefCount and NonPolymorphicRefCount to select between
// different implementations of RefCounted.
//
// Note that NonPolymorphicRefCount does not support polymorphic destruction.
// So, use NonPolymorphicRefCount only when both of the following conditions
// are guaranteed to hold:
// (a) Child is a concrete leaf class in RefCounted<Child>, and
// (b) you are gauranteed to call Unref only on concrete leaf classes and not
//     their parents.
//
// The following example is illegal, because calling Unref() will not call
// the dtor of Child.
//
//    class Parent : public RefCounted<Parent, NonPolymorphicRefCount> {}
//    class Child : public Parent {}
//
//    Child* ch;
//    ch->Unref();
//
template <typename Child, typename Impl = PolymorphicRefCount>
class RefCounted : public Impl {
 public:
  RefCountedPtr<Child> Ref() GRPC_MUST_USE_RESULT {
    IncrementRefCount();
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  RefCountedPtr<Child> Ref(const DebugLocation& location,
                           const char* reason) GRPC_MUST_USE_RESULT {
    IncrementRefCount(location, reason);
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  // TODO(roth): Once all of our code is converted to C++ and can use
  // RefCountedPtr<> instead of manual ref-counting, make this method
  // private, since it will only be used by RefCountedPtr<>, which is a
  // friend of this class.
  void Unref() {
    if (refs_.Unref()) {
      Delete(static_cast<Child*>(this));
    }
  }
  void Unref(const DebugLocation& location, const char* reason) {
    if (refs_.Unref(location, reason)) {
      Delete(static_cast<Child*>(this));
    }
  }

  // Not copyable nor movable.
  RefCounted(const RefCounted&) = delete;
  RefCounted& operator=(const RefCounted&) = delete;

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  GPRC_ALLOW_CLASS_TO_USE_NON_PUBLIC_DELETE

  // TraceFlagT is defined to accept both DebugOnlyTraceFlag and TraceFlag.
  // Note: RefCount tracing is only enabled on debug builds, even when a
  //       TraceFlag is used.
  template <typename TraceFlagT = TraceFlag>
  explicit RefCounted(TraceFlagT* trace_flag = nullptr)
      : refs_(1, trace_flag) {}

  // Note: Depending on the Impl used, this dtor can be implicitly virtual.
  ~RefCounted() = default;

 private:
  // Allow RefCountedPtr<> to access IncrementRefCount().
  template <typename T>
  friend class RefCountedPtr;

  void IncrementRefCount() { refs_.Ref(); }
  void IncrementRefCount(const DebugLocation& location, const char* reason) {
    refs_.Ref(location, reason);
  }

  RefCount refs_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_REF_COUNTED_H */
