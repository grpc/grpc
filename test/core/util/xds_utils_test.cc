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

#include <gtest/gtest.h>

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/util/validation_errors.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.h"

namespace grpc_core {
namespace testing {
namespace {

class ParseHeaderValueOptionTest : public ::testing::Test {
 protected:
  ParseHeaderValueOptionTest() = default;

  upb_Arena* arena_ = upb_Arena_New();
};

TEST_F(ParseHeaderValueOptionTest, ValidConfigWithDefaultAppendAction) {
  auto* header_value_option =
      envoy_config_core_v3_HeaderValueOption_new(arena_);
  auto* header = envoy_config_core_v3_HeaderValueOption_mutable_header(
      header_value_option, arena_);
  envoy_config_core_v3_HeaderValue_set_key(header,
                                           upb_StringView_FromString("foo"));
  envoy_config_core_v3_HeaderValue_set_value(header,
                                             upb_StringView_FromString("bar"));

  ValidationErrors errors;
  auto result = ParseHeaderValueOption(header_value_option, &errors);

  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(result.header.key, "foo");
  EXPECT_EQ(result.header.value, "bar");
  EXPECT_EQ(result.append_action,
            HeaderValueOption::AppendAction::kAppendIfExistsOrAdd);
  EXPECT_FALSE(result.keep_empty_value);
}

TEST_F(ParseHeaderValueOptionTest, ValidConfigWithAllFieldsSet) {
  auto* header_value_option =
      envoy_config_core_v3_HeaderValueOption_new(arena_);
  auto* header = envoy_config_core_v3_HeaderValueOption_mutable_header(
      header_value_option, arena_);
  envoy_config_core_v3_HeaderValue_set_key(header,
                                           upb_StringView_FromString("foo"));
  envoy_config_core_v3_HeaderValue_set_value(header,
                                             upb_StringView_FromString("bar"));
  envoy_config_core_v3_HeaderValueOption_set_append_action(
      header_value_option,
      envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS_OR_ADD);
  envoy_config_core_v3_HeaderValueOption_set_keep_empty_value(
      header_value_option, true);

  ValidationErrors errors;
  auto result = ParseHeaderValueOption(header_value_option, &errors);

  EXPECT_TRUE(errors.ok()) << errors.status(absl::StatusCode::kInvalidArgument,
                                            "unexpected errors");
  EXPECT_EQ(result.header.key, "foo");
  EXPECT_EQ(result.header.value, "bar");
  EXPECT_EQ(result.append_action,
            HeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd);
  EXPECT_TRUE(result.keep_empty_value);
}

TEST_F(ParseHeaderValueOptionTest, ErrorEmptyValue) {
  auto* header_value_option =
      envoy_config_core_v3_HeaderValueOption_new(arena_);
  auto* header = envoy_config_core_v3_HeaderValueOption_mutable_header(
      header_value_option, arena_);
  envoy_config_core_v3_HeaderValue_set_key(header,
                                           upb_StringView_FromString("foo"));
  envoy_config_core_v3_HeaderValue_set_value(header,
                                             upb_StringView_FromString(""));

  ValidationErrors errors;
  ParseHeaderValueOption(header_value_option, &errors);

  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "test_field").message(),
      "test_field: [field:value error:field not set]");
}

TEST_F(ParseHeaderValueOptionTest, AppendActionMapping) {
  struct TestCase {
    envoy_config_core_v3_HeaderValueOption_HeaderAppendAction input_action;
    HeaderValueOption::AppendAction expected_action;
  };
  std::vector<TestCase> test_cases = {
      {envoy_config_core_v3_HeaderValueOption_APPEND_IF_EXISTS_OR_ADD,
       HeaderValueOption::AppendAction::kAppendIfExistsOrAdd},
      {envoy_config_core_v3_HeaderValueOption_ADD_IF_ABSENT,
       HeaderValueOption::AppendAction::kAddIfAbsent},
      {envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS_OR_ADD,
       HeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd},
      {envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS,
       HeaderValueOption::AppendAction::kOverwriteIfExists},
  };

  for (const auto& test_case : test_cases) {
    auto* header_value_option =
        envoy_config_core_v3_HeaderValueOption_new(arena_);
    auto* header = envoy_config_core_v3_HeaderValueOption_mutable_header(
        header_value_option, arena_);
    envoy_config_core_v3_HeaderValue_set_key(header,
                                             upb_StringView_FromString("foo"));
    envoy_config_core_v3_HeaderValue_set_value(
        header, upb_StringView_FromString("bar"));
    envoy_config_core_v3_HeaderValueOption_set_append_action(
        header_value_option, test_case.input_action);

    ValidationErrors errors;
    auto result = ParseHeaderValueOption(header_value_option, &errors);

    EXPECT_TRUE(errors.ok()) << errors.status(
        absl::StatusCode::kInvalidArgument, "unexpected errors");
    EXPECT_EQ(result.append_action, test_case.expected_action);
  }
}

TEST_F(ParseHeaderValueOptionTest, ErrorMissingHeader) {
  auto* header_value_option =
      envoy_config_core_v3_HeaderValueOption_new(arena_);
  // Not setting header

  ValidationErrors errors;
  ParseHeaderValueOption(header_value_option, &errors);

  EXPECT_FALSE(errors.ok());
  EXPECT_EQ(
      errors.status(absl::StatusCode::kInvalidArgument, "test_field").message(),
      "test_field: [field:header error:field not present]");
}

TEST_F(ParseHeaderValueOptionTest, ErrorInvalidHeaderKey) {
  auto* header_value_option =
      envoy_config_core_v3_HeaderValueOption_new(arena_);
  auto* header = envoy_config_core_v3_HeaderValueOption_mutable_header(
      header_value_option, arena_);
  envoy_config_core_v3_HeaderValue_set_key(header,
                                           upb_StringView_FromString("foo\n"));
  envoy_config_core_v3_HeaderValue_set_value(header,
                                             upb_StringView_FromString("bar"));

  ValidationErrors errors;
  ParseHeaderValueOption(header_value_option, &errors);

  EXPECT_FALSE(errors.ok());
  // Expected error might depend on existing validation logic, assuming "invalid
  // character" or similar
}

TEST_F(ParseHeaderValueOptionTest, ErrorInvalidHeaderValue) {
  auto* header_value_option =
      envoy_config_core_v3_HeaderValueOption_new(arena_);
  auto* header = envoy_config_core_v3_HeaderValueOption_mutable_header(
      header_value_option, arena_);
  envoy_config_core_v3_HeaderValue_set_key(header,
                                           upb_StringView_FromString("foo"));
  envoy_config_core_v3_HeaderValue_set_value(
      header, upb_StringView_FromString("bar\n"));

  ValidationErrors errors;
  ParseHeaderValueOption(header_value_option, &errors);

  EXPECT_FALSE(errors.ok());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
