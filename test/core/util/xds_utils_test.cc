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

#include "src/core/util/xds_utils.h"

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

class ParseEnvoyHeaderTest : public ::testing::Test {
 protected:
  ParseEnvoyHeaderTest() = default;

  upb_Arena* arena_ = upb_Arena_New();
};

TEST_F(ParseEnvoyHeaderTest, NormalHeader) {
  auto* header = ParseEnvoyHeader("foo", "bar", arena_);
  EXPECT_EQ(UpbStringToAbsl(envoy_config_core_v3_HeaderValue_key(header)),
            "foo");
  EXPECT_EQ(UpbStringToAbsl(envoy_config_core_v3_HeaderValue_value(header)),
            "bar");
  EXPECT_TRUE(
      UpbStringToAbsl(envoy_config_core_v3_HeaderValue_raw_value(header))
          .empty());
}

TEST_F(ParseEnvoyHeaderTest, BinaryHeader) {
  auto* header = ParseEnvoyHeader("foo-bin", "bar", arena_);
  EXPECT_EQ(UpbStringToAbsl(envoy_config_core_v3_HeaderValue_key(header)),
            "foo-bin");
  EXPECT_EQ(UpbStringToAbsl(envoy_config_core_v3_HeaderValue_raw_value(header)),
            "bar");
  EXPECT_TRUE(
      UpbStringToAbsl(envoy_config_core_v3_HeaderValue_value(header)).empty());
}

TEST_F(ParseEnvoyHeaderTest, RoundTripNormal) {
  auto* header = ParseEnvoyHeader("foo", "bar", arena_);
  ValidationErrors errors;
  auto [key, value] = ParseXdsHeader(header, &errors);
  EXPECT_TRUE(errors.ok());
  EXPECT_EQ(key, "foo");
  EXPECT_EQ(value, "bar");
}

TEST_F(ParseEnvoyHeaderTest, RoundTripBinary) {
  auto* header = ParseEnvoyHeader("foo-bin", "YmFy", arena_);
  ValidationErrors errors;
  auto [key, value] = ParseXdsHeader(header, &errors);
  EXPECT_TRUE(errors.ok());
  EXPECT_EQ(key, "foo-bin");
  EXPECT_EQ(value, "bar");
}

TEST_F(ParseEnvoyHeaderTest, InvalidKey) {
  // Empty key
  EXPECT_EQ(ParseEnvoyHeader("", "bar", arena_), nullptr);
  // Key too long
  std::string long_key(16385, 'a');
  EXPECT_EQ(ParseEnvoyHeader(long_key, "bar", arena_), nullptr);
  // Key with invalid char (uppercase)
  EXPECT_EQ(ParseEnvoyHeader("Foo", "bar", arena_), nullptr);
  // Key with invalid char (:)
  EXPECT_EQ(ParseEnvoyHeader(":foo", "bar", arena_), nullptr);
  // Key is "host"
  EXPECT_EQ(ParseEnvoyHeader("host", "bar", arena_), nullptr);
}

TEST_F(ParseEnvoyHeaderTest, InvalidValue) {
  // Value too long
  std::string long_value(16385, 'a');
  EXPECT_EQ(ParseEnvoyHeader("foo", long_value, arena_), nullptr);
  // Non-binary value with invalid char
  EXPECT_NE(ParseEnvoyHeader("foo", "bar\n", arena_), nullptr);
}

TEST_F(ParseEnvoyHeaderTest, ValidBinaryValue) {
  std::string long_value(16385, 'a');
  EXPECT_EQ(ParseEnvoyHeader("foo-bin", long_value, arena_), nullptr);

  auto* header = ParseEnvoyHeader("foo-bin", "bar\n", arena_);
  EXPECT_NE(header, nullptr);
  EXPECT_EQ(UpbStringToAbsl(envoy_config_core_v3_HeaderValue_raw_value(header)),
            "bar\n");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
