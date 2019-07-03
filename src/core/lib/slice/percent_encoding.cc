/*
 *
 * Copyright 2016 gRPC authors.
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
                                    const uint8_t* unreserved_bytes) {
  return ((unreserved_bytes[c / 8] >> (c % 8)) & 1) != 0;
}

grpc_slice grpc_percent_encode_slice(const grpc_slice& slice,
                                     const uint8_t* unreserved_bytes) {
  static const uint8_t hex[] = "0123456789ABCDEF";

  // first pass: count the number of bytes needed to output this string
  size_t output_length = 0;
  const uint8_t* slice_start = GRPC_SLICE_START_PTR(slice);
  const uint8_t* slice_end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* p;
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
  grpc_slice out = GRPC_SLICE_MALLOC(output_length);
  uint8_t* q = GRPC_SLICE_START_PTR(out);
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

static bool valid_hex(const uint8_t* p, const uint8_t* end) {
  if (p >= end) return false;
  return (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
         (*p >= 'A' && *p <= 'F');
}

static uint8_t dehex(uint8_t c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  GPR_UNREACHABLE_CODE(return 255);
}

bool grpc_strict_percent_decode_slice(const grpc_slice& slice_in,
                                      const uint8_t* unreserved_bytes,
                                      grpc_slice* slice_out) {
  const uint8_t* p = GRPC_SLICE_START_PTR(slice_in);
  const uint8_t* in_end = GRPC_SLICE_END_PTR(slice_in);
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
  *slice_out = GRPC_SLICE_MALLOC(out_length);
  uint8_t* q = GRPC_SLICE_START_PTR(*slice_out);
  while (p != in_end) {
    if (*p == '%') {
      *q++ = static_cast<uint8_t>(dehex(p[1]) << 4) | (dehex(p[2]));
      p += 3;
    } else {
      *q++ = *p++;
    }
  }
  GPR_ASSERT(q == GRPC_SLICE_END_PTR(*slice_out));
  return true;
}

grpc_slice grpc_permissive_percent_decode_slice(const grpc_slice& slice_in) {
  const uint8_t* p = GRPC_SLICE_START_PTR(slice_in);
  const uint8_t* in_end = GRPC_SLICE_END_PTR(slice_in);
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
  grpc_slice out = GRPC_SLICE_MALLOC(out_length);
  uint8_t* q = GRPC_SLICE_START_PTR(out);
  while (p != in_end) {
    if (*p == '%') {
      if (!valid_hex(p + 1, in_end) || !valid_hex(p + 2, in_end)) {
        *q++ = *p++;
      } else {
        *q++ = static_cast<uint8_t>(dehex(p[1]) << 4) | (dehex(p[2]));
        p += 3;
      }
    } else {
      *q++ = *p++;
    }
  }
  GPR_ASSERT(q == GRPC_SLICE_END_PTR(out));
  return out;
}
