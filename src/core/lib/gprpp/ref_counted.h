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

#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

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
  // `trace` is a string to be logged with trace events; if null, no
  // trace logging will be done.  Tracing is a no-op in non-debug builds.
  explicit RefCount(
      Value init = 1,
      const char*
#ifndef NDEBUG
          // Leave unnamed if NDEBUG to avoid unused parameter warning
          trace
#endif
      = nullptr)
      :
#ifndef NDEBUG
        trace_(trace),
#endif
        value_(init) {
  }

  // Increases the ref-count by `n`.
  void Ref(Value n = 1) {
#ifndef NDEBUG
    const Value prior = value_.FetchAdd(n, MemoryOrder::RELAXED);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p ref %" PRIdPTR " -> %" PRIdPTR, trace_, this,
              prior, prior + n);
    }
#else
    value_.FetchAdd(n, MemoryOrder::RELAXED);
#endif
  }
  void Ref(const DebugLocation& location, const char* reason, Value n = 1) {
#ifndef NDEBUG
    const Value prior = value_.FetchAdd(n, MemoryOrder::RELAXED);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_, this, location.file(), location.line(), prior, prior + n,
              reason);
    }
#else
    // Use conditionally-important parameters
    (void)location;
    (void)reason;
    value_.FetchAdd(n, MemoryOrder::RELAXED);
#endif
  }

  // Similar to Ref() with an assert on the ref-count being non-zero.
  void RefNonZero() {
#ifndef NDEBUG
    const Value prior = value_.FetchAdd(1, MemoryOrder::RELAXED);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p ref %" PRIdPTR " -> %" PRIdPTR, trace_, this,
              prior, prior + 1);
    }
    assert(prior > 0);
#else
    value_.FetchAdd(1, MemoryOrder::RELAXED);
#endif
  }
  void RefNonZero(const DebugLocation& location, const char* reason) {
#ifndef NDEBUG
    const Value prior = value_.FetchAdd(1, MemoryOrder::RELAXED);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_, this, location.file(), location.line(), prior, prior + 1,
              reason);
    }
    assert(prior > 0);
#else
    // Avoid unused-parameter warnings for debug-only parameters
    (void)location;
    (void)reason;
    RefNonZero();
#endif
  }

  bool RefIfNonZero() {
#ifndef NDEBUG
    if (trace_ != nullptr) {
      const Value prior = get();
      gpr_log(GPR_INFO, "%s:%p ref_if_non_zero %" PRIdPTR " -> %" PRIdPTR,
              trace_, this, prior, prior + 1);
    }
#endif
    return value_.IncrementIfNonzero();
  }
  bool RefIfNonZero(const DebugLocation& location, const char* reason) {
#ifndef NDEBUG
    if (trace_ != nullptr) {
      const Value prior = get();
      gpr_log(GPR_INFO,
              "%s:%p %s:%d ref_if_non_zero %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_, this, location.file(), location.line(), prior, prior + 1,
              reason);
    }
#endif
    // Avoid unused-parameter warnings for debug-only parameters
    (void)location;
    (void)reason;
    return value_.IncrementIfNonzero();
  }

  // Decrements the ref-count and returns true if the ref-count reaches 0.
  bool Unref() {
#ifndef NDEBUG
    // Grab a copy of the trace flag before the atomic change, since we
    // will no longer be holding a ref afterwards and therefore can't
    // safely access it, since another thread might free us in the interim.
    auto* trace = trace_;
#endif
    const Value prior = value_.FetchSub(1, MemoryOrder::ACQ_REL);
#ifndef NDEBUG
    if (trace != nullptr) {
      gpr_log(GPR_INFO, "%s:%p unref %" PRIdPTR " -> %" PRIdPTR, trace, this,
              prior, prior - 1);
    }
    GPR_DEBUG_ASSERT(prior > 0);
#endif
    return prior == 1;
  }
  bool Unref(const DebugLocation& location, const char* reason) {
#ifndef NDEBUG
    // Grab a copy of the trace flag before the atomic change, since we
    // will no longer be holding a ref afterwards and therefore can't
    // safely access it, since another thread might free us in the interim.
    auto* trace = trace_;
#endif
    const Value prior = value_.FetchSub(1, MemoryOrder::ACQ_REL);
#ifndef NDEBUG
    if (trace != nullptr) {
      gpr_log(GPR_INFO, "%s:%p %s:%d unref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace, this, location.file(), location.line(), prior, prior - 1,
              reason);
    }
    GPR_DEBUG_ASSERT(prior > 0);
#else
    // Avoid unused-parameter warnings for debug-only parameters
    (void)location;
    (void)reason;
#endif
    return prior == 1;
  }

 private:
  Value get() const { return value_.Load(MemoryOrder::RELAXED); }

