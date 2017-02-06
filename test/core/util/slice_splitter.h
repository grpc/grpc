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

#ifndef GRPC_TEST_CORE_UTIL_SLICE_SPLITTER_H
#define GRPC_TEST_CORE_UTIL_SLICE_SPLITTER_H

/* utility function to split/merge slices together to help create test
   cases */

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>

typedef enum {
  /* merge all input slices into a single slice */
  GRPC_SLICE_SPLIT_MERGE_ALL,
  /* leave slices as is */
  GRPC_SLICE_SPLIT_IDENTITY,
  /* split slices into one byte chunks */
  GRPC_SLICE_SPLIT_ONE_BYTE
} grpc_slice_split_mode;

/* allocates *dst_slices; caller must unref all slices in dst_slices then free
   it */
void grpc_split_slices(grpc_slice_split_mode mode, grpc_slice *src_slices,
                       size_t src_slice_count, grpc_slice **dst_slices,
                       size_t *dst_slice_count);

void grpc_split_slices_to_buffer(grpc_slice_split_mode mode,
                                 grpc_slice *src_slices, size_t src_slice_count,
                                 grpc_slice_buffer *dst);
void grpc_split_slice_buffer(grpc_slice_split_mode mode, grpc_slice_buffer *src,
                             grpc_slice_buffer *dst);

grpc_slice grpc_slice_merge(grpc_slice *slices, size_t nslices);

const char *grpc_slice_split_mode_name(grpc_slice_split_mode mode);

#endif /* GRPC_TEST_CORE_UTIL_SLICE_SPLITTER_H */
