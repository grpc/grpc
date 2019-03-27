/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SLICE_SLICE_INTERNAL_H
#define GRPC_CORE_LIB_SLICE_SLICE_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <string.h>

#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/transport/static_metadata.h"

extern uint32_t static_metadata_hash_values[GRPC_STATIC_MDSTR_COUNT];
extern uint32_t g_hash_seed;
namespace grpc_core {

class SliceRefcount {
 public:
  enum class Type { STATIC, INTERNED, REGULAR };
  typedef void (*DestroyerFn)(void*);

  SliceRefcount() = default;
  SliceRefcount(SliceRefcount::Type type, RefCount* ref,
                DestroyerFn destroyer_fn, void* destroyer_arg,
                grpc_slice_refcount* sub)
      : ref_(ref),
        ref_type_(type),
        sub_refcount_(sub),
        dest_fn_(destroyer_fn),
        destroy_fn_arg_(destroyer_arg) {}
  // Initializer for static refcounts.
  SliceRefcount(grpc_slice_refcount* sub, Type type)
      : ref_type_(type), sub_refcount_(sub) {}

  Type getType() const { return ref_type_; }

  int Eq(const grpc_slice& a, const grpc_slice& b);

  uint32_t Hash(const grpc_slice& slice);
  void Ref() {
    if (!ref_) return;
    ref_->RefNonZero();
  }
  void Unref() {
    if (!ref_) return;
    if (ref_->Unref()) {
      dest_fn_(destroy_fn_arg_);
    }
  }

  bool Validate() const { return ref_ != nullptr; }

 private:
  friend struct ::grpc_slice_refcount;

  RefCount* ref_ = nullptr;
  const Type ref_type_ = Type::REGULAR;
  grpc_slice_refcount* sub_refcount_ = nullptr;
  DestroyerFn dest_fn_ = nullptr;
  void* destroy_fn_arg_ = nullptr;
};

}  // namespace grpc_core

struct grpc_slice_refcount {
  grpc_slice_refcount() { impl_.sub_refcount_ = this; }
  grpc_slice_refcount(grpc_core::SliceRefcount::Type type,
                      grpc_core::RefCount* ref,
                      grpc_core::SliceRefcount::DestroyerFn destroyer_fn,
                      void* destroyer_arg, grpc_slice_refcount* sub)
      : impl_(type, ref, destroyer_fn, destroyer_arg, sub) {}
  grpc_slice_refcount(grpc_slice_refcount* sub,
                      grpc_core::SliceRefcount::Type type)
      : impl_(sub, type) {}

  grpc_slice_refcount* sub_refcount() const { return impl_.sub_refcount_; }

  grpc_core::SliceRefcount impl_;
};

namespace grpc_core {

struct InternedSliceRefcount {
  static void Destroy(void* arg) {
    static_cast<InternedSliceRefcount*>(arg)->DestroyInstance();
  }

  InternedSliceRefcount(size_t length, uint32_t hash,
                        InternedSliceRefcount* bucket_next)
      : base(grpc_core::SliceRefcount::Type::INTERNED, &refcnt, Destroy, this,
             &sub),
        sub(grpc_core::SliceRefcount::Type::REGULAR, &refcnt, Destroy, this,
            &sub),
        length(length),
        hash(hash),
        bucket_next(bucket_next) {}

  void DestroyInstance();

  grpc_slice_refcount base;
  grpc_slice_refcount sub;
  const size_t length;
  RefCount refcnt;
  const uint32_t hash;
  InternedSliceRefcount* bucket_next;
};

inline int SliceRefcount::Eq(const grpc_slice& a, const grpc_slice& b) {
  switch (ref_type_) {
    case Type::STATIC:
      return GRPC_STATIC_METADATA_INDEX(a) == GRPC_STATIC_METADATA_INDEX(b);
    case Type::INTERNED:
      return a.refcount == b.refcount;
    case Type::REGULAR:
      break;
  }
  if (GRPC_SLICE_LENGTH(a) != GRPC_SLICE_LENGTH(b)) return false;
  if (GRPC_SLICE_LENGTH(a) == 0) return true;
  return 0 == memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                     GRPC_SLICE_LENGTH(a));
}

inline uint32_t SliceRefcount::Hash(const grpc_slice& slice) {
  switch (ref_type_) {
    case Type::STATIC:
      return ::static_metadata_hash_values[GRPC_STATIC_METADATA_INDEX(slice)];
    case Type::INTERNED:
      return reinterpret_cast<InternedSliceRefcount*>(slice.refcount)->hash;
    case Type::REGULAR:
      break;
  }
  return gpr_murmur_hash3(GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice),
                          g_hash_seed);
}

}  // namespace grpc_core

inline grpc_slice grpc_slice_ref_internal(const grpc_slice& slice) {
  if (slice.refcount) {
    slice.refcount->impl_.Ref();
  }
  return slice;
}

inline void grpc_slice_unref_internal(const grpc_slice& slice) {
  if (slice.refcount) {
    slice.refcount->impl_.Unref();
  }
}

void grpc_slice_buffer_reset_and_unref_internal(grpc_slice_buffer* sb);
void grpc_slice_buffer_partial_unref_internal(grpc_slice_buffer* sb,
                                              size_t idx);
void grpc_slice_buffer_destroy_internal(grpc_slice_buffer* sb);

/* Check if a slice is interned */
bool grpc_slice_is_interned(const grpc_slice& slice);

void grpc_slice_intern_init(void);
void grpc_slice_intern_shutdown(void);
void grpc_test_only_set_slice_hash_seed(uint32_t key);
// if slice matches a static slice, returns the static slice
// otherwise returns the passed in slice (without reffing it)
// used for surface boundaries where we might receive an un-interned static
// string
grpc_slice grpc_slice_maybe_static_intern(grpc_slice slice,
                                          bool* returned_slice_is_different);
uint32_t grpc_static_slice_hash(grpc_slice s);
int grpc_static_slice_eq(grpc_slice a, grpc_slice b);

// Returns the memory used by this slice, not counting the slice structure
// itself. This means that inlined and slices from static strings will return
// 0. All other slices will return the size of the allocated chars.
size_t grpc_slice_memory_usage(grpc_slice s);

#endif /* GRPC_CORE_LIB_SLICE_SLICE_INTERNAL_H */
