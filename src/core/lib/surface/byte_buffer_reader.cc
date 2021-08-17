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

#include <grpc/support/port_platform.h>

#include <grpc/byte_buffer_reader.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"

int grpc_byte_buffer_reader_init(grpc_byte_buffer_reader* reader,
                                 grpc_byte_buffer* buffer) {
  reader->buffer_in = buffer;
  switch (reader->buffer_in->type) {
    case GRPC_BB_RAW:
      reader->buffer_out = reader->buffer_in;
      reader->current.index = 0;
      break;
  }
  return 1;
}

void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader* reader) {
  reader->buffer_out = nullptr;
}

int grpc_byte_buffer_reader_peek(grpc_byte_buffer_reader* reader,
                                 grpc_slice** slice) {
  switch (reader->buffer_in->type) {
    case GRPC_BB_RAW: {
      grpc_slice_buffer* slice_buffer;
      slice_buffer = &reader->buffer_out->data.raw.slice_buffer;
      if (reader->current.index < slice_buffer->count) {
        *slice = &slice_buffer->slices[reader->current.index];
        reader->current.index += 1;
        return 1;
      }
      break;
    }
  }
  return 0;
}

int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader* reader,
                                 grpc_slice* slice) {
  switch (reader->buffer_in->type) {
    case GRPC_BB_RAW: {
      grpc_slice_buffer* slice_buffer;
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

grpc_slice grpc_byte_buffer_reader_readall(grpc_byte_buffer_reader* reader) {
  grpc_slice in_slice;
  size_t bytes_read = 0;
  const size_t input_size = grpc_byte_buffer_length(reader->buffer_out);
  grpc_slice out_slice = GRPC_SLICE_MALLOC(input_size);
  uint8_t* const outbuf = GRPC_SLICE_START_PTR(out_slice); /* just an alias */

  grpc_core::ExecCtx exec_ctx;
  while (grpc_byte_buffer_reader_next(reader, &in_slice) != 0) {
    const size_t slice_length = GRPC_SLICE_LENGTH(in_slice);
    memcpy(&(outbuf[bytes_read]), GRPC_SLICE_START_PTR(in_slice), slice_length);
    bytes_read += slice_length;
    grpc_slice_unref_internal(in_slice);
    GPR_ASSERT(bytes_read <= input_size);
  }

  return out_slice;
}
