//
// Copyright 2019 gRPC authors.
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

#include <regex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>

#include "src/core/ext/filters/client_channel/xds/xds_bootstrap.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

void VerifyRegexMatch(grpc_error* error, const std::regex& e) {
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

TEST(XdsBootstrapTest, Basic) {
  const char* json =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": ["
      "        {"
      "          \"type\": \"fake\","
      "          \"ignore\": 0"
      "        }"
      "      ],"
      "      \"ignore\": 0"
      "    },"
      "    {"
      "      \"server_uri\": \"ignored\","
      "      \"channel_creds\": ["
      "        {"
      "          \"type\": \"ignored\","
      "          \"ignore\": 0"
      "        }"
      "      ],"
      "      \"ignore\": 0"
      "    }"
      "  ],"
      "  \"node\": {"
      "    \"id\": \"foo\","
      "    \"cluster\": \"bar\","
      "    \"locality\": {"
      "      \"region\": \"milky_way\","
      "      \"zone\": \"sol_system\","
      "      \"subzone\": \"earth\","
      "      \"ignore\": {}"
      "    },"
      "    \"metadata\": {"
      "      \"null\": null,"
      "      \"string\": \"quux\","
      "      \"double\": 123.4,"
      "      \"bool\": true,"
      "      \"struct\": {"
      "        \"whee\": 0"
      "      },"
      "      \"list\": [1, 2, 3]"
      "    },"
      "    \"ignore\": \"whee\""
      "  },"
      "  \"ignore\": {}"
      "}";
  grpc_slice slice = grpc_slice_from_copied_string(json);
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  EXPECT_STREQ(bootstrap.server().server_uri, "fake:///lb");
  ASSERT_EQ(bootstrap.server().channel_creds.size(), 1);
  EXPECT_STREQ(bootstrap.server().channel_creds[0].type, "fake");
  EXPECT_EQ(bootstrap.server().channel_creds[0].config, nullptr);
  ASSERT_NE(bootstrap.node(), nullptr);
  EXPECT_STREQ(bootstrap.node()->id, "foo");
  EXPECT_STREQ(bootstrap.node()->cluster, "bar");
  EXPECT_STREQ(bootstrap.node()->locality_region, "milky_way");
  EXPECT_STREQ(bootstrap.node()->locality_zone, "sol_system");
  EXPECT_STREQ(bootstrap.node()->locality_subzone, "earth");
  EXPECT_THAT(
      bootstrap.node()->metadata,
      ::testing::ElementsAre(
          ::testing::Pair(
              ::testing::StrEq("bool"),
              ::testing::AllOf(
                  ::testing::Field(&XdsBootstrap::MetadataValue::type,
                                   XdsBootstrap::MetadataValue::Type::BOOL),
                  ::testing::Field(&XdsBootstrap::MetadataValue::bool_value,
                                   true))),
          ::testing::Pair(
              ::testing::StrEq("double"),
              ::testing::AllOf(
                  ::testing::Field(&XdsBootstrap::MetadataValue::type,
                                   XdsBootstrap::MetadataValue::Type::DOUBLE),
                  ::testing::Field(&XdsBootstrap::MetadataValue::double_value,
                                   123.4))),
          ::testing::Pair(
              ::testing::StrEq("list"),
              ::testing::Field(&XdsBootstrap::MetadataValue::type,
                               XdsBootstrap::MetadataValue::Type::LIST)),
          ::testing::Pair(::testing::StrEq("null"),
                          ::testing::AllOf(::testing::Field(
                              &XdsBootstrap::MetadataValue::type,
                              XdsBootstrap::MetadataValue::Type::MD_NULL))),
          ::testing::Pair(
              ::testing::StrEq("string"),
              ::testing::AllOf(
                  ::testing::Field(&XdsBootstrap::MetadataValue::type,
                                   XdsBootstrap::MetadataValue::Type::STRING),
                  ::testing::Field(&XdsBootstrap::MetadataValue::string_value,
                                   ::testing::StrEq("quux")))),
          ::testing::Pair(
              ::testing::StrEq("struct"),
              ::testing::AllOf(
                  ::testing::Field(&XdsBootstrap::MetadataValue::type,
                                   XdsBootstrap::MetadataValue::Type::STRUCT),
                  ::testing::Field(
                      &XdsBootstrap::MetadataValue::struct_value,
                      ::testing::ElementsAre(::testing::Pair(
                          ::testing::StrEq("whee"),
                          ::testing::AllOf(
                              ::testing::Field(
                                  &XdsBootstrap::MetadataValue::type,
                                  XdsBootstrap::MetadataValue::Type::DOUBLE),
                              ::testing::Field(
                                  &XdsBootstrap::MetadataValue::double_value,
                                  0)))))))));
  // TODO(roth): Once our InlinedVector<> implementation supports
  // iteration, replace this by using ElementsAre() in the statement above.
  auto it = bootstrap.node()->metadata.find("list");
  ASSERT_TRUE(it != bootstrap.node()->metadata.end());
  ASSERT_EQ(it->second.list_value.size(), 3);
  EXPECT_EQ(it->second.list_value[0].type,
            XdsBootstrap::MetadataValue::Type::DOUBLE);
  EXPECT_EQ(it->second.list_value[0].double_value, 1);
  EXPECT_EQ(it->second.list_value[1].type,
            XdsBootstrap::MetadataValue::Type::DOUBLE);
  EXPECT_EQ(it->second.list_value[1].double_value, 2);
  EXPECT_EQ(it->second.list_value[2].type,
            XdsBootstrap::MetadataValue::Type::DOUBLE);
  EXPECT_EQ(it->second.list_value[2].double_value, 3);
}

TEST(XdsBootstrapTest, ValidWithoutChannelCredsAndNode) {
  const char* json =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ]"
      "}";
  grpc_slice slice = grpc_slice_from_copied_string(json);
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE);
  EXPECT_STREQ(bootstrap.server().server_uri, "fake:///lb");
  EXPECT_EQ(bootstrap.server().channel_creds.size(), 0);
  EXPECT_EQ(bootstrap.node(), nullptr);
}

