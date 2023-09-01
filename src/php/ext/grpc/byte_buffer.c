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

#include <php.h>

// The include file must place here under <php.h> for fixing compile error.
// See: https://github.com/grpc/grpc/pull/12360#issuecomment-326484589
#include "byte_buffer.h"

#include <grpc/byte_buffer_reader.h>

grpc_byte_buffer *string_to_byte_buffer(char *string, size_t length) {
  grpc_slice slice = grpc_slice_from_copied_buffer(string, length);
  grpc_byte_buffer *buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  return buffer;
}

zend_string* byte_buffer_to_zend_string(grpc_byte_buffer *buffer) {
  grpc_byte_buffer_reader reader;
  if (buffer == NULL || !grpc_byte_buffer_reader_init(&reader, buffer)) {
    /* TODO(dgq): distinguish between the error cases. */
    return NULL;
  }

  const size_t length = grpc_byte_buffer_length(reader.buffer_out);
  zend_string* zstr = zend_string_alloc(length, 0);

  char* buf = ZSTR_VAL(zstr);
  grpc_slice next;
  while (grpc_byte_buffer_reader_next(&reader, &next) != 0) {
    const size_t next_len = GRPC_SLICE_LENGTH(next);
    memcpy(buf, GRPC_SLICE_START_PTR(next), next_len);
    buf += next_len;
    grpc_slice_unref(next);
  }

  *buf = '\0';
  
  return zstr;
}
