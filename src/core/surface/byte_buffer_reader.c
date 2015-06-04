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

void grpc_byte_buffer_reader_init(grpc_byte_buffer_reader *reader,
                                  grpc_byte_buffer *buffer) {
  reader->buffer = buffer;
  switch (buffer->type) {
    case GRPC_BB_SLICE_BUFFER:
      reader->current.index = 0;
  }
}

void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader *reader) {
  /* no-op: the user is responsible for memory deallocation.
   * Other cleanup operations would go here if needed. */
}

int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader *reader,
                                 gpr_slice *slice) {
  grpc_byte_buffer *buffer = reader->buffer;
  gpr_slice_buffer *slice_buffer;
  switch (buffer->type) {
    case GRPC_BB_SLICE_BUFFER:
      slice_buffer = &buffer->data.slice_buffer;
      if (reader->current.index < slice_buffer->count) {
        *slice = gpr_slice_ref(slice_buffer->slices[reader->current.index]);
        reader->current.index += 1;
        return 1;
      } else {
        return 0;
      }
      break;
  }
  return 0;
}
