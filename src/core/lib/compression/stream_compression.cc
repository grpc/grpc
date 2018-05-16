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

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>

#include "src/core/lib/compression/stream_compression.h"
#include "src/core/lib/compression/stream_compression_gzip.h"

extern const grpc_stream_compression_vtable
    grpc_stream_compression_identity_vtable;

bool grpc_stream_compress(grpc_stream_compression_context* ctx,
                          grpc_slice_buffer* in, grpc_slice_buffer* out,
                          size_t* output_size, size_t max_output_size,
                          grpc_stream_compression_flush flush) {
  return ctx->vtable->compress(ctx, in, out, output_size, max_output_size,
                               flush);
}

bool grpc_stream_decompress(grpc_stream_compression_context* ctx,
                            grpc_slice_buffer* in, grpc_slice_buffer* out,
                            size_t* output_size, size_t max_output_size,
                            bool* end_of_context) {
  return ctx->vtable->decompress(ctx, in, out, output_size, max_output_size,
                                 end_of_context);
}

grpc_stream_compression_context* grpc_stream_compression_context_create(
    grpc_stream_compression_method method) {
  switch (method) {
    case GRPC_STREAM_COMPRESSION_IDENTITY_COMPRESS:
    case GRPC_STREAM_COMPRESSION_IDENTITY_DECOMPRESS:
      return grpc_stream_compression_identity_vtable.context_create(method);
    case GRPC_STREAM_COMPRESSION_GZIP_COMPRESS:
    case GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS:
      return grpc_stream_compression_gzip_vtable.context_create(method);
    default:
      gpr_log(GPR_ERROR, "Unknown stream compression method: %d", method);
      return nullptr;
  }
}

void grpc_stream_compression_context_destroy(
    grpc_stream_compression_context* ctx) {
  ctx->vtable->context_destroy(ctx);
}

int grpc_stream_compression_method_parse(
    grpc_slice value, bool is_compress,
    grpc_stream_compression_method* method) {
  if (grpc_slice_eq(value, GRPC_MDSTR_IDENTITY)) {
    *method = is_compress ? GRPC_STREAM_COMPRESSION_IDENTITY_COMPRESS
                          : GRPC_STREAM_COMPRESSION_IDENTITY_DECOMPRESS;
    return 1;
  } else if (grpc_slice_eq(value, GRPC_MDSTR_GZIP)) {
    *method = is_compress ? GRPC_STREAM_COMPRESSION_GZIP_COMPRESS
                          : GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS;
    return 1;
  } else {
    return 0;
  }
}
