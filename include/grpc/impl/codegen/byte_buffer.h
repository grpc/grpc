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

#ifndef GRPC_IMPL_CODEGEN_BYTE_BUFFER_H
#define GRPC_IMPL_CODEGEN_BYTE_BUFFER_H

#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/slice_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GRPC_BB_RAW
  /* Future types may include GRPC_BB_PROTOBUF, etc. */
} grpc_byte_buffer_type;

struct grpc_byte_buffer {
  void *reserved;
  grpc_byte_buffer_type type;
  union {
    struct {
      void *reserved[8];
    } reserved;
    struct {
      grpc_compression_algorithm compression;
      gpr_slice_buffer slice_buffer;
    } raw;
  } data;
};
typedef struct grpc_byte_buffer grpc_byte_buffer;

/** Returns a RAW byte buffer instance over the given slices (up to \a nslices).
 *
 * Increases the reference count for all \a slices processed. The user is
 * responsible for invoking grpc_byte_buffer_destroy on the returned instance.*/
GRPCAPI grpc_byte_buffer *grpc_raw_byte_buffer_create(gpr_slice *slices,
                                                      size_t nslices);

/** Returns a *compressed* RAW byte buffer instance over the given slices (up to
 * \a nslices). The \a compression argument defines the compression algorithm
 * used to generate the data in \a slices.
 *
 * Increases the reference count for all \a slices processed. The user is
 * responsible for invoking grpc_byte_buffer_destroy on the returned instance.*/
GRPCAPI grpc_byte_buffer *grpc_raw_compressed_byte_buffer_create(
    gpr_slice *slices, size_t nslices, grpc_compression_algorithm compression);

/** Copies input byte buffer \a bb.
 *
 * Increases the reference count of all the source slices. The user is
 * responsible for calling grpc_byte_buffer_destroy over the returned copy. */
GRPCAPI grpc_byte_buffer *grpc_byte_buffer_copy(grpc_byte_buffer *bb);

/** Returns the size of the given byte buffer, in bytes. */
GRPCAPI size_t grpc_byte_buffer_length(grpc_byte_buffer *bb);

/** Destroys \a byte_buffer deallocating all its memory. */
GRPCAPI void grpc_byte_buffer_destroy(grpc_byte_buffer *byte_buffer);

/** Reader for byte buffers. Iterates over slices in the byte buffer */
struct grpc_byte_buffer_reader;
typedef struct grpc_byte_buffer_reader grpc_byte_buffer_reader;

/** Initialize \a reader to read over \a buffer */
GRPCAPI void grpc_byte_buffer_reader_init(grpc_byte_buffer_reader *reader,
                                          grpc_byte_buffer *buffer);

/** Cleanup and destroy \a reader */
GRPCAPI void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader *reader);

/** Updates \a slice with the next piece of data from from \a reader and returns
 * 1. Returns 0 at the end of the stream. Caller is responsible for calling
 * gpr_slice_unref on the result. */
GRPCAPI int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader *reader,
                                         gpr_slice *slice);

/** Merge all data from \a reader into single slice */
GRPCAPI gpr_slice
grpc_byte_buffer_reader_readall(grpc_byte_buffer_reader *reader);

/** Returns a RAW byte buffer instance from the output of \a reader. */
GRPCAPI grpc_byte_buffer *grpc_raw_byte_buffer_from_reader(
    grpc_byte_buffer_reader *reader);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_BYTE_BUFFER_H */
