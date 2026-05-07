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

#include "src/core/credentials/call/utils/redact_utils.h"

#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(RedactUtilsTest, RedactSensitiveFields) {
  EXPECT_EQ(
      RedactSensitiveJsonFields(
          R"string({"access_token": "secret1", "refresh_token": "secret2", "client_secret": "secret3", "other": "value"})string"),
      R"string({"access_token":"<redacted>","client_secret":"<redacted>","other":"value","refresh_token":"<redacted>"})string");
}

TEST(RedactUtilsTest, NoSensitiveFields) {
  EXPECT_EQ(RedactSensitiveJsonFields(R"string({"other": "value"})string"),
            R"string({"other":"value"})string");
}

TEST(RedactUtilsTest, NestedFields) {
  EXPECT_EQ(
      RedactSensitiveJsonFields(
          R"string({"a": {"access_token": "secret1"}, "b": [{"refresh_token": "secret2"}]})string"),
      R"string({"a":{"access_token":"<redacted>"},"b":[{"refresh_token":"<redacted>"}]})string");
}

TEST(RedactUtilsTest, ManyLayersOfNesting) {
  EXPECT_EQ(
      RedactSensitiveJsonFields(
          R"string({"l1":[{"l2":{"access_token": "secret"}}, {"l2_sibling": ["foo", {"client_secret": "secret2"}]}]})string"),
      R"string({"l1":[{"l2":{"access_token":"<redacted>"}},{"l2_sibling":["foo",{"client_secret":"<redacted>"}]}]})string");
}

TEST(RedactUtilsTest, InvalidJson) {
  EXPECT_EQ(RedactSensitiveJsonFields(R"string({"a": "b",)string"),
            "[unparseable JSON - potential secrets redacted]");
}

TEST(RedactUtilsTest, EmptyJson) {
  EXPECT_EQ(RedactSensitiveJsonFields(""),
            "[unparseable JSON - potential secrets redacted]");
  EXPECT_EQ(RedactSensitiveJsonFields("{}"), "{}");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