TEST(XdsBootstrapTest, InvalidJson) {
  grpc_slice slice = grpc_slice_from_copied_string("");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("failed to parse bootstrap file JSON"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, MalformedJson) {
  grpc_slice slice = grpc_slice_from_copied_string("\"foo\"");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("malformed JSON in bootstrap file"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, MissingXdsServers) {
  grpc_slice slice = grpc_slice_from_copied_string("{}");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("\"xds_servers\" field not present"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, BadXdsServers) {
  grpc_slice slice = grpc_slice_from_copied_string(
      "{"
      "  \"xds_servers\":1,"
      "  \"xds_servers\":[{}]"
      "}");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("\"xds_servers\" field is not an array(.*)"
                  "duplicate \"xds_servers\" field(.*)"
                  "errors parsing \"xds_servers\" array(.*)"
                  "errors parsing index 0(.*)"
                  "\"server_uri\" field not present"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, BadXdsServerContents) {
  grpc_slice slice = grpc_slice_from_copied_string(
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":1,"
      "      \"server_uri\":\"foo\","
      "      \"channel_creds\":1,"
      "      \"channel_creds\":{}"
      "    }"
      "  ]"
      "}");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("errors parsing \"xds_servers\" array(.*)"
                  "errors parsing index 0(.*)"
                  "\"server_uri\" field is not a string(.*)"
                  "duplicate \"server_uri\" field(.*)"
                  "\"channel_creds\" field is not an array(.*)"
                  "\"channel_creds\" field is not an array(.*)"
                  "duplicate \"channel_creds\" field(.*)"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, BadChannelCredsContents) {
  grpc_slice slice = grpc_slice_from_copied_string(
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":\"foo\","
      "      \"channel_creds\":["
      "        {"
      "          \"type\":0,"
      "          \"type\":\"fake\","
      "          \"config\":1,"
      "          \"config\":{}"
      "        }"
      "      ]"
      "    }"
      "  ]"
      "}");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("errors parsing \"xds_servers\" array(.*)"
                  "errors parsing index 0(.*)"
                  "errors parsing \"channel_creds\" array(.*)"
                  "errors parsing index 0(.*)"
                  "\"type\" field is not a string(.*)"
                  "duplicate \"type\" field(.*)"
                  "\"config\" field is not an object(.*)"
                  "duplicate \"config\" field"));
  VerifyRegexMatch(error, e);
}

// under TSAN, ASAN and UBSAN, bazel RBE can suffer from a std::regex
// stackoverflow bug if the analyzed string is too long (> ~2000 characters). As
// this test is only single-thread and deterministic, it is safe to just disable
// it under TSAN and ASAN until
// https://github.com/GoogleCloudPlatform/layer-definitions/issues/591
// is resolved. The risk for UBSAN problem also doesn't seem to be very high.
#ifndef GRPC_ASAN
#ifndef GRPC_TSAN
#ifndef GRPC_UBSAN

TEST(XdsBootstrapTest, BadNode) {
  grpc_slice slice = grpc_slice_from_copied_string(
      "{"
      "  \"node\":1,"
      "  \"node\":{"
      "    \"id\":0,"
      "    \"id\":\"foo\","
      "    \"cluster\":0,"
      "    \"cluster\":\"foo\","
      "    \"locality\":0,"
      "    \"locality\":{"
      "      \"region\":0,"
      "      \"region\":\"foo\","
      "      \"zone\":0,"
      "      \"zone\":\"foo\","
      "      \"subzone\":0,"
      "      \"subzone\":\"foo\""
      "    },"
      "    \"metadata\":0,"
      "    \"metadata\":{"
      "      \"foo\":0,"
      "      \"foo\":\"whee\","
      "      \"foo\":\"whee2\""
      "    }"
      "  }"
      "}");
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::XdsBootstrap bootstrap(slice, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("\"node\" field is not an object(.*)"
                  "duplicate \"node\" field(.*)"
                  "errors parsing \"node\" object(.*)"
                  "\"id\" field is not a string(.*)"
                  "duplicate \"id\" field(.*)"
                  "\"cluster\" field is not a string(.*)"
                  "duplicate \"cluster\" field(.*)"
                  "\"locality\" field is not an object(.*)"
                  "duplicate \"locality\" field(.*)"
                  "errors parsing \"locality\" object(.*)"
                  "\"region\" field is not a string(.*)"
                  "duplicate \"region\" field(.*)"
                  "\"zone\" field is not a string(.*)"
                  "duplicate \"zone\" field(.*)"
                  "\"subzone\" field is not a string(.*)"
                  "duplicate \"subzone\" field(.*)"
                  "\"metadata\" field is not an object(.*)"
                  "duplicate \"metadata\" field(.*)"
                  "errors parsing \"metadata\" object(.*)"
                  "duplicate metadata key \"foo\""));
  VerifyRegexMatch(error, e);
}

#endif
#endif
#endif

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
// Regexes don't work in old libstdc++ versions, so just skip testing in those
// cases
#if defined(__GLIBCXX__) && (__GLIBCXX__ <= 20150623)
  gpr_log(GPR_ERROR,
          "Skipping xds_bootstrap_test since std::regex is not supported on "
          "this system.");
  return 0;
#endif
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
