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

#include <ruby/ruby.h>

#include "rb_byte_buffer.h"

#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>

grpc_byte_buffer* grpc_rb_s_to_byte_buffer(char* string, size_t length) {
  grpc_slice slice = grpc_slice_from_copied_buffer(string, length);
  grpc_byte_buffer* buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  return buffer;
}

VALUE grpc_rb_byte_buffer_to_s(grpc_byte_buffer* buffer) {
  VALUE rb_string;
  grpc_byte_buffer_reader reader;
  grpc_slice next;
  if (buffer == NULL) {
    return Qnil;
  }
  rb_string = rb_str_buf_new(grpc_byte_buffer_length(buffer));
  if (!grpc_byte_buffer_reader_init(&reader, buffer)) {
    rb_raise(rb_eRuntimeError, "Error initializing byte buffer reader.");
    return Qnil;
  }
  while (grpc_byte_buffer_reader_next(&reader, &next) != 0) {
    rb_str_cat(rb_string, (const char*)GRPC_SLICE_START_PTR(next),
               GRPC_SLICE_LENGTH(next));
    grpc_slice_unref(next);
  }
  grpc_byte_buffer_reader_destroy(&reader);
  return rb_string;
}

VALUE grpc_rb_slice_to_ruby_string(grpc_slice slice) {
  if (GRPC_SLICE_START_PTR(slice) == NULL) {
    rb_raise(rb_eRuntimeError,
             "attempt to convert uninitialized grpc_slice to ruby string");
  }
  return rb_str_new((char*)GRPC_SLICE_START_PTR(slice),
                    GRPC_SLICE_LENGTH(slice));
}
