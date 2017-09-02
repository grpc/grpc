/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/compression/stream_compression.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"

#define OUTPUT_BLOCK_SIZE (1024)

static void grpc_stream_compression_pass_through(grpc_slice_buffer *in, grpc_slice_buffer *out, size_t *output_size, size_t max_output_size) {
  if (max_output_size >= in->length) {
    *output_size = in->length;
    grpc_slice_buffer_move_into(in, out);
  } else {
    *output_size = max_output_size;
    grpc_slice_buffer_move_first(in, max_output_size, out);
  }
}

static bool grpc_stream_compress_identity(grpc_stream_compression_context *ctx,
                          grpc_slice_buffer *in, grpc_slice_buffer *out,
                          size_t *output_size, size_t max_output_size,
                          grpc_stream_compression_flush flush) {
  if (ctx == NULL) {
    return false;
  }
  grpc_stream_compression_pass_through(in, out, output_size, max_output_size);
  return true;
}

static bool grpc_stream_decompress_identity(grpc_stream_compression_context *ctx,
                            grpc_slice_buffer *in, grpc_slice_buffer *out,
                            size_t *output_size, size_t max_output_size,
                            bool *end_of_context) {
  if (ctx == NULL) {
    return false;
  }
  grpc_stream_compression_pass_through(in, out, output_size, max_output_size);
  return true;
}

static grpc_stream_compression_context *grpc_stream_compression_context_create_identity(
    grpc_stream_compression_method method) {
  GPR_ASSERT(method == GRPC_STREAM_COMPRESSION_IDENTITY_COMPRESS || method == GRPC_STREAM_COMPRESSION_IDENTITY_DECOMPRESS);
  /* No context needed in this case. Use fake context instead. */
  return (grpc_stream_compression_context *)1;
}

static void grpc_stream_compression_context_destroy_identity(
    grpc_stream_compression_context *ctx) {
  return;
}

const grpc_stream_compression_vtable grpc_stream_compression_identity_vtable = {
  .compress = grpc_stream_compress_identity,
  .decompress = grpc_stream_decompress_identity,
  .context_create = grpc_stream_compression_context_create_identity,
  .context_destroy = grpc_stream_compression_context_destroy_identity
};
