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

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

grpc_byte_buffer *grpc_raw_byte_buffer_create(gpr_slice *slices,
                                              size_t nslices) {
  return grpc_raw_compressed_byte_buffer_create(slices, nslices,
                                                GRPC_COMPRESS_NONE);
}

grpc_byte_buffer *grpc_raw_compressed_byte_buffer_create(
    gpr_slice *slices, size_t nslices, grpc_compression_algorithm compression) {
  size_t i;
  grpc_byte_buffer *bb = gpr_malloc(sizeof(grpc_byte_buffer));
  bb->type = GRPC_BB_RAW;
  bb->data.raw.compression = compression;
  gpr_slice_buffer_init(&bb->data.raw.slice_buffer);
  for (i = 0; i < nslices; i++) {
    gpr_slice_ref(slices[i]);
    gpr_slice_buffer_add(&bb->data.raw.slice_buffer, slices[i]);
  }
  return bb;
}

grpc_byte_buffer *grpc_raw_byte_buffer_from_reader(
    grpc_byte_buffer_reader *reader) {
  grpc_byte_buffer *bb = gpr_malloc(sizeof(grpc_byte_buffer));
  gpr_slice slice;
  bb->type = GRPC_BB_RAW;
  bb->data.raw.compression = GRPC_COMPRESS_NONE;
  gpr_slice_buffer_init(&bb->data.raw.slice_buffer);

  while (grpc_byte_buffer_reader_next(reader, &slice)) {
    gpr_slice_buffer_add(&bb->data.raw.slice_buffer, slice);
  }
  return bb;
}

grpc_byte_buffer *grpc_byte_buffer_copy(grpc_byte_buffer *bb) {
  switch (bb->type) {
    case GRPC_BB_RAW:
      return grpc_raw_byte_buffer_create(bb->data.raw.slice_buffer.slices,
                                         bb->data.raw.slice_buffer.count);
  }
  GPR_UNREACHABLE_CODE(return NULL);
}

void grpc_byte_buffer_destroy(grpc_byte_buffer *bb) {
  if (!bb) return;
  switch (bb->type) {
    case GRPC_BB_RAW:
      gpr_slice_buffer_destroy(&bb->data.raw.slice_buffer);
      break;
  }
  gpr_free(bb);
}

size_t grpc_byte_buffer_length(grpc_byte_buffer *bb) {
  switch (bb->type) {
    case GRPC_BB_RAW:
      return bb->data.raw.slice_buffer.length;
  }
  GPR_UNREACHABLE_CODE(return 0);
}
