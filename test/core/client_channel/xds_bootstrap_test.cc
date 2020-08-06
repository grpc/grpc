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

#include "src/core/ext/xds/xds_bootstrap.h"
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
  const char* json_str =
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
      "      \"foo\": 1,"
      "      \"bar\": 2"
      "    },"
      "    \"ignore\": \"whee\""
      "  },"
      "  \"ignore\": {}"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  ASSERT_EQ(bootstrap.server().channel_creds.size(), 1UL);
  EXPECT_EQ(bootstrap.server().channel_creds[0].type, "fake");
  EXPECT_EQ(bootstrap.server().channel_creds[0].config.type(),
            Json::Type::JSON_NULL);
  ASSERT_NE(bootstrap.node(), nullptr);
  EXPECT_EQ(bootstrap.node()->id, "foo");
  EXPECT_EQ(bootstrap.node()->cluster, "bar");
  EXPECT_EQ(bootstrap.node()->locality_region, "milky_way");
  EXPECT_EQ(bootstrap.node()->locality_zone, "sol_system");
  EXPECT_EQ(bootstrap.node()->locality_subzone, "earth");
  ASSERT_EQ(bootstrap.node()->metadata.type(), Json::Type::OBJECT);
  EXPECT_THAT(bootstrap.node()->metadata.object_value(),
              ::testing::ElementsAre(
                  ::testing::Pair(
                      ::testing::Eq("bar"),
                      ::testing::AllOf(
                          ::testing::Property(&Json::type, Json::Type::NUMBER),
                          ::testing::Property(&Json::string_value, "2"))),
                  ::testing::Pair(
                      ::testing::Eq("foo"),
                      ::testing::AllOf(
                          ::testing::Property(&Json::type, Json::Type::NUMBER),
                          ::testing::Property(&Json::string_value, "1")))));
}

TEST(XdsBootstrapTest, ValidWithoutChannelCredsAndNode) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  EXPECT_EQ(bootstrap.server().channel_creds.size(), 0UL);
  EXPECT_EQ(bootstrap.node(), nullptr);
}

TEST(XdsBootstrapTest, MissingXdsServers) {
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse("{}", &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("\"xds_servers\" field not present"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, TopFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":1,"
      "  \"node\":1"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("\"xds_servers\" field is not an array(.*)"
                  "\"node\" field is not an object"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, XdsServerMissingServerUri) {
  const char* json_str =
      "{"
      "  \"xds_servers\":[{}]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("errors parsing \"xds_servers\" array(.*)"
                  "errors parsing index 0(.*)"
                  "\"server_uri\" field not present"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, XdsServerUriAndCredsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":1,"
      "      \"channel_creds\":1"
      "    }"
      "  ]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("errors parsing \"xds_servers\" array(.*)"
                  "errors parsing index 0(.*)"
                  "\"server_uri\" field is not a string(.*)"
                  "\"channel_creds\" field is not an array"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, ChannelCredsFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":\"foo\","
      "      \"channel_creds\":["
      "        {"
      "          \"type\":0,"
      "          \"config\":1"
      "        }"
      "      ]"
      "    }"
      "  ]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("errors parsing \"xds_servers\" array(.*)"
                  "errors parsing index 0(.*)"
                  "errors parsing \"channel_creds\" array(.*)"
                  "errors parsing index 0(.*)"
                  "\"type\" field is not a string(.*)"
                  "\"config\" field is not an object"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, NodeFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"node\":{"
      "    \"id\":0,"
      "    \"cluster\":0,"
      "    \"locality\":0,"
      "    \"metadata\":0"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("errors parsing \"node\" object(.*)"
                  "\"id\" field is not a string(.*)"
                  "\"cluster\" field is not a string(.*)"
                  "\"locality\" field is not an object(.*)"
                  "\"metadata\" field is not an object"));
  VerifyRegexMatch(error, e);
}

TEST(XdsBootstrapTest, LocalityFieldsWrongType) {
  const char* json_str =
      "{"
      "  \"node\":{"
      "    \"locality\":{"
      "      \"region\":0,"
      "      \"zone\":0,"
      "      \"subzone\":0"
      "    }"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("errors parsing \"node\" object(.*)"
                  "errors parsing \"locality\" object(.*)"
                  "\"region\" field is not a string(.*)"
                  "\"zone\" field is not a string(.*)"
                  "\"subzone\" field is not a string"));
  VerifyRegexMatch(error, e);
}

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
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
