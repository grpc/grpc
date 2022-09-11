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
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"

#include <grpc/slice.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/decode_huff.h"

bool squelch = true;
bool leak_check = true;

std::string ToString(absl::optional<std::vector<uint8_t>> s) {
  if (s == absl::nullopt) return "nullopt";
  return absl::StrCat("{", absl::StrJoin(*s, ","), "}");
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  grpc_slice uncompressed =
      grpc_slice_from_copied_buffer(reinterpret_cast<const char*>(data), size);
  grpc_slice compressed = grpc_chttp2_huffman_compress(uncompressed);
  std::vector<uint8_t> uncompressed_again;
  auto add = [&uncompressed_again](uint8_t c) {
    uncompressed_again.push_back(c);
  };
  auto fail = [&](const char* reason) {
    fprintf(stderr,
            "Failed: %s\nuncompressed: %s\ncompressed: %s\nuncompressed_again: "
            "%s\n",
            reason, ToString(std::vector<uint8_t>(data, data + size)).c_str(),
            ToString(std::vector<uint8_t>(GRPC_SLICE_START_PTR(compressed),
                                          GRPC_SLICE_START_PTR(compressed) +
                                              GRPC_SLICE_LENGTH(compressed)))
                .c_str(),
            ToString(uncompressed_again).c_str());
    abort();
  };
  if (!grpc_core::HuffDecoder<decltype(add)>(add,
                                             GRPC_SLICE_START_PTR(compressed),
                                             GRPC_SLICE_END_PTR(compressed))
           .Run()) {
    fail("decoding");
  }
  if (uncompressed_again.size() != size) {
    fail("size mismatch");
  }
  if (memcmp(uncompressed_again.data(), data, size) != 0) {
    fail("data mismatch");
  }
  grpc_slice_unref(uncompressed);
  grpc_slice_unref(compressed);
  return 0;
}
