/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/support/percent_encoding.h"

#include <grpc/support/log.h>

static bool is_unreserved_character(uint8_t c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}

gpr_slice gpr_percent_encode_slice(gpr_slice slice) {
  static const uint8_t hex[] = "0123456789ABCDEF";

  // first pass: count the number of bytes needed to output this string
  size_t output_length = 0;
  const uint8_t *slice_start = GPR_SLICE_START_PTR(slice);
  const uint8_t *slice_end = GPR_SLICE_END_PTR(slice);
  const uint8_t *p;
  bool any_reserved_bytes = false;
  for (p = slice_start; p < slice_end; p++) {
    bool unres = is_unreserved_character(*p);
    output_length += unres ? 1 : 3;
    any_reserved_bytes |= !unres;
  }
  // no unreserved bytes: return the string unmodified
  if (!any_reserved_bytes) {
    return gpr_slice_ref(slice);
  }
  // second pass: actually encode
  gpr_slice out = gpr_slice_malloc(output_length);
  uint8_t *q = GPR_SLICE_START_PTR(out);
  for (p = slice_start; p < slice_end; p++) {
    if (is_unreserved_character(*p)) {
      *q++ = *p;
    } else {
      *q++ = '%';
      *q++ = hex[*p >> 4];
      *q++ = hex[*p & 15];
    }
  }
  GPR_ASSERT(q == GPR_SLICE_END_PTR(out));
  return out;
}

static bool valid_hex(const uint8_t *p, const uint8_t *end) {
  if (p == end) return false;
  return (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
         (*p >= 'A' && *p <= 'F');
}

static uint8_t dehex(uint8_t c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  GPR_UNREACHABLE_CODE(return 255);
}

bool gpr_percent_decode_slice(gpr_slice slice_in, gpr_slice *slice_out) {
  const uint8_t *p = GPR_SLICE_START_PTR(slice_in);
  const uint8_t *in_end = GPR_SLICE_END_PTR(slice_in);
  size_t out_length = 0;
  bool any_percent_encoded_stuff = false;
  while (p != in_end) {
    if (*p == '%') {
      if (!valid_hex(++p, in_end)) return false;
      if (!valid_hex(++p, in_end)) return false;
      p++;
      any_percent_encoded_stuff = true;
      out_length++;
    } else {
      p++;
      out_length++;
    }
  }
  if (!any_percent_encoded_stuff) {
    *slice_out = gpr_slice_ref(slice_in);
    return true;
  }
  p = GPR_SLICE_START_PTR(slice_in);
  *slice_out = gpr_slice_malloc(out_length);
  uint8_t *q = GPR_SLICE_START_PTR(*slice_out);
  while (p != in_end) {
    if (*p == '%') {
      *q++ = (uint8_t)(dehex(p[1]) << 4) | (dehex(p[2]));
      p += 3;
    } else {
      *q++ = *p++;
    }
  }
  GPR_ASSERT(q == GPR_SLICE_END_PTR(*slice_out));
  return true;
}
