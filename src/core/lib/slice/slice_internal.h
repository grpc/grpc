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
#include <grpc/support/alloc.h>

inline grpc_slice grpc_slice_ref_internal(grpc_slice slice) {
  if (slice.refcount) {
    slice.refcount->vtable->ref(slice.refcount);
  }
  return slice;
}

inline void grpc_slice_unref_internal(grpc_slice slice) {
  if (slice.refcount) {
    slice.refcount->vtable->unref(slice.refcount);
  }
}

typedef struct {
  grpc_slice_refcount base;
  gpr_refcount refs;
} malloc_refcount;

inline void malloc_ref(void* p) {
  malloc_refcount* r = static_cast<malloc_refcount*>(p);
  gpr_ref(&r->refs);
}

inline void malloc_unref(void* p) {
  malloc_refcount* r = static_cast<malloc_refcount*>(p);
  if (gpr_unref(&r->refs)) {
    gpr_free(r);
  }
}

static const grpc_slice_refcount_vtable malloc_vtable = {
    malloc_ref, malloc_unref, grpc_slice_default_eq_impl,
    grpc_slice_default_hash_impl};

inline grpc_slice grpc_slice_malloc_large_internal(size_t length) {
  grpc_slice slice;

  /* Memory layout used by the slice created here:

     +-----------+----------------------------------------------------------+
     | refcount  | bytes                                                    |
     +-----------+----------------------------------------------------------+

     refcount is a malloc_refcount
     bytes is an array of bytes of the requested length
     Both parts are placed in the same allocation returned from gpr_malloc */
  malloc_refcount* rc = static_cast<malloc_refcount*>(
      gpr_malloc(sizeof(malloc_refcount) + length));

  /* Initial refcount on rc is 1 - and it's up to the caller to release
     this reference. */
  gpr_ref_init(&rc->refs, 1);

  rc->base.vtable = &malloc_vtable;
  rc->base.sub_refcount = &rc->base;

  /* Build up the slice to be returned. */
  /* The slices refcount points back to the allocated block. */
  slice.refcount = &rc->base;
  /* The data bytes are placed immediately after the refcount struct */
  slice.data.refcounted.bytes = reinterpret_cast<uint8_t*>(rc + 1);
  /* And the length of the block is set to the requested length */
  slice.data.refcounted.length = length;
  return slice;
}

inline grpc_slice grpc_slice_malloc_internal(size_t length) {
  grpc_slice slice;

  if (length > sizeof(slice.data.inlined.bytes)) {
    return grpc_slice_malloc_large(length);
  } else {
    /* small slice: just inline the data */
    slice.refcount = nullptr;
    slice.data.inlined.length = static_cast<uint8_t>(length);
  }
  return slice;
}

inline void grpc_slice_buffer_reset_and_unref_internal(grpc_slice_buffer* sb) {
  size_t i;
  for (i = 0; i < sb->count; i++) {
    grpc_slice_unref_internal(sb->slices[i]);
  }

  sb->count = 0;
  sb->length = 0;
}

inline void grpc_slice_buffer_destroy_internal(grpc_slice_buffer* sb) {
  grpc_slice_buffer_reset_and_unref_internal(sb);
  if (sb->base_slices != sb->inlined) {
    gpr_free(sb->base_slices);
  }
}

/* Check if a slice is interned */
bool grpc_slice_is_interned(grpc_slice slice);

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

#endif /* GRPC_CORE_LIB_SLICE_SLICE_INTERNAL_H */
