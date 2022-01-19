// Copyright 2021 gRPC authors.
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

#include "src/core/ext/filters/rbac/rbac_service_config_parser.h"

#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/service_config/service_config.h"
#include "test/core/util/test_config.h"

// A regular expression to enter referenced or child errors.
#ifdef GRPC_ERROR_IS_ABSEIL_STATUS
#define CHILD_ERROR_TAG ".*children.*"
#else
#define CHILD_ERROR_TAG ".*referenced_errors.*"
#endif

namespace grpc_core {
namespace testing {
namespace {

// Test basic parsing of RBAC policy
TEST(RbacServiceConfigParsingTest, EmptyRbacPolicy) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [ {\n"
      "    } ]"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr =
      svc_cfg->GetMethodParsedConfigVector(grpc_empty_slice());
  ASSERT_NE(vector_ptr, nullptr);
  auto* parsed_rbac_config = static_cast<RbacMethodParsedConfig*>(
      ((*vector_ptr)[RbacServiceConfigParser::ParserIndex()]).get());
  ASSERT_NE(parsed_rbac_config, nullptr);
  ASSERT_NE(parsed_rbac_config->authorization_engine(0), nullptr);
  EXPECT_EQ(parsed_rbac_config->authorization_engine(0)->action(),
            Rbac::Action::kDeny);
  EXPECT_EQ(parsed_rbac_config->authorization_engine(0)->num_policies(), 0);
}

// Test that RBAC policies are not parsed if the channel arg
// GRPC_ARG_PARSE_RBAC_METHOD_CONFIG is not present
TEST(RbacServiceConfigParsingTest, MissingChannelArg) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [ {\n"
      "    } ]"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr =
      svc_cfg->GetMethodParsedConfigVector(grpc_empty_slice());
  ASSERT_NE(vector_ptr, nullptr);
  auto* parsed_rbac_config = static_cast<RbacMethodParsedConfig*>(
      ((*vector_ptr)[RbacServiceConfigParser::ParserIndex()]).get());
  ASSERT_EQ(parsed_rbac_config, nullptr);
}

// Test an empty rbacPolicy array
TEST(RbacServiceConfigParsingTest, EmptyRbacPolicyArray) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": []"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr =
      svc_cfg->GetMethodParsedConfigVector(grpc_empty_slice());
  ASSERT_NE(vector_ptr, nullptr);
  auto* parsed_rbac_config = static_cast<RbacMethodParsedConfig*>(
      ((*vector_ptr)[RbacServiceConfigParser::ParserIndex()]).get());
  ASSERT_EQ(parsed_rbac_config, nullptr);
}

// Test presence of multiple RBAC policies in the array
TEST(RbacServiceConfigParsingTest, MultipleRbacPolicies) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [ {}, {}, {} ]"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr =
      svc_cfg->GetMethodParsedConfigVector(grpc_empty_slice());
  ASSERT_NE(vector_ptr, nullptr);
  auto* parsed_rbac_config = static_cast<RbacMethodParsedConfig*>(
      ((*vector_ptr)[RbacServiceConfigParser::ParserIndex()]).get());
  ASSERT_NE(parsed_rbac_config, nullptr);
  for (auto i = 0; i < 3; ++i) {
    ASSERT_NE(parsed_rbac_config->authorization_engine(i), nullptr);
    EXPECT_EQ(parsed_rbac_config->authorization_engine(i)->action(),
              Rbac::Action::kDeny);
    EXPECT_EQ(parsed_rbac_config->authorization_engine(i)->num_policies(), 0);
  }
}

TEST(RbacServiceConfigParsingTest, BadRbacPolicyType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": 1234"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Rbac parser" CHILD_ERROR_TAG
                               "field:rbacPolicy error:type should be ARRAY"));
  GRPC_ERROR_UNREF(error);
}

TEST(RbacServiceConfigParsingTest, BadRulesType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\"rules\":1}]"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Rbac parser" CHILD_ERROR_TAG
                               "rbacPolicy\\[0\\]" CHILD_ERROR_TAG
                               "field:rules error:type should be OBJECT"));
  GRPC_ERROR_UNREF(error);
}

TEST(RbacServiceConfigParsingTest, BadActionAndPolicyType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":{},\n"
      "        \"policies\":123\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Rbac parser" CHILD_ERROR_TAG
                               "rbacPolicy\\[0\\]" CHILD_ERROR_TAG
                               "field:action error:type should be NUMBER.*"
                               "field:policies error:type should be OBJECT"));
  GRPC_ERROR_UNREF(error);
}

TEST(RbacServiceConfigParsingTest, MissingPermissionAndPrincipals) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Rbac parser" CHILD_ERROR_TAG
                               "rbacPolicy\\[0\\]" CHILD_ERROR_TAG
                               "policies key:'policy'" CHILD_ERROR_TAG
                               "field:permissions error:does not exist.*"
                               "field:principals error:does not exist"));
  GRPC_ERROR_UNREF(error);
}

