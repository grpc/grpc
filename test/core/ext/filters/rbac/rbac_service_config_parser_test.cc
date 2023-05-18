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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

// Filter name is required in RBAC policy.
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].filter_name error:field not "
            "present]")
      << service_config.status();
}

// Test basic parsing of RBAC policy
TEST(RbacServiceConfigParsingTest, RbacPolicyWithoutRules) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [ {\"filter_name\": \"rbac\"} ]\n"
      "  } ]\n"
      "}";
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)->GetMethodParsedConfigVector(grpc_empty_slice());
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
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)->GetMethodParsedConfigVector(grpc_empty_slice());
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)->GetMethodParsedConfigVector(grpc_empty_slice());
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
      "    \"rbacPolicy\": [\n"
      "      { \"filter_name\": \"rbac-1\" },\n"
      "      { \"filter_name\": \"rbac-2\" },\n"
      "      { \"filter_name\": \"rbac-3\" }\n"
      "    ]"
      "  } ]\n"
      "}";
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)->GetMethodParsedConfigVector(grpc_empty_slice());
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy error:is not an array]")
      << service_config.status();
}

TEST(RbacServiceConfigParsingTest, BadRulesType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\"filter_name\": \"rbac\", \"rules\":1}]\n"
      "  } ]\n"
      "}";
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].rules error:is not an object]")
      << service_config.status();
}

TEST(RbacServiceConfigParsingTest, BadActionAndPolicyType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"filter_name\": \"rbac\",\n"
      "      \"rules\":{\n"
      "        \"action\":{},\n"
      "        \"policies\":123\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].rules.action "
            "error:is not a number; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies "
            "error:is not an object]")
      << service_config.status();
}

TEST(RbacServiceConfigParsingTest, MissingPermissionAndPrincipals) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"filter_name\": \"rbac\",\n"
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions error:field not present; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals error:field not present]")
      << service_config.status();
}

TEST(RbacServiceConfigParsingTest, EmptyPrincipalAndPermission) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"filter_name\": \"rbac\",\n"
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[0] error:no valid rule found; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[0] error:no valid id found]")
      << service_config.status();
}

TEST(RbacServiceConfigParsingTest, VariousPermissionsAndPrincipalsTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"filter_name\": \"rbac\",\n"
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)->GetMethodParsedConfigVector(grpc_empty_slice());
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
      "      \"filter_name\": \"rbac\",\n"
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[0].andRules error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[1].orRules error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[2].any error:is not a boolean; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[3].header error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[4].urlPath error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[5].destinationIp error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[6].destinationPort "
            "error:failed to parse non-negative number; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[7].metadata error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[8].notRule error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[9].requestedServerName error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[0].andIds error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[10].notId error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[1].orIds error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[2].any error:is not a boolean; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[3].authenticated error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[4].sourceIp error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[5].directRemoteIp error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[6].remoteIp error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[7].header error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[8].urlPath error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".principals[9].metadata error:is not an object]")
      << service_config.status();
}

TEST(RbacServiceConfigParsingTest, HeaderMatcherVariousTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"filter_name\": \"rbac\",\n"
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)->GetMethodParsedConfigVector(grpc_empty_slice());
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
      "      \"filter_name\": \"rbac\",\n"
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
      "              {\"header\":{\"name\":\"name\", \"containsMatch\":1}},\n"
      "              {\"header\":{\"name\":\"name\"}}\n"
      "            ],\n"
      "            \"principals\":[]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[0].header.exactMatch error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[0].header.invertMatch error:is not a boolean; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[1].header.safeRegexMatch error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[2].header.rangeMatch error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[3].header.presentMatch error:is not a boolean; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[4].header.prefixMatch error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[5].header.suffixMatch error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[6].header.containsMatch error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[7].header error:no valid matcher found]")
      << service_config.status();
}

TEST(RbacServiceConfigParsingTest, StringMatcherVariousTypes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      {}\n"
      "    ],\n"
      "    \"rbacPolicy\": [{\n"
      "      \"filter_name\": \"rbac\",\n"
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
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)->GetMethodParsedConfigVector(grpc_empty_slice());
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
      "      \"filter_name\": \"rbac\",\n"
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
      "              {\"requestedServerName\":{\"contains\":1}},\n"
      "              {\"requestedServerName\":{}}\n"
      "            ],\n"
      "            \"principals\":[]\n"
      "          }\n"
      "        }\n"
      "      }\n"
      "    } ]\n"
      "  } ]\n"
      "}";
  ChannelArgs args = ChannelArgs().Set(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[0].requestedServerName.exact error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[0].requestedServerName.ignoreCase "
            "error:is not a boolean; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[1].requestedServerName.prefix "
            "error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[2].requestedServerName.suffix "
            "error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[3].requestedServerName.safeRegex "
            "error:is not an object; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[4].requestedServerName.contains "
            "error:is not a string; "
            "field:methodConfig[0].rbacPolicy[0].rules.policies[\"policy\"]"
            ".permissions[5].requestedServerName error:no valid matcher found]")
      << service_config.status();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
