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

#include <ruby/ruby.h>

#include "rb_grpc_imports.generated.h"
#include "rb_byte_buffer.h"

#include <grpc/grpc.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/slice.h>
#include "rb_grpc.h"

grpc_byte_buffer* grpc_rb_s_to_byte_buffer(char *string, size_t length) {
  grpc_slice slice = grpc_slice_from_copied_buffer(string, length);
  grpc_byte_buffer *buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  return buffer;
}

VALUE grpc_rb_byte_buffer_to_s(grpc_byte_buffer *buffer) {
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
    rb_str_cat(rb_string, (const char *) GRPC_SLICE_START_PTR(next),
               GRPC_SLICE_LENGTH(next));
    grpc_slice_unref(next);
  }
  grpc_byte_buffer_reader_destroy(&reader);
  return rb_string;
}

VALUE grpc_rb_slice_to_ruby_string(grpc_slice slice) {
  if (GRPC_SLICE_START_PTR(slice) == NULL) {
    rb_raise(rb_eRuntimeError, "attempt to convert uninitialized grpc_slice to ruby string");
  }
  return rb_str_new((char*)GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice));
}
