/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_IMPL_CODEGEN_GPR_TYPES_H
#define GRPC_IMPL_CODEGEN_GPR_TYPES_H

#include <grpc/impl/codegen/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The clocks we support. */
typedef enum {
  /* Monotonic clock. Epoch undefined. Always moves forwards. */
  GPR_CLOCK_MONOTONIC = 0,
  /* Realtime clock. May jump forwards or backwards. Settable by
     the system administrator. Has its epoch at 0:00:00 UTC 1 Jan 1970. */
  GPR_CLOCK_REALTIME,
  /* CPU cycle time obtained by rdtsc instruction on x86 platforms. Epoch
     undefined. Degrades to GPR_CLOCK_REALTIME on other platforms. */
  GPR_CLOCK_PRECISE,
  /* Unmeasurable clock type: no base, created by taking the difference
     between two times */
  GPR_TIMESPAN
} gpr_clock_type;

/* Analogous to struct timespec. On some machines, absolute times may be in
 * local time. */
typedef struct gpr_timespec {
  int64_t tv_sec;
  int32_t tv_nsec;
  /** Against which clock was this time measured? (or GPR_TIMESPAN if
      this is a relative time meaure) */
  gpr_clock_type clock_type;
} gpr_timespec;

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

#define GRPC_SLICE_BUFFER_INLINE_ELEMENTS 8

/* Represents an expandable array of slices, to be interpreted as a
   single item. */
typedef struct {
  /* slices in the array */
  gpr_slice *slices;
  /* the number of slices in the array */
  size_t count;
  /* the number of slices allocated in the array */
  size_t capacity;
  /* the combined length of all slices in the array */
  size_t length;
  /* inlined elements to avoid allocations */
  gpr_slice inlined[GRPC_SLICE_BUFFER_INLINE_ELEMENTS];
} gpr_slice_buffer;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_GPR_TYPES_H */
