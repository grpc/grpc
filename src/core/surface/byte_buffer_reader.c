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

#include <grpc/byte_buffer_reader.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/byte_buffer.h>

#include "src/core/compression/algorithm.h"
#include "src/core/compression/message_compress.h"

void grpc_byte_buffer_reader_init(grpc_byte_buffer_reader *reader,
                                  grpc_byte_buffer *buffer) {
  grpc_compression_algorithm compress_algo;
  gpr_slice_buffer decompressed_slices_buffer;
  reader->buffer_in = buffer;
  switch (buffer->type) {
    case GRPC_BB_COMPRESSED_DEFLATE:
    case GRPC_BB_COMPRESSED_GZIP:
      compress_algo =
          GRPC_COMPRESS_ALGORITHM_FROM_BB_TYPE(reader->buffer_in->type);
      gpr_slice_buffer_init(&decompressed_slices_buffer);
      grpc_msg_decompress(compress_algo, &reader->buffer_in->data.slice_buffer,
                          &decompressed_slices_buffer);
      /* the output buffer is a regular GRPC_BB_SLICE_BUFFER */
      reader->buffer_out = grpc_byte_buffer_create(
          decompressed_slices_buffer.slices,
          decompressed_slices_buffer.count);
      gpr_slice_buffer_destroy(&decompressed_slices_buffer);
    /* fallthrough */
    case GRPC_BB_SLICE_BUFFER:
    case GRPC_BB_COMPRESSED_NONE:
      reader->current.index = 0;
  }
}

void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader *reader) {
  switch (reader->buffer_in->type) {
    case GRPC_BB_COMPRESSED_DEFLATE:
    case GRPC_BB_COMPRESSED_GZIP:
      grpc_byte_buffer_destroy(reader->buffer_out);
      break;
    case GRPC_BB_SLICE_BUFFER:
    case GRPC_BB_COMPRESSED_NONE:
      ; /* no-op */
  }
}

int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader *reader,
                                 gpr_slice *slice) {
  gpr_slice_buffer *slice_buffer;
  grpc_byte_buffer *buffer = NULL;

  /* Pick the right buffer based on the input type */
  switch (reader->buffer_in->type) {
    case GRPC_BB_SLICE_BUFFER:
    case GRPC_BB_COMPRESSED_NONE:
      buffer = reader->buffer_in;
      break;
    case GRPC_BB_COMPRESSED_DEFLATE:
    case GRPC_BB_COMPRESSED_GZIP:
      buffer = reader->buffer_out;
      break;
  }
  GPR_ASSERT(buffer);
  slice_buffer = &buffer->data.slice_buffer;
  if (reader->current.index < slice_buffer->count) {
    *slice = gpr_slice_ref(slice_buffer->slices[reader->current.index]);
    reader->current.index += 1;
    return 1;
  }
  return 0;
}
