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

#ifndef GRPC_IMPL_CODEGEN_SLICE_BUFFER_H
#define GRPC_IMPL_CODEGEN_SLICE_BUFFER_H

#include <grpc/impl/codegen/slice.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* initialize a slice buffer */
GPRAPI void gpr_slice_buffer_init(gpr_slice_buffer *sb);
/* destroy a slice buffer - unrefs any held elements */
GPRAPI void gpr_slice_buffer_destroy(gpr_slice_buffer *sb);
/* Add an element to a slice buffer - takes ownership of the slice.
   This function is allowed to concatenate the passed in slice to the end of
   some other slice if desired by the slice buffer. */
GPRAPI void gpr_slice_buffer_add(gpr_slice_buffer *sb, gpr_slice slice);
/* add an element to a slice buffer - takes ownership of the slice and returns
   the index of the slice.
   Guarantees that the slice will not be concatenated at the end of another
   slice (i.e. the data for this slice will begin at the first byte of the
   slice at the returned index in sb->slices)
   The implementation MAY decide to concatenate data at the end of a small
   slice added in this fashion. */
GPRAPI size_t gpr_slice_buffer_add_indexed(gpr_slice_buffer *sb,
                                           gpr_slice slice);
GPRAPI void gpr_slice_buffer_addn(gpr_slice_buffer *sb, gpr_slice *slices,
                                  size_t n);
/* add a very small (less than 8 bytes) amount of data to the end of a slice
   buffer: returns a pointer into which to add the data */
GPRAPI uint8_t *gpr_slice_buffer_tiny_add(gpr_slice_buffer *sb, size_t len);
/* pop the last buffer, but don't unref it */
GPRAPI void gpr_slice_buffer_pop(gpr_slice_buffer *sb);
/* clear a slice buffer, unref all elements */
GPRAPI void gpr_slice_buffer_reset_and_unref(gpr_slice_buffer *sb);
/* swap the contents of two slice buffers */
GPRAPI void gpr_slice_buffer_swap(gpr_slice_buffer *a, gpr_slice_buffer *b);
/* move all of the elements of src into dst */
GPRAPI void gpr_slice_buffer_move_into(gpr_slice_buffer *src,
                                       gpr_slice_buffer *dst);
/* remove n bytes from the end of a slice buffer */
GPRAPI void gpr_slice_buffer_trim_end(gpr_slice_buffer *src, size_t n,
                                      gpr_slice_buffer *garbage);
/* move the first n bytes of src into dst */
GPRAPI void gpr_slice_buffer_move_first(gpr_slice_buffer *src, size_t n,
                                        gpr_slice_buffer *dst);
/* take the first slice in the slice buffer */
GPRAPI gpr_slice gpr_slice_buffer_take_first(gpr_slice_buffer *src);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_SLICE_BUFFER_H */
