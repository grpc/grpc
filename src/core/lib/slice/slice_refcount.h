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

#ifndef GRPC_CORE_LIB_SLICE_SLICE_REFCOUNT_H
#define GRPC_CORE_LIB_SLICE_SLICE_REFCOUNT_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <grpc/slice.h>

#include "src/core/lib/slice/slice_refcount_base.h"

namespace grpc_core {

extern uint32_t g_hash_seed;

}  // namespace grpc_core

inline const grpc_slice& grpc_slice_ref_internal(const grpc_slice& slice) {
  if (reinterpret_cast<uintptr_t>(slice.refcount) > 1) {
    slice.refcount->Ref();
  }
  return slice;
}

inline void grpc_slice_unref_internal(const grpc_slice& slice) {
  if (reinterpret_cast<uintptr_t>(slice.refcount) > 1) {
    slice.refcount->Unref();
  }
}

#endif /* GRPC_CORE_LIB_SLICE_SLICE_REFCOUNT_H */
