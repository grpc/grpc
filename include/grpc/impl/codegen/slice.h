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

#include <stddef.h>
#include <stdint.h>

#include <grpc/impl/codegen/exec_ctx_fwd.h>
#include <grpc/impl/codegen/gpr_slice.h>

/* Slice API

   A slice represents a contiguous reference counted array of bytes.
   It is cheap to take references to a slice, and it is cheap to create a
   slice pointing to a subset of another slice.

   The data-structure for slices is exposed here to allow non-gpr code to
   build slices from whatever data they have available.

   When defining interfaces that handle slices, care should be taken to define
   reference ownership semantics (who should call unref?) and mutability
   constraints (is the callee allowed to modify the slice?) */

/* Reference count container for grpc_slice. Contains function pointers to
   increment and decrement reference counts. Implementations should cleanup
   when the reference count drops to zero.
   Typically client code should not touch this, and use grpc_slice_malloc,
   grpc_slice_new, or grpc_slice_new_with_len instead. */
typedef struct grpc_slice_refcount {
  void (*ref)(void *);
  void (*unref)(grpc_exec_ctx *exec_ctx, void *);
} grpc_slice_refcount;

#define GRPC_SLICE_INLINED_SIZE (sizeof(size_t) + sizeof(uint8_t *) - 1)

/* A grpc_slice s, if initialized, represents the byte range
   s.bytes[0..s.length-1].

   It can have an associated ref count which has a destruction routine to be run
   when the ref count reaches zero (see grpc_slice_new() and grp_slice_unref()).
   Multiple grpc_slice values may share a ref count.

   If the slice does not have a refcount, it represents an inlined small piece
   of data that is copied by value. */
typedef struct grpc_slice {
  struct grpc_slice_refcount *refcount;
  union {
    struct {
      uint8_t *bytes;
      size_t length;
    } refcounted;
    struct {
      uint8_t length;
      uint8_t bytes[GRPC_SLICE_INLINED_SIZE];
    } inlined;
  } data;
} grpc_slice;

#define GRPC_SLICE_BUFFER_INLINE_ELEMENTS 8

/* Represents an expandable array of slices, to be interpreted as a
   single item. */
typedef struct {
  /* slices in the array */
  grpc_slice *slices;
  /* the number of slices in the array */
  size_t count;
  /* the number of slices allocated in the array */
  size_t capacity;
  /* the combined length of all slices in the array */
  size_t length;
  /* inlined elements to avoid allocations */
  grpc_slice inlined[GRPC_SLICE_BUFFER_INLINE_ELEMENTS];
} grpc_slice_buffer;

#define GRPC_SLICE_START_PTR(slice)                 \
  ((slice).refcount ? (slice).data.refcounted.bytes \
                    : (slice).data.inlined.bytes)
#define GRPC_SLICE_LENGTH(slice)                     \
  ((slice).refcount ? (slice).data.refcounted.length \
                    : (slice).data.inlined.length)
#define GRPC_SLICE_SET_LENGTH(slice, newlen)                              \
  ((slice).refcount ? ((slice).data.refcounted.length = (size_t)(newlen)) \
                    : ((slice).data.inlined.length = (uint8_t)(newlen)))
#define GRPC_SLICE_END_PTR(slice) \
  GRPC_SLICE_START_PTR(slice) + GRPC_SLICE_LENGTH(slice)
#define GRPC_SLICE_IS_EMPTY(slice) (GRPC_SLICE_LENGTH(slice) == 0)

#ifdef GRPC_ALLOW_GPR_SLICE_FUNCTIONS

/* Duplicate GPR_* definitions */
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
  GRPC_SLICE_START_PTR(slice) + GRPC_SLICE_LENGTH(slice)
#define GPR_SLICE_IS_EMPTY(slice) (GRPC_SLICE_LENGTH(slice) == 0)

#endif /* GRPC_ALLOW_GPR_SLICE_FUNCTIONS */

#endif /* GRPC_IMPL_CODEGEN_SLICE_H */
