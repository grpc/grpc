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

#ifndef GRPC_CORE_LIB_GPRPP_ORPHANABLE_H
#define GRPC_CORE_LIB_GPRPP_ORPHANABLE_H

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <cinttypes>
#include <memory>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

// A base class for orphanable objects, which have one external owner
// but are not necessarily destroyed immediately when the external owner
// gives up ownership.  Instead, the owner calls the object's Orphan()
// method, and the object then takes responsibility for its own cleanup
// and destruction.
class Orphanable {
 public:
  // Gives up ownership of the object.  The implementation must arrange
  // to eventually destroy the object without further interaction from the
  // caller.
  virtual void Orphan() GRPC_ABSTRACT;

  // Not copyable or movable.
  Orphanable(const Orphanable&) = delete;
  Orphanable& operator=(const Orphanable&) = delete;

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  Orphanable() {}
  virtual ~Orphanable() {}
};

template <typename T>
class OrphanableDelete {
 public:
  void operator()(T* p) { p->Orphan(); }
};

template <typename T, typename Deleter = OrphanableDelete<T>>
using OrphanablePtr = std::unique_ptr<T, Deleter>;

template <typename T, typename... Args>
inline OrphanablePtr<T> MakeOrphanable(Args&&... args) {
  return OrphanablePtr<T>(New<T>(std::forward<Args>(args)...));
}

// A type of Orphanable with internal ref-counting.
template <typename Child>
class InternallyRefCounted : public Orphanable {
 public:
  // Not copyable nor movable.
  InternallyRefCounted(const InternallyRefCounted&) = delete;
  InternallyRefCounted& operator=(const InternallyRefCounted&) = delete;

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  // Allow Delete() to access destructor.
  template <typename T>
  friend void Delete(T*);

  // Allow RefCountedPtr<> to access Unref() and IncrementRefCount().
  friend class RefCountedPtr<Child>;

  InternallyRefCounted() { gpr_ref_init(&refs_, 1); }
  virtual ~InternallyRefCounted() {}

  RefCountedPtr<Child> Ref() GRPC_MUST_USE_RESULT {
    IncrementRefCount();
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  void Unref() {
    if (gpr_unref(&refs_)) {
      Delete(static_cast<Child*>(this));
    }
  }

 private:
  void IncrementRefCount() { gpr_ref(&refs_); }

  gpr_refcount refs_;
};

// An alternative version of the InternallyRefCounted base class that
// supports tracing.  This is intended to be used in cases where the
// object will be handled both by idiomatic C++ code using smart
// pointers and legacy code that is manually calling Ref() and Unref().
// Once all of our code is converted to idiomatic C++, we may be able to
// eliminate this class.
template <typename Child>
class InternallyRefCountedWithTracing : public Orphanable {
 public:
  // Not copyable nor movable.
  InternallyRefCountedWithTracing(const InternallyRefCountedWithTracing&) =
      delete;
  InternallyRefCountedWithTracing& operator=(
      const InternallyRefCountedWithTracing&) = delete;

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  // Allow Delete() to access destructor.
  template <typename T>
  friend void Delete(T*);

  // Allow RefCountedPtr<> to access Unref() and IncrementRefCount().
  friend class RefCountedPtr<Child>;

  InternallyRefCountedWithTracing()
      : InternallyRefCountedWithTracing(static_cast<TraceFlag*>(nullptr)) {}

  explicit InternallyRefCountedWithTracing(TraceFlag* trace_flag)
      : trace_flag_(trace_flag) {
    gpr_ref_init(&refs_, 1);
  }

#ifdef NDEBUG
  explicit InternallyRefCountedWithTracing(DebugOnlyTraceFlag* trace_flag)
      : InternallyRefCountedWithTracing() {}
#endif

  virtual ~InternallyRefCountedWithTracing() {}

  RefCountedPtr<Child> Ref() GRPC_MUST_USE_RESULT {
    IncrementRefCount();
    return RefCountedPtr<Child>(static_cast<Child*>(this));
  }

  RefCountedPtr<Child> Ref(const DebugLocation& location,
                           const char* reason) GRPC_MUST_USE_RESULT {
    if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
      gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
      gpr_log(GPR_INFO, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_flag_->name(), this, location.file(), location.line(),
              old_refs, old_refs + 1, reason);
    }
    return Ref();
  }

  // TODO(roth): Once all of our code is converted to C++ and can use
  // RefCountedPtr<> instead of manual ref-counting, make the Unref() methods
  // private, since they will only be used by RefCountedPtr<>, which is a
  // friend of this class.

  void Unref() {
    if (gpr_unref(&refs_)) {
      Delete(static_cast<Child*>(this));
    }
  }

  void Unref(const DebugLocation& location, const char* reason) {
    if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
      gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
      gpr_log(GPR_INFO, "%s:%p %s:%d unref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_flag_->name(), this, location.file(), location.line(),
              old_refs, old_refs - 1, reason);
    }
    Unref();
  }

 private:
  void IncrementRefCount() { gpr_ref(&refs_); }

  TraceFlag* trace_flag_ = nullptr;
  gpr_refcount refs_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_GPRPP_ORPHANABLE_H */
