//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/lib/slice/percent_encoding.h"

#include <stdlib.h>

#include <cstdint>
#include <utility>

#include "absl/log/check.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/bitset.h"

namespace grpc_core {

namespace {
class UrlTable : public BitSet<256> {
 public:
  constexpr UrlTable() {
    for (int i = 'a'; i <= 'z'; i++) set(i);
    for (int i = 'A'; i <= 'Z'; i++) set(i);
    for (int i = '0'; i <= '9'; i++) set(i);
    set('-');
    set('_');
    set('.');
    set('~');
  }
};

constexpr UrlTable g_url_table;

class CompatibleTable : public BitSet<256> {
 public:
  constexpr CompatibleTable() {
    for (int i = 32; i <= 126; i++) {
      if (i == '%') continue;
      set(i);
    }
  }
};

constexpr CompatibleTable g_compatible_table;

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

Slice PercentEncodeSlice(Slice slice, PercentEncodingType type) {
  static const uint8_t hex[] = "0123456789ABCDEF";

  const BitSet<256>& lut = LookupTableForPercentEncodingType(type);

  // first pass: count the number of bytes needed to output this string
  size_t output_length = 0;
  bool any_reserved_bytes = false;
  for (uint8_t c : slice) {
    bool unres = lut.is_set(c);
    output_length += unres ? 1 : 3;
    any_reserved_bytes |= !unres;
  }
  // no unreserved bytes: return the string unmodified
  if (!any_reserved_bytes) {
    return slice;
  }
  // second pass: actually encode
  auto out = MutableSlice::CreateUninitialized(output_length);
  uint8_t* q = out.begin();
  for (uint8_t c : slice) {
    if (lut.is_set(c)) {
      *q++ = c;
    } else {
      *q++ = '%';
      *q++ = hex[c >> 4];
      *q++ = hex[c & 15];
    }
  }
  CHECK(q == out.end());
  return Slice(std::move(out));
}

static bool ValidHex(const uint8_t* p, const uint8_t* end) {
  if (p >= end) return false;
  return (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
         (*p >= 'A' && *p <= 'F');
}

static uint8_t DeHex(uint8_t c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  GPR_UNREACHABLE_CODE(return 255);
}

Slice PermissivePercentDecodeSlice(Slice slice_in) {
  bool any_percent_encoded_stuff = false;
  for (uint8_t c : slice_in) {
    if (c == '%') {
      any_percent_encoded_stuff = true;
      break;
    }
  }
  if (!any_percent_encoded_stuff) return slice_in;

  MutableSlice out = slice_in.TakeMutable();
  uint8_t* q = out.begin();
  const uint8_t* p = out.begin();
  const uint8_t* end = out.end();
  while (p != end) {
    if (*p == '%') {
      if (!ValidHex(p + 1, end) || !ValidHex(p + 2, end)) {
        *q++ = *p++;
      } else {
        *q++ = static_cast<uint8_t>(DeHex(p[1]) << 4) | (DeHex(p[2]));
        p += 3;
      }
    } else {
      *q++ = *p++;
    }
  }
  return Slice(out.TakeSubSlice(0, q - out.begin()));
}

}  // namespace grpc_core
