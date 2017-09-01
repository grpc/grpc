/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"

grpc_byte_buffer *grpc_raw_byte_buffer_create(grpc_slice *slices,
                                              size_t nslices) {
  return grpc_raw_compressed_byte_buffer_create(slices, nslices,
                                                GRPC_COMPRESS_NONE);
}

grpc_byte_buffer *grpc_raw_compressed_byte_buffer_create(
    grpc_slice *slices, size_t nslices,
    grpc_compression_algorithm compression) {
  size_t i;
  grpc_byte_buffer *bb = gpr_malloc(sizeof(grpc_byte_buffer));
  bb->type = GRPC_BB_RAW;
  bb->data.raw.compression = compression;
  grpc_slice_buffer_init(&bb->data.raw.slice_buffer);
  for (i = 0; i < nslices; i++) {
    grpc_slice_ref_internal(slices[i]);
    grpc_slice_buffer_add(&bb->data.raw.slice_buffer, slices[i]);
  }
  return bb;
}

grpc_byte_buffer *grpc_raw_byte_buffer_from_reader(
    grpc_byte_buffer_reader *reader) {
  grpc_byte_buffer *bb = gpr_malloc(sizeof(grpc_byte_buffer));
  grpc_slice slice;
  bb->type = GRPC_BB_RAW;
  bb->data.raw.compression = GRPC_COMPRESS_NONE;
  grpc_slice_buffer_init(&bb->data.raw.slice_buffer);

  while (grpc_byte_buffer_reader_next(reader, &slice)) {
    grpc_slice_buffer_add(&bb->data.raw.slice_buffer, slice);
  }
  return bb;
}

grpc_byte_buffer *grpc_byte_buffer_copy(grpc_byte_buffer *bb) {
  switch (bb->type) {
    case GRPC_BB_RAW:
      return grpc_raw_compressed_byte_buffer_create(
          bb->data.raw.slice_buffer.slices, bb->data.raw.slice_buffer.count,
          bb->data.raw.compression);
  }
  GPR_UNREACHABLE_CODE(return NULL);
}

void grpc_byte_buffer_destroy(grpc_byte_buffer *bb) {
  if (!bb) return;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  switch (bb->type) {
    case GRPC_BB_RAW:
      grpc_slice_buffer_destroy_internal(&exec_ctx, &bb->data.raw.slice_buffer);
      break;
  }
  gpr_free(bb);
  grpc_exec_ctx_finish(&exec_ctx);
}

size_t grpc_byte_buffer_length(grpc_byte_buffer *bb) {
  switch (bb->type) {
    case GRPC_BB_RAW:
      return bb->data.raw.slice_buffer.length;
  }
  GPR_UNREACHABLE_CODE(return 0);
}
