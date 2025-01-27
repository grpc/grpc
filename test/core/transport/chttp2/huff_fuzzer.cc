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

#include <grpc/slice.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/decode_huff.h"
#include "src/core/ext/transport/chttp2/transport/huffsyms.h"
#include "src/core/util/dump_args.h"

namespace grpc_core {
namespace {

std::string ToString(std::optional<std::vector<uint8_t>> s) {
  if (s == std::nullopt) return "nullopt";
  return absl::StrCat("{", absl::StrJoin(*s, ","), "}");
}

void EncodeDecodeRoundTrips(std::vector<uint8_t> buffer) {
  grpc_slice uncompressed = grpc_slice_from_copied_buffer(
      reinterpret_cast<const char*>(buffer.data()), buffer.size());
  grpc_slice compressed = grpc_chttp2_huffman_compress(uncompressed);
  std::vector<uint8_t> uncompressed_again;
  auto add = [&uncompressed_again](uint8_t c) {
    uncompressed_again.push_back(c);
  };
  EXPECT_TRUE(HuffDecoder<decltype(add)>(add, GRPC_SLICE_START_PTR(compressed),
                                         GRPC_SLICE_END_PTR(compressed))
                  .Run());
  EXPECT_EQ(buffer, uncompressed_again);
  grpc_slice_unref(uncompressed);
  grpc_slice_unref(compressed);
}
FUZZ_TEST(HuffTest, EncodeDecodeRoundTrips);

std::optional<std::vector<uint8_t>> DecodeHuffSlow(const uint8_t* begin,
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
      return std::nullopt;
    }
    bits >>= 1;
    bits_left--;
  }
  return out;
}

std::optional<std::vector<uint8_t>> DecodeHuffFast(const uint8_t* begin,
                                                   const uint8_t* end) {
  std::vector<uint8_t> v;
  auto f = [&](uint8_t x) { v.push_back(x); };
  if (!HuffDecoder<decltype(f)>(f, begin, end).Run()) {
    return std::nullopt;
  }
  return v;
}

void DifferentialOptimizedTest(std::vector<uint8_t> buffer) {
  auto slow = DecodeHuffSlow(buffer.data(), buffer.data() + buffer.size());
  auto fast = DecodeHuffFast(buffer.data(), buffer.data() + buffer.size());
  EXPECT_EQ(fast, slow) << GRPC_DUMP_ARGS(ToString(buffer), ToString(slow),
                                          ToString(fast));
}
FUZZ_TEST(HuffTest, DifferentialOptimizedTest);

}  // namespace
}  // namespace grpc_core