TEST(RbacServiceConfigParsingTest, EmptyPrincipalAndPermission) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "            \"permissions\":[{}],\n"
      "            \"principals\":[{}]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(
          "Rbac parser" CHILD_ERROR_TAG "rbacPolicy\\[0\\]" CHILD_ERROR_TAG
          "policies key:'policy'" CHILD_ERROR_TAG
          "permissions\\[0\\]" CHILD_ERROR_TAG "No valid rule found.*"
          "principals\\[0\\]" CHILD_ERROR_TAG "No valid id found"));
  GRPC_ERROR_UNREF(error);
}

TEST(RbacServiceConfigParsingTest, VariousPermissionsAndPrincipalsTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "            \"permissions\":[\n"
      "              {\"andRules\":{\"rules\":[{\"any\":true}]}},\n"
      "              {\"orRules\":{\"rules\":[{\"any\":true}]}},\n"
      "              {\"any\":true},\n"
      "              {\"header\":{\"name\":\"name\", \"exactMatch\":\"\"}},\n"
      "              {\"urlPath\":{\"path\":{\"exact\":\"\"}}},\n"
      "              {\"destinationIp\":{\"addressPrefix\":\"::1\"}},\n"
      "              {\"destinationPort\":1234},\n"
      "              {\"metadata\":{\"invert\":true}},\n"
      "              {\"notRule\":{\"any\":true}},\n"
      "              {\"requestedServerName\":{\"exact\":\"\"}}\n"
      "            ],\n"
      "            \"principals\":[\n"
      "              {\"andIds\":{\"ids\":[{\"any\":true}]}},\n"
      "              {\"orIds\":{\"ids\":[{\"any\":true}]}},\n"
      "              {\"any\":true},\n"
      "              {\"authenticated\":{\n"
      "                \"principalName\":{\"exact\":\"\"}}},\n"
      "              {\"sourceIp\":{\"addressPrefix\":\"::1\"}},\n"
      "              {\"directRemoteIp\":{\"addressPrefix\":\"::1\"}},\n"
      "              {\"remoteIp\":{\"addressPrefix\":\"::1\"}},\n"
      "              {\"header\":{\"name\":\"name\", \"exactMatch\":\"\"}},\n"
      "              {\"urlPath\":{\"path\":{\"exact\":\"\"}}},\n"
      "              {\"metadata\":{\"invert\":true}},\n"
      "              {\"notId\":{\"any\":true}}\n"
      "            ]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr =
      svc_cfg->GetMethodParsedConfigVector(grpc_empty_slice());
  ASSERT_NE(vector_ptr, nullptr);
  auto* parsed_rbac_config = static_cast<RbacMethodParsedConfig*>(
      ((*vector_ptr)[RbacServiceConfigParser::ParserIndex()]).get());
  ASSERT_NE(parsed_rbac_config, nullptr);
  ASSERT_NE(parsed_rbac_config->authorization_engine(0), nullptr);
  EXPECT_EQ(parsed_rbac_config->authorization_engine(0)->num_policies(), 1);
}

