/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRONSPORT_STREAM_COMPRESSION_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRONSPORT_STREAM_COMPRESSION_H

#include <stdbool.h>

#include <grpc/slice_buffer.h>
#include <zlib.h>

/* Stream compression/decompression context */
typedef struct grpc_stream_compression_context {
  z_stream zs;
  int (*flate)(z_stream *zs, int flush);
} grpc_stream_compression_context;

typedef enum grpc_stream_compression_method {
  GRPC_STREAM_COMPRESSION_COMPRESS,
  GRPC_STREAM_COMPRESSION_DECOMPRESS
} grpc_stream_compression_method;

typedef enum grpc_stream_compression_flush {
  GRPC_STREAM_COMPRESSION_FLUSH_NONE,
  GRPC_STREAM_COMPRESSION_FLUSH_SYNC,
  GRPC_STREAM_COMPRESSION_FLUSH_FINISH
} grpc_stream_compression_flush;

/**
 * Compress bytes provided in \a in with a given context, with an optional flush
 * at the end of compression. Emits at most \a max_output_size compressed bytes
 * into \a out. If all the bytes in input buffer \a in are depleted and \a flush
 * is not GRPC_STREAM_COMPRESSION_FLUSH_NONE, the corresponding flush method is
 * executed. The total number of bytes emitted is outputed in \a output_size.
 */
bool grpc_stream_compress(grpc_stream_compression_context *ctx,
                          grpc_slice_buffer *in, grpc_slice_buffer *out,
                          size_t *output_size, size_t max_output_size,
                          grpc_stream_compression_flush flush);

/**
 * Decompress bytes provided in \a in with a given context. Emits at most \a
 * max_output_size decompressed bytes into \a out. If decompression process
 * reached the end of a gzip stream, \a end_of_context is set to true; otherwise
 * it is set to false. The total number of bytes emitted is outputed in \a
 * output_size.
 */
bool grpc_stream_decompress(grpc_stream_compression_context *ctx,
                            grpc_slice_buffer *in, grpc_slice_buffer *out,
                            size_t *output_size, size_t max_output_size,
                            bool *end_of_context);

/**
 * Creates a stream compression context. \a pending_bytes_buffer is the input
 * buffer for compression/decompression operations. \a method specifies whether
 * the context is for compression or decompression.
 */
grpc_stream_compression_context *grpc_stream_compression_context_create(
    grpc_stream_compression_method method);

/**
 * Destroys a stream compression context.
 */
void grpc_stream_compression_context_destroy(
    grpc_stream_compression_context *ctx);

#endif
