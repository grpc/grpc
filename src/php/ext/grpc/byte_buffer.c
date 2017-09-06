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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/spl/spl_exceptions.h>
#include "php_grpc.h"

#include <string.h>

#include "byte_buffer.h"

#include <grpc/grpc.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/slice.h>

grpc_byte_buffer *string_to_byte_buffer(char *string, size_t length) {
  grpc_slice slice = grpc_slice_from_copied_buffer(string, length);
  grpc_byte_buffer *buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  return buffer;
}

void byte_buffer_to_string(grpc_byte_buffer *buffer, char **out_string,
                           size_t *out_length) {
  grpc_byte_buffer_reader reader;
  if (buffer == NULL || !grpc_byte_buffer_reader_init(&reader, buffer)) {
    /* TODO(dgq): distinguish between the error cases. */
    *out_string = NULL;
    *out_length = 0;
    return;
  }

  grpc_slice slice = grpc_byte_buffer_reader_readall(&reader);
  size_t length = GRPC_SLICE_LENGTH(slice);
  char *string = ecalloc(length + 1, sizeof(char));
  memcpy(string, GRPC_SLICE_START_PTR(slice), length);
  grpc_slice_unref(slice);

  *out_string = string;
  *out_length = length;
}
