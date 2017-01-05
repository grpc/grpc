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

#include "src/core/lib/slice/percent_encoding.h"

#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"

const uint8_t grpc_url_percent_encoding_unreserved_bytes[256 / 8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xff, 0x03, 0xfe, 0xff, 0xff,
    0x87, 0xfe, 0xff, 0xff, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t grpc_compatible_percent_encoding_unreserved_bytes[256 / 8] = {
    0x00, 0x00, 0x00, 0x00, 0xdf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static bool is_unreserved_character(uint8_t c,
                                    const uint8_t *unreserved_bytes) {
  return ((unreserved_bytes[c / 8] >> (c % 8)) & 1) != 0;
}

grpc_slice grpc_percent_encode_slice(grpc_slice slice,
                                     const uint8_t *unreserved_bytes) {
  static const uint8_t hex[] = "0123456789ABCDEF";

  // first pass: count the number of bytes needed to output this string
  size_t output_length = 0;
  const uint8_t *slice_start = GRPC_SLICE_START_PTR(slice);
  const uint8_t *slice_end = GRPC_SLICE_END_PTR(slice);
  const uint8_t *p;
  bool any_reserved_bytes = false;
  for (p = slice_start; p < slice_end; p++) {
    bool unres = is_unreserved_character(*p, unreserved_bytes);
    output_length += unres ? 1 : 3;
    any_reserved_bytes |= !unres;
  }
  // no unreserved bytes: return the string unmodified
  if (!any_reserved_bytes) {
    return grpc_slice_ref_internal(slice);
  }
  // second pass: actually encode
  grpc_slice out = grpc_slice_malloc(output_length);
  uint8_t *q = GRPC_SLICE_START_PTR(out);
  for (p = slice_start; p < slice_end; p++) {
    if (is_unreserved_character(*p, unreserved_bytes)) {
      *q++ = *p;
    } else {
      *q++ = '%';
      *q++ = hex[*p >> 4];
      *q++ = hex[*p & 15];
    }
  }
  GPR_ASSERT(q == GRPC_SLICE_END_PTR(out));
  return out;
}

static bool valid_hex(const uint8_t *p, const uint8_t *end) {
  if (p >= end) return false;
  return (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
         (*p >= 'A' && *p <= 'F');
}

static uint8_t dehex(uint8_t c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  GPR_UNREACHABLE_CODE(return 255);
}

bool grpc_strict_percent_decode_slice(grpc_slice slice_in,
                                      const uint8_t *unreserved_bytes,
                                      grpc_slice *slice_out) {
  const uint8_t *p = GRPC_SLICE_START_PTR(slice_in);
  const uint8_t *in_end = GRPC_SLICE_END_PTR(slice_in);
  size_t out_length = 0;
  bool any_percent_encoded_stuff = false;
  while (p != in_end) {
    if (*p == '%') {
      if (!valid_hex(++p, in_end)) return false;
      if (!valid_hex(++p, in_end)) return false;
      p++;
      out_length++;
      any_percent_encoded_stuff = true;
    } else if (is_unreserved_character(*p, unreserved_bytes)) {
      p++;
      out_length++;
    } else {
      return false;
    }
  }
  if (!any_percent_encoded_stuff) {
    *slice_out = grpc_slice_ref_internal(slice_in);
    return true;
  }
  p = GRPC_SLICE_START_PTR(slice_in);
  *slice_out = grpc_slice_malloc(out_length);
  uint8_t *q = GRPC_SLICE_START_PTR(*slice_out);
  while (p != in_end) {
    if (*p == '%') {
      *q++ = (uint8_t)(dehex(p[1]) << 4) | (dehex(p[2]));
      p += 3;
    } else {
      *q++ = *p++;
    }
  }
  GPR_ASSERT(q == GRPC_SLICE_END_PTR(*slice_out));
  return true;
}

grpc_slice grpc_permissive_percent_decode_slice(grpc_slice slice_in) {
  const uint8_t *p = GRPC_SLICE_START_PTR(slice_in);
  const uint8_t *in_end = GRPC_SLICE_END_PTR(slice_in);
  size_t out_length = 0;
  bool any_percent_encoded_stuff = false;
  while (p != in_end) {
    if (*p == '%') {
      if (!valid_hex(p + 1, in_end) || !valid_hex(p + 2, in_end)) {
        p++;
        out_length++;
      } else {
        p += 3;
        out_length++;
        any_percent_encoded_stuff = true;
      }
    } else {
      p++;
      out_length++;
    }
  }
  if (!any_percent_encoded_stuff) {
    return grpc_slice_ref_internal(slice_in);
  }
  p = GRPC_SLICE_START_PTR(slice_in);
  grpc_slice out = grpc_slice_malloc(out_length);
  uint8_t *q = GRPC_SLICE_START_PTR(out);
  while (p != in_end) {
    if (*p == '%') {
      if (!valid_hex(p + 1, in_end) || !valid_hex(p + 2, in_end)) {
        *q++ = *p++;
      } else {
        *q++ = (uint8_t)(dehex(p[1]) << 4) | (dehex(p[2]));
        p += 3;
      }
    } else {
      *q++ = *p++;
    }
  }
  GPR_ASSERT(q == GRPC_SLICE_END_PTR(out));
  return out;
}
