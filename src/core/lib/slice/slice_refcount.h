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

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/slice/slice_refcount_base.h"

namespace grpc_core {

extern uint32_t g_hash_seed;
extern grpc_slice_refcount kNoopRefcount;

// TODO(ctiller): when this is removed, remove the std::atomic* in
// grpc_slice_refcount and just put it there directly.
struct InternedSliceRefcount {
  static void Destroy(void* arg) {
    auto* rc = static_cast<InternedSliceRefcount*>(arg);
    rc->~InternedSliceRefcount();
    gpr_free(rc);
  }

  InternedSliceRefcount(size_t length, uint32_t hash,
                        InternedSliceRefcount* bucket_next)
      : base(grpc_slice_refcount::Type::INTERNED, &refcnt, Destroy, this, &sub),
        sub(grpc_slice_refcount::Type::REGULAR, &refcnt, Destroy, this, &sub),
        length(length),
        hash(hash),
        bucket_next(bucket_next) {}

  ~InternedSliceRefcount();

  grpc_slice_refcount base;
  grpc_slice_refcount sub;
  const size_t length;
  std::atomic<size_t> refcnt{1};
  const uint32_t hash;
  InternedSliceRefcount* bucket_next;
};

}  // namespace grpc_core

inline size_t grpc_refcounted_slice_length(const grpc_slice& slice) {
  GPR_DEBUG_ASSERT(slice.refcount != nullptr);
  return slice.data.refcounted.length;
}

inline const uint8_t* grpc_refcounted_slice_data(const grpc_slice& slice) {
  GPR_DEBUG_ASSERT(slice.refcount != nullptr);
  return slice.data.refcounted.bytes;
}

inline int grpc_slice_refcount::Eq(const grpc_slice& a, const grpc_slice& b) {
  GPR_DEBUG_ASSERT(a.refcount != nullptr);
  GPR_DEBUG_ASSERT(a.refcount == this);
  switch (ref_type_) {
    case Type::INTERNED:
      return a.refcount == b.refcount;
    case Type::NOP:
    case Type::REGULAR:
      break;
  }
  if (grpc_refcounted_slice_length(a) != GRPC_SLICE_LENGTH(b)) return false;
  if (grpc_refcounted_slice_length(a) == 0) return true;
  return 0 == memcmp(grpc_refcounted_slice_data(a), GRPC_SLICE_START_PTR(b),
                     grpc_refcounted_slice_length(a));
}

inline uint32_t grpc_slice_refcount::Hash(const grpc_slice& slice) {
  GPR_DEBUG_ASSERT(slice.refcount != nullptr);
  GPR_DEBUG_ASSERT(slice.refcount == this);
  switch (ref_type_) {
    case Type::INTERNED:
      return reinterpret_cast<grpc_core::InternedSliceRefcount*>(slice.refcount)
          ->hash;
    case Type::NOP:
    case Type::REGULAR:
      break;
  }
  return gpr_murmur_hash3(grpc_refcounted_slice_data(slice),
                          grpc_refcounted_slice_length(slice),
                          grpc_core::g_hash_seed);
}

inline const grpc_slice& grpc_slice_ref_internal(const grpc_slice& slice) {
  if (slice.refcount) {
    slice.refcount->Ref();
  }
  return slice;
}

inline void grpc_slice_unref_internal(const grpc_slice& slice) {
  if (slice.refcount) {
    slice.refcount->Unref();
  }
}

#endif /* GRPC_CORE_LIB_SLICE_SLICE_REFCOUNT_H */
