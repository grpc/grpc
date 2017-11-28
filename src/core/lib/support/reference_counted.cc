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

#include "src/core/lib/support/reference_counted.h"

#include <grpc/support/log.h>

#include "src/core/lib/support/memory.h"

namespace grpc_core {

void ReferenceCounted::Ref(const DebugLocation& location, const char* reason) {
  if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
    gpr_log(GPR_DEBUG, "%s:%p %s:%d ref %" PRIdPTR " -> %" PRIdPTR " %s",
            trace_flag_->name(), this, location.file(), location.line(),
            old_refs, old_refs + 1, reason);
  }
  Ref();
}

void ReferenceCounted::Ref() { gpr_ref(&refs_); }

bool ReferenceCounted::Unref(const DebugLocation& location,
                             const char* reason) {
  if (location.Log() && trace_flag_ != nullptr && trace_flag_->enabled()) {
    gpr_atm old_refs = gpr_atm_no_barrier_load(&refs_.count);
    gpr_log(GPR_DEBUG, "%s:%p %s:%d unref %" PRIdPTR " -> %" PRIdPTR " %s",
            trace_flag_->name(), this, location.file(), location.line(),
            old_refs, old_refs - 1, reason);
  }
  return Unref();
}

bool ReferenceCounted::Unref() {
  if (gpr_unref(&refs_)) {
    Delete(this);
    return true;
  }
  return false;
}

}  // namespace grpc_core
