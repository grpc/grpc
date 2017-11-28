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

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/support/debug_location.h"

namespace grpc_core {

class ReferenceCounted {
 public:
  void Ref();
  void Ref(const DebugLocation& location, const char* reason);

  bool Unref();
  bool Unref(const DebugLocation& location, const char* reason);

  // Not copyable nor movable.
  ReferenceCounted(const ReferenceCounted&) = delete;
  ReferenceCounted& operator=(const ReferenceCounted&) = delete;

 protected:
  // Allow Delete() to access destructor.
  template <typename T>
  friend void Delete(T*);

  explicit ReferenceCounted(TraceFlag* trace_flag) : trace_flag_(trace_flag) {
    gpr_ref_init(&refs_, 1);
  }

  virtual ~ReferenceCounted() {}

 private:
  TraceFlag* trace_flag_;
  gpr_refcount refs_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SUPPORT_REFERENCE_COUNTED_H */
