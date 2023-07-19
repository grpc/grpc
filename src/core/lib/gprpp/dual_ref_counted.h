//
// Copyright 2020 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_LIB_GPRPP_DUAL_REF_COUNTED_H
#define GRPC_SRC_CORE_LIB_GPRPP_DUAL_REF_COUNTED_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

// DualRefCounted is an interface for reference-counted objects with two
// classes of refs: strong refs (usually just called "refs") and weak refs.
// This supports cases where an object needs to start shutting down when
// all external callers are done with it (represented by strong refs) but
// cannot be destroyed until all internal callbacks are complete
// (represented by weak refs).
//
// Each class of refs can be incremented and decremented independently.
// Objects start with 1 strong ref and 0 weak refs at instantiation.
// When the strong refcount reaches 0, the object's Orphan() method is called.
// When the weak refcount reaches 0, the object is destroyed.
//
// This will be used by CRTP (curiously-recurring template pattern), e.g.:
//   class MyClass : public RefCounted<MyClass> { ... };
template <typename Child>
class DualRefCounted : public Orphanable {
 public:
  ~DualRefCounted() override = default;

  GRPC_MUST_USE_RESULT RefCountedPtr<Child> Ref() {
    IncrementRefCount();
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  GRPC_MUST_USE_RESULT RefCountedPtr<Child> Ref(const DebugLocation& location,
                                                const char* reason) {
    IncrementRefCount(location, reason);
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  void Unref() {
    // Convert strong ref to weak ref.
    const uint64_t prev_ref_pair =
        refs_.fetch_add(MakeRefPair(-1, 1), std::memory_order_acq_rel);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
#ifndef NDEBUG
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p unref %d -> %d, weak_ref %d -> %d", trace_, this,
              strong_refs, strong_refs - 1, weak_refs, weak_refs + 1);
    }
    GPR_ASSERT(strong_refs > 0);
#endif
    if (GPR_UNLIKELY(strong_refs == 1)) {
      Orphan();
    }
    // Now drop the weak ref.
    WeakUnref();
  }
  void Unref(const DebugLocation& location, const char* reason) {
    const uint64_t prev_ref_pair =
        refs_.fetch_add(MakeRefPair(-1, 1), std::memory_order_acq_rel);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
#ifndef NDEBUG
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p %s:%d unref %d -> %d, weak_ref %d -> %d) %s",
              trace_, this, location.file(), location.line(), strong_refs,
              strong_refs - 1, weak_refs, weak_refs + 1, reason);
    }
    GPR_ASSERT(strong_refs > 0);
#else
    // Avoid unused-parameter warnings for debug-only parameters
    (void)location;
    (void)reason;
