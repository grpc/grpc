/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_IMPL_CODEGEN_SLICE_H
#define GRPC_IMPL_CODEGEN_SLICE_H

#include <grpc/impl/codegen/sync.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Slice API

   A slice represents a contiguous reference counted array of bytes.
   It is cheap to take references to a slice, and it is cheap to create a
   slice pointing to a subset of another slice.

   The data-structure for slices is exposed here to allow non-gpr code to
   build slices from whatever data they have available.

   When defining interfaces that handle slices, care should be taken to define
   reference ownership semantics (who should call unref?) and mutability
   constraints (is the callee allowed to modify the slice?) */

/* Reference count container for gpr_slice. Contains function pointers to
   increment and decrement reference counts. Implementations should cleanup
   when the reference count drops to zero.
   Typically client code should not touch this, and use gpr_slice_malloc,
   gpr_slice_new, or gpr_slice_new_with_len instead. */
typedef struct gpr_slice_refcount {
  void (*ref)(void *);
  void (*unref)(void *);
} gpr_slice_refcount;

#define GPR_SLICE_INLINED_SIZE (sizeof(size_t) + sizeof(uint8_t *) - 1)

/* A gpr_slice s, if initialized, represents the byte range
   s.bytes[0..s.length-1].

   It can have an associated ref count which has a destruction routine to be run
   when the ref count reaches zero (see gpr_slice_new() and grp_slice_unref()).
   Multiple gpr_slice values may share a ref count.

   If the slice does not have a refcount, it represents an inlined small piece
   of data that is copied by value. */
typedef struct gpr_slice {
  struct gpr_slice_refcount *refcount;
  union {
    struct {
      uint8_t *bytes;
      size_t length;
    } refcounted;
    struct {
      uint8_t length;
      uint8_t bytes[GPR_SLICE_INLINED_SIZE];
    } inlined;
  } data;
} gpr_slice;

#define GPR_SLICE_START_PTR(slice)                  \
  ((slice).refcount ? (slice).data.refcounted.bytes \
                    : (slice).data.inlined.bytes)
#define GPR_SLICE_LENGTH(slice)                      \
  ((slice).refcount ? (slice).data.refcounted.length \
                    : (slice).data.inlined.length)
#define GPR_SLICE_SET_LENGTH(slice, newlen)                               \
  ((slice).refcount ? ((slice).data.refcounted.length = (size_t)(newlen)) \
                    : ((slice).data.inlined.length = (uint8_t)(newlen)))
#define GPR_SLICE_END_PTR(slice) \
  GPR_SLICE_START_PTR(slice) + GPR_SLICE_LENGTH(slice)
#define GPR_SLICE_IS_EMPTY(slice) (GPR_SLICE_LENGTH(slice) == 0)

/* Increment the refcount of s. Requires slice is initialized.
   Returns s. */
GPRAPI gpr_slice gpr_slice_ref(gpr_slice s);

/* Decrement the ref count of s.  If the ref count of s reaches zero, all
   slices sharing the ref count are destroyed, and considered no longer
   initialized.  If s is ultimately derived from a call to gpr_slice_new(start,
   len, dest) where dest!=NULL , then (*dest)(start) is called, else if s is
   ultimately derived from a call to gpr_slice_new_with_len(start, len, dest)
   where dest!=NULL , then (*dest)(start, len).  Requires s initialized.  */
GPRAPI void gpr_slice_unref(gpr_slice s);

/* Create a slice pointing at some data. Calls malloc to allocate a refcount
   for the object, and arranges that destroy will be called with the pointer
   passed in at destruction. */
GPRAPI gpr_slice gpr_slice_new(void *p, size_t len, void (*destroy)(void *));

/* Equivalent to gpr_slice_new, but with a two argument destroy function that
   also takes the slice length. */
GPRAPI gpr_slice gpr_slice_new_with_len(void *p, size_t len,
                                        void (*destroy)(void *, size_t));

/* Equivalent to gpr_slice_new(malloc(len), len, free), but saves one malloc()
   call.
   Aborts if malloc() fails. */
GPRAPI gpr_slice gpr_slice_malloc(size_t length);

/* Create a slice by copying a string.
   Does not preserve null terminators.
   Equivalent to:
     size_t len = strlen(source);
     gpr_slice slice = gpr_slice_malloc(len);
     memcpy(slice->data, source, len); */
GPRAPI gpr_slice gpr_slice_from_copied_string(const char *source);

/* Create a slice by copying a buffer.
   Equivalent to:
     gpr_slice slice = gpr_slice_malloc(len);
     memcpy(slice->data, source, len); */
GPRAPI gpr_slice gpr_slice_from_copied_buffer(const char *source, size_t len);

/* Create a slice pointing to constant memory */
GPRAPI gpr_slice gpr_slice_from_static_string(const char *source);

/* Return a result slice derived from s, which shares a ref count with s, where
   result.data==s.data+begin, and result.length==end-begin.
   The ref count of s is increased by one.
   Requires s initialized, begin <= end, begin <= s.length, and
   end <= source->length. */
GPRAPI gpr_slice gpr_slice_sub(gpr_slice s, size_t begin, size_t end);

/* The same as gpr_slice_sub, but without altering the ref count */
GPRAPI gpr_slice gpr_slice_sub_no_ref(gpr_slice s, size_t begin, size_t end);

/* Splits s into two: modifies s to be s[0:split], and returns a new slice,
   sharing a refcount with s, that contains s[split:s.length].
   Requires s intialized, split <= s.length */
GPRAPI gpr_slice gpr_slice_split_tail(gpr_slice *s, size_t split);

/* Splits s into two: modifies s to be s[split:s.length], and returns a new
   slice, sharing a refcount with s, that contains s[0:split].
   Requires s intialized, split <= s.length */
GPRAPI gpr_slice gpr_slice_split_head(gpr_slice *s, size_t split);

GPRAPI gpr_slice gpr_empty_slice(void);

/* Returns <0 if a < b, ==0 if a == b, >0 if a > b
   The order is arbitrary, and is not guaranteed to be stable across different
   versions of the API. */
GPRAPI int gpr_slice_cmp(gpr_slice a, gpr_slice b);
GPRAPI int gpr_slice_str_cmp(gpr_slice a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_SLICE_H */