TEST(RbacServiceConfigParsingTest, VariousPermissionsAndPrincipalsBadTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "            \"permissions\":[\n"
      "              {\"andRules\":1234},\n"
      "              {\"orRules\":1234},\n"
      "              {\"any\":1234},\n"
      "              {\"header\":1234},\n"
      "              {\"urlPath\":1234},\n"
      "              {\"destinationIp\":1234},\n"
      "              {\"destinationPort\":\"port\"},\n"
      "              {\"metadata\":1234},\n"
      "              {\"notRule\":1234},\n"
      "              {\"requestedServerName\":1234}\n"
      "            ],\n"
      "            \"principals\":[\n"
      "              {\"andIds\":1234},\n"
      "              {\"orIds\":1234},\n"
      "              {\"any\":1234},\n"
      "              {\"authenticated\":1234},\n"
      "              {\"sourceIp\":1234},\n"
      "              {\"directRemoteIp\":1234},\n"
      "              {\"remoteIp\":1234},\n"
      "              {\"header\":1234},\n"
      "              {\"urlPath\":1234},\n"
      "              {\"metadata\":1234},\n"
      "              {\"notId\":1234}\n"
      "            ]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(
          "Rbac parser" CHILD_ERROR_TAG "rbacPolicy\\[0\\]" CHILD_ERROR_TAG
          "policies key:'policy'" CHILD_ERROR_TAG
          "permissions\\[0\\]" CHILD_ERROR_TAG
          "field:andRules error:type should be OBJECT.*"
          "permissions\\[1\\]" CHILD_ERROR_TAG
          "field:orRules error:type should be OBJECT.*"
          "permissions\\[2\\]" CHILD_ERROR_TAG
          "field:any error:type should be BOOLEAN.*"
          "permissions\\[3\\]" CHILD_ERROR_TAG
          "field:header error:type should be OBJECT.*"
          "permissions\\[4\\]" CHILD_ERROR_TAG
          "field:urlPath error:type should be OBJECT.*"
          "permissions\\[5\\]" CHILD_ERROR_TAG
          "field:destinationIp error:type should be OBJECT.*"
          "permissions\\[6\\]" CHILD_ERROR_TAG
          "field:destinationPort error:type should be NUMBER.*"
          "permissions\\[7\\]" CHILD_ERROR_TAG
          "field:metadata error:type should be OBJECT.*"
          "permissions\\[8\\]" CHILD_ERROR_TAG
          "field:notRule error:type should be OBJECT.*"
          "permissions\\[9\\]" CHILD_ERROR_TAG
          "field:requestedServerName error:type should be OBJECT.*"
          "principals\\[0\\]" CHILD_ERROR_TAG
          "field:andIds error:type should be OBJECT.*"
          "principals\\[1\\]" CHILD_ERROR_TAG
          "field:orIds error:type should be OBJECT.*"
          "principals\\[2\\]" CHILD_ERROR_TAG
          "field:any error:type should be BOOLEAN.*"
          "principals\\[3\\]" CHILD_ERROR_TAG
          "field:authenticated error:type should be OBJECT.*"
          "principals\\[4\\]" CHILD_ERROR_TAG
          "field:sourceIp error:type should be OBJECT.*"
          "principals\\[5\\]" CHILD_ERROR_TAG
          "field:directRemoteIp error:type should be OBJECT.*"
          "principals\\[6\\]" CHILD_ERROR_TAG
          "field:remoteIp error:type should be OBJECT.*"
          "principals\\[7\\]" CHILD_ERROR_TAG
          "field:header error:type should be OBJECT.*"
          "principals\\[8\\]" CHILD_ERROR_TAG
          "field:urlPath error:type should be OBJECT.*"
          "principals\\[9\\]" CHILD_ERROR_TAG
          "field:metadata error:type should be OBJECT.*"
          "principals\\[10\\]" CHILD_ERROR_TAG
          "field:notId error:type should be OBJECT.*"));
  GRPC_ERROR_UNREF(error);
}

TEST(RbacServiceConfigParsingTest, HeaderMatcherVariousTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "            \"permissions\":[\n"
      "              {\"header\":{\"name\":\"name\", \"exactMatch\":\"\", \n"
      "                \"invertMatch\":true}},\n"
      "              {\"header\":{\"name\":\"name\", \"safeRegexMatch\":{\n"
      "                \"regex\":\"\"}}},\n"
      "              {\"header\":{\"name\":\"name\", \"rangeMatch\":{\n"
      "                \"start\":0, \"end\":1}}},\n"
      "              {\"header\":{\"name\":\"name\", \"presentMatch\":true}},\n"
      "              {\"header\":{\"name\":\"name\", \"prefixMatch\":\"\"}},\n"
      "              {\"header\":{\"name\":\"name\", \"suffixMatch\":\"\"}},\n"
      "              {\"header\":{\"name\":\"name\", \"containsMatch\":\"\"}}\n"
      "            ],\n"
      "            \"principals\":[]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr =
      svc_cfg->GetMethodParsedConfigVector(grpc_empty_slice());
  ASSERT_NE(vector_ptr, nullptr);
  auto* parsed_rbac_config = static_cast<RbacMethodParsedConfig*>(
      ((*vector_ptr)[RbacServiceConfigParser::ParserIndex()]).get());
  ASSERT_NE(parsed_rbac_config, nullptr);
  ASSERT_NE(parsed_rbac_config->authorization_engine(0), nullptr);
  EXPECT_EQ(parsed_rbac_config->authorization_engine(0)->num_policies(), 1);
}

