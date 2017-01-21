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
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/slice/slice_internal.h"

static int is_compressed(grpc_byte_buffer *buffer) {
  switch (buffer->type) {
    case GRPC_BB_RAW:
      if (buffer->data.raw.compression == GRPC_COMPRESS_NONE) {
        return 0 /* GPR_FALSE */;
      }
      break;
  }
  return 1 /* GPR_TRUE */;
}

int grpc_byte_buffer_reader_init(grpc_byte_buffer_reader *reader,
                                 grpc_byte_buffer *buffer) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_buffer decompressed_slices_buffer;
  reader->buffer_in = buffer;
  switch (reader->buffer_in->type) {
    case GRPC_BB_RAW:
      grpc_slice_buffer_init(&decompressed_slices_buffer);
      if (is_compressed(reader->buffer_in)) {
        if (grpc_msg_decompress(&exec_ctx,
                                reader->buffer_in->data.raw.compression,
                                &reader->buffer_in->data.raw.slice_buffer,
                                &decompressed_slices_buffer) == 0) {
          gpr_log(GPR_ERROR,
                  "Unexpected error decompressing data for algorithm with enum "
                  "value '%d'.",
                  reader->buffer_in->data.raw.compression);
          memset(reader, 0, sizeof(*reader));
          return 0;
        } else { /* all fine */
          reader->buffer_out =
              grpc_raw_byte_buffer_create(decompressed_slices_buffer.slices,
                                          decompressed_slices_buffer.count);
        }
        grpc_slice_buffer_destroy_internal(&exec_ctx,
                                           &decompressed_slices_buffer);
      } else { /* not compressed, use the input buffer as output */
        reader->buffer_out = reader->buffer_in;
      }
      reader->current.index = 0;
      break;
  }
  grpc_exec_ctx_finish(&exec_ctx);
  return 1;
}

void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader *reader) {
  switch (reader->buffer_in->type) {
    case GRPC_BB_RAW:
      /* keeping the same if-else structure as in the init function */
      if (is_compressed(reader->buffer_in)) {
        grpc_byte_buffer_destroy(reader->buffer_out);
      }
      break;
  }
}

int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader *reader,
                                 grpc_slice *slice) {
  switch (reader->buffer_in->type) {
    case GRPC_BB_RAW: {
      grpc_slice_buffer *slice_buffer;
      slice_buffer = &reader->buffer_out->data.raw.slice_buffer;
      if (reader->current.index < slice_buffer->count) {
        *slice = grpc_slice_ref_internal(
            slice_buffer->slices[reader->current.index]);
        reader->current.index += 1;
        return 1;
      }
      break;
    }
  }
  return 0;
}

grpc_slice grpc_byte_buffer_reader_readall(grpc_byte_buffer_reader *reader) {
  grpc_slice in_slice;
  size_t bytes_read = 0;
  const size_t input_size = grpc_byte_buffer_length(reader->buffer_out);
  grpc_slice out_slice = grpc_slice_malloc(input_size);
  uint8_t *const outbuf = GRPC_SLICE_START_PTR(out_slice); /* just an alias */

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  while (grpc_byte_buffer_reader_next(reader, &in_slice) != 0) {
    const size_t slice_length = GRPC_SLICE_LENGTH(in_slice);
    memcpy(&(outbuf[bytes_read]), GRPC_SLICE_START_PTR(in_slice), slice_length);
    bytes_read += slice_length;
    grpc_slice_unref_internal(&exec_ctx, in_slice);
    GPR_ASSERT(bytes_read <= input_size);
  }
  grpc_exec_ctx_finish(&exec_ctx);
  return out_slice;
}
