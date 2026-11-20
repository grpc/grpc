//
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
//

#include "src/core/xds/grpc/xds_header_map.h"

#include <grpc/status.h>

#include <string>
#include <vector>

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/matchers.h"
#include "src/core/util/upb_utils.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

// A decoded envoy.config.core.v3.HeaderValue. Tests match it with
// FieldsAre(key, value, raw_value); since PopulateHeaderMap() sets only
// raw_value (per gRFC A102), the expected value is always "".
struct Header {
  std::string key;
  std::string value;
  std::string raw_value;
};

class PopulateHeaderMapTest : public ::testing::Test {
 protected:
  void Append(absl::string_view key, absl::string_view value) {
    batch_.Append(key, Slice::FromCopiedString(value),
                  [](absl::string_view, const Slice&) {});
  }

  std::vector<Header> Populate(
      const std::vector<StringMatcher>& allowed = {},
      const std::vector<StringMatcher>& disallowed = {}) {
    auto* header_map = envoy_config_core_v3_HeaderMap_new(arena_.ptr());
    PopulateHeaderMap(batch_, allowed, disallowed, arena_.ptr(), header_map);
    size_t size;
    const envoy_config_core_v3_HeaderValue* const* headers =
        envoy_config_core_v3_HeaderMap_headers(header_map, &size);
    std::vector<Header> result;
    for (size_t i = 0; i < size; ++i) {
      result.push_back(
          Header{UpbStringToStdString(
                     envoy_config_core_v3_HeaderValue_key(headers[i])),
                 UpbStringToStdString(
                     envoy_config_core_v3_HeaderValue_value(headers[i])),
                 UpbStringToStdString(
                     envoy_config_core_v3_HeaderValue_raw_value(headers[i]))});
    }
    return result;
  }

  static StringMatcher Exact(absl::string_view key) {
    return StringMatcher::Create(StringMatcher::Type::kExact, key).value();
  }

  upb::Arena arena_;
  grpc_metadata_batch batch_;
};

TEST_F(PopulateHeaderMapTest, EmptyBatch) {
  EXPECT_THAT(Populate(), IsEmpty());
}

TEST_F(PopulateHeaderMapTest, NoMatchersForwardsAllHeadersAsRawValue) {
  Append("key1", "val1");
  Append("key2", "val2");
  batch_.Set(TeMetadata(), TeMetadata::kTrailers);
  batch_.Set(GrpcStatusMetadata(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_THAT(Populate(),
              UnorderedElementsAre(FieldsAre("key1", "", "val1"),
                                   FieldsAre("key2", "", "val2"),
                                   FieldsAre("te", "", "trailers"),
                                   FieldsAre("grpc-status", "", "14")));
}

TEST_F(PopulateHeaderMapTest, OnlyAllowedSet) {
  Append("key1", "val1");
  Append("key2", "val2");
  Append("key3", "val3");
  EXPECT_THAT(Populate({Exact("key1"), Exact("key3")}),
              UnorderedElementsAre(FieldsAre("key1", "", "val1"),
                                   FieldsAre("key3", "", "val3")));
}

TEST_F(PopulateHeaderMapTest, OnlyDisallowedSet) {
  Append("key1", "val1");
  Append("key2", "val2");
  Append("key3", "val3");
  EXPECT_THAT(Populate({}, {Exact("key2")}),
              UnorderedElementsAre(FieldsAre("key1", "", "val1"),
                                   FieldsAre("key3", "", "val3")));
}

TEST_F(PopulateHeaderMapTest, DisallowedTakesPrecedenceOverAllowed) {
  Append("key1", "val1");
  Append("key2", "val2");
  Append("key3", "val3");
  EXPECT_THAT(Populate({Exact("key1"), Exact("key3")}, {Exact("key3")}),
              UnorderedElementsAre(FieldsAre("key1", "", "val1")));
}

TEST_F(PopulateHeaderMapTest, MatchersApplyToTraitHeaders) {
  Append("key1", "val1");
  batch_.Set(TeMetadata(), TeMetadata::kTrailers);
  EXPECT_THAT(Populate({}, {Exact("te")}),
              UnorderedElementsAre(FieldsAre("key1", "", "val1")));
}

TEST_F(PopulateHeaderMapTest, BinaryHeaderValuesAreBase64Encoded) {
  Append("key1", "val1");
  Append("key2-bin", std::string("\x00\x01\xfe\xff", 4));
  batch_.Set(GrpcTraceBinMetadata(), Slice::FromCopiedString("trace"));
  EXPECT_THAT(Populate(), UnorderedElementsAre(
                              FieldsAre("key1", "", "val1"),
                              FieldsAre("key2-bin", "", "AAH+/w=="),
                              FieldsAre("grpc-trace-bin", "", "dHJhY2U=")));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
