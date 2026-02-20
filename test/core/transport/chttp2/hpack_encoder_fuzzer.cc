// Copyright 2026 gRPC authors.
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/transport/chttp2/hpack_encoder_test_helper.h"
#include "gtest/gtest.h"

struct Collector {
  void Encode(const grpc_core::Slice& key, const grpc_core::Slice& value) {
    headers.emplace_back(std::string(key.as_string_view()),
                         std::string(value.as_string_view()));
  }
  template <typename Trait, typename V>
  void Encode(Trait, const V&) {}

  std::vector<std::pair<std::string, std::string>> headers;
};

// Encodes a vector of headers using `RawEncoder` and verifies that the encoded
// headers can be parsed back into the original headers.
void FuzzRawEncoder(bool is_true_binary,
                    std::vector<std::pair<std::string, std::string>> headers) {
  grpc_core::RawEncoder encoder(is_true_binary);
  std::vector<std::pair<std::string, std::string>> added_headers;
  // Size limit is 16KB. Keep track of size to avoid filling up and getting
  // dropped headers which would fail verification.
  size_t current_size = 0;
  // 2KB per key-value pair limit
  constexpr size_t kMaxKeyValueSize = 2 * 1024u;
  constexpr size_t kMaxSize = 1 << 14;

  for (const auto& p : headers) {
    // Prefix key to avoid colliding with known traits that might do validation
    // or normalization (like grpc-timeout).
    std::string key = "user-key-" + p.first;

    // 32 bytes is a conservative estimate for the overhead of encoding a
    // header. It may be smaller in practice.
    size_t kv_size = key.length() + p.second.length() + 32u;
    if (kv_size > kMaxKeyValueSize) continue;
    if (current_size + kv_size > kMaxSize) break;

    if (!p.first.empty()) {
      uint32_t len_before = encoder.Length();
      encoder.Encode(grpc_core::Slice::FromCopiedString(key),
                     grpc_core::Slice::FromCopiedString(p.second));
      ASSERT_GT(encoder.Length(), len_before);
      current_size += (encoder.Length() - len_before);
      added_headers.emplace_back(key, p.second);
    }
  }

  if (added_headers.empty()) return;

  // Encode the headers and parse into a metadata batch.
  grpc_metadata_batch actual;
  grpc_core::HpackEncoderTestHelper::EncodeAndParse(std::move(encoder),
                                                    &actual);

  // Collect the headers from the metadata batch.
  Collector collector;
  actual.ForEach(&collector);

  // Verify that the headers are the same as the ones we added.
  ASSERT_EQ(collector.headers.size(), added_headers.size());
  for (size_t i = 0; i < added_headers.size(); ++i) {
    EXPECT_EQ(collector.headers[i].first, added_headers[i].first);
    EXPECT_EQ(collector.headers[i].second, added_headers[i].second);
  }
}

FUZZ_TEST(FuzzRawEncoder, FuzzRawEncoder)
    .WithDomains(
        fuzztest::Arbitrary<bool>(),
        fuzztest::VectorOf(fuzztest::PairOf(fuzztest::StringOf(fuzztest::OneOf(
                                                fuzztest::InRange('a', 'z'),
                                                fuzztest::InRange('0', '9'),
                                                fuzztest::Just('-'),
                                                fuzztest::Just('_'))),
                                            fuzztest::Arbitrary<std::string>()))
            .WithMaxSize(10));
