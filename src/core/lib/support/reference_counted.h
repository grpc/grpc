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

#ifndef GRPC_CORE_LIB_SUPPORT_REFERENCE_COUNTED_H
#define GRPC_CORE_LIB_SUPPORT_REFERENCE_COUNTED_H

#include <grpc/support/sync.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/support/debug_location.h"
#include "src/core/lib/support/memory.h"

namespace grpc_core {

// A base class for reference-counted objects.
// New objects should be created via New() and start with a refcount of 1.
// When the refcount reaches 0, the object will be deleted via Delete().
class ReferenceCounted {
 public:
  void Ref() { gpr_ref(&refs_); }

  void Ref(const DebugLocation& location, const char* reason) {
    if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
      gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
      gpr_log(GPR_DEBUG, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_flag_->name(), this, location.file(), location.line(),
              old_refs, old_refs + 1, reason);
    }
    Ref();
  }

  void Unref() {
    if (gpr_unref(&refs_)) {
      Delete(this);
    }
  }

  void Unref(const DebugLocation& location, const char* reason) {
    if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
      gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
      gpr_log(GPR_DEBUG, "%s:%p %s:%d unref %" PRIdPTR " -> %" PRIdPTR " %s",
              trace_flag_->name(), this, location.file(), location.line(),
              old_refs, old_refs - 1, reason);
    }
    Unref();
  }

  // Not copyable nor movable.
  ReferenceCounted(const ReferenceCounted&) = delete;
  ReferenceCounted& operator=(const ReferenceCounted&) = delete;

 protected:
  // Allow Delete() to access destructor.
  template <typename T>
  friend void Delete(T*);

  ReferenceCounted() : ReferenceCounted(nullptr) {}

  explicit ReferenceCounted(TraceFlag* trace_flag) : trace_flag_(trace_flag) {
    gpr_ref_init(&refs_, 1);
  }

  virtual ~ReferenceCounted() {}

 private:
  TraceFlag* trace_flag_ = nullptr;
  gpr_refcount refs_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SUPPORT_REFERENCE_COUNTED_H */
