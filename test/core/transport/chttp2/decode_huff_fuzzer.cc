// Copyright 2022 gRPC authors.
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

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"

#include "src/core/ext/transport/chttp2/transport/decode_huff.h"
#include "src/core/ext/transport/chttp2/transport/huffsyms.h"

bool squelch = true;
bool leak_check = true;

absl::optional<std::vector<uint8_t>> DecodeHuffSlow(const uint8_t* begin,
                                                    const uint8_t* end) {
  uint64_t bits = 0;
  size_t bits_left = 0u;
  std::vector<uint8_t> out;
  while (true) {
    while (begin != end && bits_left < 30) {
      bits <<= 8;
      bits |= begin[0];
      ++begin;
      bits_left += 8;
    }
    if (bits_left < 5) break;
    bool found = false;
    for (int i = 0; i < GRPC_CHTTP2_NUM_HUFFSYMS; i++) {
      const auto& sym = grpc_chttp2_huffsyms[i];
      if (sym.length > bits_left) continue;
      if (((bits >> (bits_left - sym.length)) & ((1 << sym.length) - 1)) ==
          sym.bits) {
        found = true;
        bits_left -= sym.length;
        if (i == 256) {
          return out;
        }
        out.push_back(i);
        break;
      }
    }
    if (!found) {
      break;
    }
  }
  while (bits_left > 0) {
    if ((bits & 1) == 0) {
      return absl::nullopt;
    }
    bits >>= 1;
    bits_left--;
  }
  return out;
}

std::string ToString(absl::optional<std::vector<uint8_t>> s) {
  if (s == absl::nullopt) return "nullopt";
  return absl::StrCat("{", absl::StrJoin(*s, ","), "}");
}

absl::optional<std::vector<uint8_t>> DecodeHuffFast(const uint8_t* begin,
                                                    const uint8_t* end) {
  std::vector<uint8_t> v;
  auto f = [&](uint8_t x) { v.push_back(x); };
  if (!grpc_core::HuffDecoder<decltype(f)>(f, begin, end).Run()) {
    return absl::nullopt;
  }
  return v;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto slow = DecodeHuffSlow(data, data + size);
  auto fast = DecodeHuffFast(data, data + size);
  if (slow != fast) {
    fprintf(stderr, "MISMATCH:\ninpt: %s\nslow: %s\nfast: %s\n",
            ToString(std::vector<uint8_t>(data, data + size)).c_str(),
            ToString(slow).c_str(), ToString(fast).c_str());
    abort();
  }
  return 0;
}