#endif
    if (GPR_UNLIKELY(strong_refs == 1)) {
      Orphan();
    }
    // Now drop the weak ref.
    WeakUnref(location, reason);
  }

  GRPC_MUST_USE_RESULT RefCountedPtr<Child> RefIfNonZero() {
    uint64_t prev_ref_pair = refs_.load(std::memory_order_acquire);
    do {
      const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
#ifndef NDEBUG
      const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
      if (trace_ != nullptr) {
        gpr_log(GPR_INFO, "%s:%p ref_if_non_zero %d -> %d (weak_refs=%d)",
                trace_, this, strong_refs, strong_refs + 1, weak_refs);
      }
#endif
      if (strong_refs == 0) return nullptr;
    } while (!refs_.compare_exchange_weak(
        prev_ref_pair, prev_ref_pair + MakeRefPair(1, 0),
        std::memory_order_acq_rel, std::memory_order_acquire));
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  GRPC_MUST_USE_RESULT RefCountedPtr<Child> RefIfNonZero(
      const DebugLocation& location, const char* reason) {
    uint64_t prev_ref_pair = refs_.load(std::memory_order_acquire);
    do {
      const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
#ifndef NDEBUG
      const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
      if (trace_ != nullptr) {
        gpr_log(GPR_INFO,
                "%s:%p %s:%d ref_if_non_zero %d -> %d (weak_refs=%d) %s",
                trace_, this, location.file(), location.line(), strong_refs,
                strong_refs + 1, weak_refs, reason);
      }
#else
      // Avoid unused-parameter warnings for debug-only parameters
      (void)location;
      (void)reason;
#endif
      if (strong_refs == 0) return nullptr;
    } while (!refs_.compare_exchange_weak(
        prev_ref_pair, prev_ref_pair + MakeRefPair(1, 0),
        std::memory_order_acq_rel, std::memory_order_acquire));
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  GRPC_MUST_USE_RESULT WeakRefCountedPtr<Child> WeakRef() {
    IncrementWeakRefCount();
    return WeakRefCountedPtr<Child>(static_cast<Child*>(this));
  }

  GRPC_MUST_USE_RESULT WeakRefCountedPtr<Child> WeakRef(
      const DebugLocation& location, const char* reason) {
    IncrementWeakRefCount(location, reason);
    return WeakRefCountedPtr<Child>(static_cast<Child*>(this));
  }

  void WeakUnref() {
#ifndef NDEBUG
    // Grab a copy of the trace flag before the atomic change, since we
    // will no longer be holding a ref afterwards and therefore can't
    // safely access it, since another thread might free us in the interim.
    const char* trace = trace_;
#endif
    const uint64_t prev_ref_pair =
        refs_.fetch_sub(MakeRefPair(0, 1), std::memory_order_acq_rel);
#ifndef NDEBUG
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
    if (trace != nullptr) {
      gpr_log(GPR_INFO, "%s:%p weak_unref %d -> %d (refs=%d)", trace, this,
              weak_refs, weak_refs - 1, strong_refs);
    }
    GPR_ASSERT(weak_refs > 0);
#endif
    if (GPR_UNLIKELY(prev_ref_pair == MakeRefPair(0, 1))) {
      delete static_cast<Child*>(this);
    }
  }
  void WeakUnref(const DebugLocation& location, const char* reason) {
#ifndef NDEBUG
    // Grab a copy of the trace flag before the atomic change, since we
    // will no longer be holding a ref afterwards and therefore can't
    // safely access it, since another thread might free us in the interim.
    const char* trace = trace_;
#endif
    const uint64_t prev_ref_pair =
        refs_.fetch_sub(MakeRefPair(0, 1), std::memory_order_acq_rel);
#ifndef NDEBUG
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
    if (trace != nullptr) {
      gpr_log(GPR_INFO, "%s:%p %s:%d weak_unref %d -> %d (refs=%d) %s", trace,
              this, location.file(), location.line(), weak_refs, weak_refs - 1,
              strong_refs, reason);
    }
    GPR_ASSERT(weak_refs > 0);
#else
    // Avoid unused-parameter warnings for debug-only parameters
    (void)location;
    (void)reason;
#endif
    if (GPR_UNLIKELY(prev_ref_pair == MakeRefPair(0, 1))) {
      delete static_cast<Child*>(this);
    }
  }

  // Not copyable nor movable.
  DualRefCounted(const DualRefCounted&) = delete;
  DualRefCounted& operator=(const DualRefCounted&) = delete;

 protected:
  // Note: Tracing is a no-op in non-debug builds.
  explicit DualRefCounted(
      const char*
#ifndef NDEBUG
          // Leave unnamed if NDEBUG to avoid unused parameter warning
          trace
#endif
      = nullptr,
      int32_t initial_refcount = 1)
      :
#ifndef NDEBUG
        trace_(trace),
#endif
        refs_(MakeRefPair(initial_refcount, 0)) {
  }

 private:
  // Allow RefCountedPtr<> to access IncrementRefCount().
  template <typename T>
  friend class RefCountedPtr;
  // Allow WeakRefCountedPtr<> to access IncrementWeakRefCount().
  template <typename T>
  friend class WeakRefCountedPtr;

  // First 32 bits are strong refs, next 32 bits are weak refs.
  static uint64_t MakeRefPair(uint32_t strong, uint32_t weak) {
    return (static_cast<uint64_t>(strong) << 32) + static_cast<int64_t>(weak);
  }
  static uint32_t GetStrongRefs(uint64_t ref_pair) {
    return static_cast<uint32_t>(ref_pair >> 32);
  }
  static uint32_t GetWeakRefs(uint64_t ref_pair) {
    return static_cast<uint32_t>(ref_pair & 0xffffffffu);
  }

  void IncrementRefCount() {
#ifndef NDEBUG
    const uint64_t prev_ref_pair =
        refs_.fetch_add(MakeRefPair(1, 0), std::memory_order_relaxed);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    GPR_ASSERT(strong_refs != 0);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p ref %d -> %d; (weak_refs=%d)", trace_, this,
              strong_refs, strong_refs + 1, weak_refs);
    }
#else
    refs_.fetch_add(MakeRefPair(1, 0), std::memory_order_relaxed);
#endif
  }
  void IncrementRefCount(const DebugLocation& location, const char* reason) {
#ifndef NDEBUG
    const uint64_t prev_ref_pair =
        refs_.fetch_add(MakeRefPair(1, 0), std::memory_order_relaxed);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    GPR_ASSERT(strong_refs != 0);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p %s:%d ref %d -> %d (weak_refs=%d) %s", trace_,
              this, location.file(), location.line(), strong_refs,
              strong_refs + 1, weak_refs, reason);
    }
#else
    // Use conditionally-important parameters
    (void)location;
    (void)reason;
    refs_.fetch_add(MakeRefPair(1, 0), std::memory_order_relaxed);
#endif
  }

  void IncrementWeakRefCount() {
#ifndef NDEBUG
    const uint64_t prev_ref_pair =
        refs_.fetch_add(MakeRefPair(0, 1), std::memory_order_relaxed);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p weak_ref %d -> %d; (refs=%d)", trace_, this,
              weak_refs, weak_refs + 1, strong_refs);
    }
#else
    refs_.fetch_add(MakeRefPair(0, 1), std::memory_order_relaxed);
#endif
  }
  void IncrementWeakRefCount(const DebugLocation& location,
                             const char* reason) {
#ifndef NDEBUG
    const uint64_t prev_ref_pair =
        refs_.fetch_add(MakeRefPair(0, 1), std::memory_order_relaxed);
    const uint32_t strong_refs = GetStrongRefs(prev_ref_pair);
    const uint32_t weak_refs = GetWeakRefs(prev_ref_pair);
    if (trace_ != nullptr) {
      gpr_log(GPR_INFO, "%s:%p %s:%d weak_ref %d -> %d (refs=%d) %s", trace_,
              this, location.file(), location.line(), weak_refs, weak_refs + 1,
              strong_refs, reason);
    }
#else
    // Use conditionally-important parameters
    (void)location;
    (void)reason;
    refs_.fetch_add(MakeRefPair(0, 1), std::memory_order_relaxed);
#endif
  }

#ifndef NDEBUG
  const char* trace_;
#endif
  std::atomic<uint64_t> refs_{0};
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_DUAL_REF_COUNTED_H
