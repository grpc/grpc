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

#include <cstdint>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/slice/slice_internal.h"

#if __cplusplus > 201103l
#define GRPC_PCTENCODE_CONSTEXPR_FN constexpr
#define GRPC_PCTENCODE_CONSTEXPR_VALUE constexpr
#else
#define GRPC_PCTENCODE_CONSTEXPR_FN
#define GRPC_PCTENCODE_CONSTEXPR_VALUE const
#endif

namespace grpc_core {

namespace {
class UrlTable : public BitSet<256> {
 public:
  GRPC_PCTENCODE_CONSTEXPR_FN UrlTable() {
    for (int i = 'a'; i <= 'z'; i++) set(i);
    for (int i = 'A'; i <= 'Z'; i++) set(i);
    for (int i = '0'; i <= '9'; i++) set(i);
    set('-');
    set('_');
    set('.');
    set('~');
  }
};

static GRPC_PCTENCODE_CONSTEXPR_VALUE UrlTable g_url_table;

class CompatibleTable : public BitSet<256> {
 public:
  GRPC_PCTENCODE_CONSTEXPR_FN CompatibleTable() {
    for (int i = 32; i <= 126; i++) {
      if (i == '%') continue;
      set(i);
    }
  }
};

static GRPC_PCTENCODE_CONSTEXPR_VALUE CompatibleTable g_compatible_table;

// Map PercentEncodingType to a lookup table of legal symbols for that encoding.
const BitSet<256>& LookupTableForPercentEncodingType(PercentEncodingType type) {
  switch (type) {
    case PercentEncodingType::URL:
      return g_url_table;
    case PercentEncodingType::Compatible:
      return g_compatible_table;
  }
  // Crash if a bad PercentEncodingType was passed in.
  GPR_UNREACHABLE_CODE(abort());
}
}  // namespace

grpc_slice PercentEncodeSlice(const grpc_slice& slice,
                              PercentEncodingType type) {
  static const uint8_t hex[] = "0123456789ABCDEF";

  const BitSet<256>& lut = LookupTableForPercentEncodingType(type);

  // first pass: count the number of bytes needed to output this string
  size_t output_length = 0;
  const uint8_t* slice_start = GRPC_SLICE_START_PTR(slice);
  const uint8_t* slice_end = GRPC_SLICE_END_PTR(slice);
  const uint8_t* p;
  bool any_reserved_bytes = false;
  for (p = slice_start; p < slice_end; p++) {
    bool unres = lut.is_set(*p);
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
    if (lut.is_set(*p)) {
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

absl::optional<grpc_slice> PercentDecodeSlice(const grpc_slice& slice_in,
                                              PercentEncodingType type) {
  const uint8_t* p = GRPC_SLICE_START_PTR(slice_in);
  const uint8_t* in_end = GRPC_SLICE_END_PTR(slice_in);
  size_t out_length = 0;
  bool any_percent_encoded_stuff = false;
  const BitSet<256>& lut = LookupTableForPercentEncodingType(type);
  while (p != in_end) {
    if (*p == '%') {
      if (!valid_hex(++p, in_end)) return {};
      if (!valid_hex(++p, in_end)) return {};
      p++;
      out_length++;
      any_percent_encoded_stuff = true;
    } else if (lut.is_set(*p)) {
      p++;
      out_length++;
    } else {
      return {};
    }
  }
  if (!any_percent_encoded_stuff) {
    return grpc_slice_ref_internal(slice_in);
  }
  p = GRPC_SLICE_START_PTR(slice_in);
  grpc_slice slice_out = GRPC_SLICE_MALLOC(out_length);
  uint8_t* q = GRPC_SLICE_START_PTR(slice_out);
  while (p != in_end) {
    if (*p == '%') {
      *q++ = static_cast<uint8_t>(dehex(p[1]) << 4) | (dehex(p[2]));
      p += 3;
    } else {
      *q++ = *p++;
    }
  }
  GPR_ASSERT(q == GRPC_SLICE_END_PTR(slice_out));
  return slice_out;
}

grpc_slice PermissivePercentDecodeSlice(const grpc_slice& slice_in) {
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

}  // namespace grpc_core