#ifndef NDEBUG
  const char* trace_;
#endif
  Atomic<Value> value_;
};

// PolymorphicRefCount enforces polymorphic destruction of RefCounted.
class PolymorphicRefCount {
 public:
  virtual ~PolymorphicRefCount() = default;
};

// NonPolymorphicRefCount does not enforce polymorphic destruction of
// RefCounted. Please refer to grpc_core::RefCounted for more details, and
// when in doubt use PolymorphicRefCount.
class NonPolymorphicRefCount {
 public:
  ~NonPolymorphicRefCount() = default;
};

// Behavior of RefCounted<> upon ref count reaching 0.
enum UnrefBehavior {
  // Default behavior: Delete the object.
  kUnrefDelete,
  // Do not delete the object upon unref.  This is useful in cases where all
  // existing objects must be tracked in a registry but the object's entry in
  // the registry cannot be removed from the object's dtor due to
  // synchronization issues.  In this case, the registry can be cleaned up
  // later by identifying entries for which RefIfNonZero() returns null.
  kUnrefNoDelete,
  // Call the object's dtor but do not delete it.  This is useful for cases
  // where the object is stored in memory allocated elsewhere (e.g., the call
  // arena).
  kUnrefCallDtor,
};

namespace internal {
template <typename T, UnrefBehavior UnrefBehaviorArg>
class Delete;
template <typename T>
class Delete<T, kUnrefDelete> {
 public:
  explicit Delete(T* t) { delete t; }
};
template <typename T>
class Delete<T, kUnrefNoDelete> {
 public:
  explicit Delete(T* /*t*/) {}
};
template <typename T>
class Delete<T, kUnrefCallDtor> {
 public:
  explicit Delete(T* t) { t->~T(); }
};
}  // namespace internal

// A base class for reference-counted objects.
// New objects should be created via new and start with a refcount of 1.
// When the refcount reaches 0, executes the specified UnrefBehavior.
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
// (b) you are guaranteed to call Unref only on concrete leaf classes and not
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
template <typename Child, typename Impl = PolymorphicRefCount,
          UnrefBehavior UnrefBehaviorArg = kUnrefDelete>
class RefCounted : public Impl {
 public:
  // Note: Depending on the Impl used, this dtor can be implicitly virtual.
  ~RefCounted() = default;

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
    if (GPR_UNLIKELY(refs_.Unref())) {
      internal::Delete<Child, UnrefBehaviorArg>(static_cast<Child*>(this));
    }
  }
  void Unref(const DebugLocation& location, const char* reason) {
    if (GPR_UNLIKELY(refs_.Unref(location, reason))) {
      internal::Delete<Child, UnrefBehaviorArg>(static_cast<Child*>(this));
    }
  }

  RefCountedPtr<Child> RefIfNonZero() GRPC_MUST_USE_RESULT {
    return RefCountedPtr<Child>(refs_.RefIfNonZero() ? static_cast<Child*>(this)
                                                     : nullptr);
  }
  RefCountedPtr<Child> RefIfNonZero(const DebugLocation& location,
                                    const char* reason) GRPC_MUST_USE_RESULT {
    return RefCountedPtr<Child>(refs_.RefIfNonZero(location, reason)
                                    ? static_cast<Child*>(this)
                                    : nullptr);
  }

  // Not copyable nor movable.
  RefCounted(const RefCounted&) = delete;
  RefCounted& operator=(const RefCounted&) = delete;

 protected:
  // Note: Tracing is a no-op on non-debug builds.
  explicit RefCounted(const char* trace = nullptr,
                      intptr_t initial_refcount = 1)
      : refs_(initial_refcount, trace) {}

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
