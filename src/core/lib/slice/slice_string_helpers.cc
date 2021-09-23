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

#include "src/core/lib/slice/slice_string_helpers.h"

#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/slice/slice_internal.h"

char* grpc_dump_slice(const grpc_slice& s, uint32_t flags) {
  return gpr_dump(reinterpret_cast<const char*> GRPC_SLICE_START_PTR(s),
                  GRPC_SLICE_LENGTH(s), flags);
}

grpc_slice grpc_dump_slice_to_slice(const grpc_slice& s, uint32_t flags) {
  size_t len;
  grpc_core::UniquePtr<char> ptr(
      gpr_dump_return_len(reinterpret_cast<const char*> GRPC_SLICE_START_PTR(s),
                          GRPC_SLICE_LENGTH(s), flags, &len));
  return grpc_slice_from_moved_buffer(std::move(ptr), len);
}

/** Finds the initial (\a begin) and final (\a end) offsets of the next
 * substring from \a str + \a read_offset until the next \a sep or the end of \a
 * str.
 *
 * Returns 1 and updates \a begin and \a end. Returns 0 otherwise. */
static int slice_find_separator_offset(const grpc_slice str, const char* sep,
                                       const size_t read_offset, size_t* begin,
                                       size_t* end) {
  size_t i;
  const uint8_t* str_ptr = GRPC_SLICE_START_PTR(str) + read_offset;
  const size_t str_len = GRPC_SLICE_LENGTH(str) - read_offset;
  const size_t sep_len = strlen(sep);
  if (str_len < sep_len) {
    return 0;
  }

  for (i = 0; i <= str_len - sep_len; i++) {
    if (memcmp(str_ptr + i, sep, sep_len) == 0) {
      *begin = read_offset;
      *end = read_offset + i;
      return 1;
    }
  }
  return 0;
}

static void skip_leading_trailing_spaces(const uint8_t* str_buffer,
                                         size_t* begin, size_t* end) {
  while (*begin < *end && str_buffer[*begin] == ' ') {
    (*begin)++;
  }
  while (*begin < *end && str_buffer[*end - 1] == ' ') {
    (*end)--;
  }
}

bool grpc_parse_slice_to_uint32(grpc_slice str, uint32_t* result) {
  return gpr_parse_bytes_to_uint32(
             reinterpret_cast<const char*> GRPC_SLICE_START_PTR(str),
             GRPC_SLICE_LENGTH(str), result) != 0;
}