TEST(RbacServiceConfigParsingTest, HeaderMatcherBadTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "            \"permissions\":[\n"
      "              {\"header\":{\"name\":\"name\", \"exactMatch\":1, \n"
      "                \"invertMatch\":1}},\n"
      "              {\"header\":{\"name\":\"name\", \"safeRegexMatch\":1}},\n"
      "              {\"header\":{\"name\":\"name\", \"rangeMatch\":1}},\n"
      "              {\"header\":{\"name\":\"name\", \"presentMatch\":1}},\n"
      "              {\"header\":{\"name\":\"name\", \"prefixMatch\":1}},\n"
      "              {\"header\":{\"name\":\"name\", \"suffixMatch\":1}},\n"
      "              {\"header\":{\"name\":\"name\", \"containsMatch\":1}}\n"
      "            ],\n"
      "            \"principals\":[]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(
          "Rbac parser" CHILD_ERROR_TAG "rbacPolicy\\[0\\]" CHILD_ERROR_TAG
          "policies key:'policy'" CHILD_ERROR_TAG
          "permissions\\[0\\]" CHILD_ERROR_TAG "header" CHILD_ERROR_TAG
          "field:invertMatch error:type should be BOOLEAN.*"
          "field:exactMatch error:type should be STRING.*"
          "permissions\\[1\\]" CHILD_ERROR_TAG "header" CHILD_ERROR_TAG
          "field:safeRegexMatch error:type should be OBJECT.*"
          "permissions\\[2\\]" CHILD_ERROR_TAG "header" CHILD_ERROR_TAG
          "field:rangeMatch error:type should be OBJECT.*"
          "permissions\\[3\\]" CHILD_ERROR_TAG "header" CHILD_ERROR_TAG
          "field:presentMatch error:type should be BOOLEAN.*"
          "permissions\\[4\\]" CHILD_ERROR_TAG "header" CHILD_ERROR_TAG
          "field:prefixMatch error:type should be STRING.*"
          "permissions\\[5\\]" CHILD_ERROR_TAG "header" CHILD_ERROR_TAG
          "field:suffixMatch error:type should be STRING.*"
          "permissions\\[6\\]" CHILD_ERROR_TAG "header" CHILD_ERROR_TAG
          "field:containsMatch error:type should be STRING.*"));
  GRPC_ERROR_UNREF(error);
}

TEST(RbacServiceConfigParsingTest, StringMatcherVariousTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "            \"permissions\":[\n"
      "              {\"requestedServerName\":{\"exact\":\"\", \n"
      "                \"ignoreCase\":true}},\n"
      "              {\"requestedServerName\":{\"prefix\":\"\"}},\n"
      "              {\"requestedServerName\":{\"suffix\":\"\"}},\n"
      "              {\"requestedServerName\":{\"safeRegex\":{\n"
      "                \"regex\":\"\"}}},\n"
      "              {\"requestedServerName\":{\"contains\":\"\"}}\n"
      "            ],\n"
      "            \"principals\":[]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr =
      svc_cfg->GetMethodParsedConfigVector(grpc_empty_slice());
  ASSERT_NE(vector_ptr, nullptr);
  auto* parsed_rbac_config = static_cast<RbacMethodParsedConfig*>(
      ((*vector_ptr)[RbacServiceConfigParser::ParserIndex()]).get());
  ASSERT_NE(parsed_rbac_config, nullptr);
  ASSERT_NE(parsed_rbac_config->authorization_engine(0), nullptr);
  EXPECT_EQ(parsed_rbac_config->authorization_engine(0)->num_policies(), 1);
}

TEST(RbacServiceConfigParsingTest, StringMatcherBadTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"rules\":{\n"
      "        \"action\":1,\n"
      "        \"policies\":{\n"
      "          \"policy\":{\n"
      "            \"permissions\":[\n"
      "              {\"requestedServerName\":{\"exact\":1, \n"
      "                \"ignoreCase\":1}},\n"
      "              {\"requestedServerName\":{\"prefix\":1}},\n"
      "              {\"requestedServerName\":{\"suffix\":1}},\n"
      "              {\"requestedServerName\":{\"safeRegex\":1}},\n"
      "              {\"requestedServerName\":{\"contains\":1}}\n"
      "            ],\n"
      "            \"principals\":[]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG), 1);
  grpc_channel_args args = {1, &arg};
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Rbac parser" CHILD_ERROR_TAG
                               "rbacPolicy\\[0\\]" CHILD_ERROR_TAG
                               "policies key:'policy'" CHILD_ERROR_TAG
                               "permissions\\[0\\]" CHILD_ERROR_TAG
                               "requestedServerName" CHILD_ERROR_TAG
                               "field:ignoreCase error:type should be BOOLEAN.*"
                               "field:exact error:type should be STRING.*"
                               "permissions\\[1\\]" CHILD_ERROR_TAG
                               "requestedServerName" CHILD_ERROR_TAG
                               "field:prefix error:type should be STRING.*"
                               "permissions\\[2\\]" CHILD_ERROR_TAG
                               "requestedServerName" CHILD_ERROR_TAG
                               "field:suffix error:type should be STRING.*"
                               "permissions\\[3\\]" CHILD_ERROR_TAG
                               "requestedServerName" CHILD_ERROR_TAG
                               "field:safeRegex error:type should be OBJECT.*"
                               "permissions\\[4\\]" CHILD_ERROR_TAG
                               "requestedServerName" CHILD_ERROR_TAG
                               "field:contains error:type should be STRING.*"));
  GRPC_ERROR_UNREF(error);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
