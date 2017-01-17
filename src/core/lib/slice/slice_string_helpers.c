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

#include "src/core/lib/slice/slice_string_helpers.h"

#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/string.h"

char *grpc_dump_slice(grpc_slice s, uint32_t flags) {
  return gpr_dump((const char *)GRPC_SLICE_START_PTR(s), GRPC_SLICE_LENGTH(s),
                  flags);
}

/** Finds the initial (\a begin) and final (\a end) offsets of the next
 * substring from \a str + \a read_offset until the next \a sep or the end of \a
 * str.
 *
 * Returns 1 and updates \a begin and \a end. Returns 0 otherwise. */
static int slice_find_separator_offset(const grpc_slice str, const char *sep,
                                       const size_t read_offset, size_t *begin,
                                       size_t *end) {
  size_t i;
  const uint8_t *str_ptr = GRPC_SLICE_START_PTR(str) + read_offset;
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

void grpc_slice_split(grpc_slice str, const char *sep, grpc_slice_buffer *dst) {
  const size_t sep_len = strlen(sep);
  size_t begin, end;

  GPR_ASSERT(sep_len > 0);

  if (slice_find_separator_offset(str, sep, 0, &begin, &end) != 0) {
    do {
      grpc_slice_buffer_add_indexed(dst, grpc_slice_sub(str, begin, end));
    } while (slice_find_separator_offset(str, sep, end + sep_len, &begin,
                                         &end) != 0);
    grpc_slice_buffer_add_indexed(
        dst, grpc_slice_sub(str, end + sep_len, GRPC_SLICE_LENGTH(str)));
  } else { /* no sep found, add whole input */
    grpc_slice_buffer_add_indexed(dst, grpc_slice_ref_internal(str));
  }
}
