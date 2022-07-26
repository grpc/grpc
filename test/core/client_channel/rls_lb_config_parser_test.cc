//
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
//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "test/core/util/test_config.h"

// A regular expression to enter referenced or child errors.
#define CHILD_ERROR_TAG ".*children.*"

namespace grpc_core {
namespace {

class RlsConfigParsingTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() { grpc_init(); }

  static void TearDownTestSuite() { grpc_shutdown_blocking(); }
};

TEST_F(RlsConfigParsingTest, ValidConfig) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"lookupService\":\"rls.example.com:80\",\n"
      "        \"cacheSizeBytes\":1,\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\"service\":\"foo\"}\n"
      "            ]\n"
      "          }\n"
      "        ]\n"
      "      },\n"
      "      \"routeLookupChannelServiceConfig\": {\n"
      "        \"loadBalancingPolicy\": \"ROUND_ROBIN\"\n"
      "      },\n"
      "      \"childPolicy\":[\n"
      "        {\"unknown\":{}},\n"  // Okay, since the next one exists.
      "        {\"grpclb\":{}}\n"
      "      ],\n"
      "      \"childPolicyConfigTargetFieldName\":\"target\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_NE(*service_config, nullptr);
}

//
// top-level fields
//

TEST_F(RlsConfigParsingTest, TopLevelRequiredFieldsMissing) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupConfig error:does not exist.*"
          "field:childPolicyConfigTargetFieldName error:does not exist.*"
          "field:childPolicy error:does not exist"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, TopLevelFieldsWrongTypes) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":1,\n"
      "      \"routeLookupChannelServiceConfig\": 1,\n"
      "      \"childPolicy\":1,\n"
      "      \"childPolicyConfigTargetFieldName\":1\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupConfig error:type should be OBJECT.*"
          "field:routeLookupChannelServiceConfig error:type should be OBJECT.*"
          "field:childPolicyConfigTargetFieldName error:type should be STRING.*"
          "field:childPolicy error:type should be ARRAY"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, TopLevelFieldsInvalidValues) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"childPolicy\":[\n"
      "        {\"unknown\":{}}\n"
      "      ],\n"
      "      \"childPolicyConfigTargetFieldName\":\"\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:childPolicyConfigTargetFieldName error:must be non-empty.*"
          "field:childPolicy" CHILD_ERROR_TAG
          "No known policies in list: unknown"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, InvalidChildPolicyConfig) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"childPolicy\":[\n"
      "        {\"grpclb\":{\"childPolicy\":1}}\n"
      "      ],\n"
      "      \"childPolicyConfigTargetFieldName\":\"serviceName\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "errors parsing RLS LB policy config" CHILD_ERROR_TAG
                  "field:childPolicy" CHILD_ERROR_TAG
                  "errors parsing grpclb LB policy config: \\["
                  "error parsing childPolicy field: type should be array\\]"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, InvalidRlsChannelServiceConfig) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupChannelServiceConfig\": {\n"
      "        \"loadBalancingPolicy\": \"unknown\"\n"
      "      },\n"
      "      \"childPolicy\":[\n"
      "        {\"grpclb\":{}}\n"
      "      ],\n"
      "      \"childPolicyConfigTargetFieldName\":\"serviceName\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupChannelServiceConfig" CHILD_ERROR_TAG
          "Service config parsing errors: \\["
          "error parsing client channel global parameters" CHILD_ERROR_TAG
          "field:loadBalancingPolicy error:Unknown lb policy"))
      << service_config.status();
}

//
// routeLookupConfig fields
//

TEST_F(RlsConfigParsingTest, RouteLookupConfigRequiredFieldsMissing) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "errors parsing RLS LB policy config" CHILD_ERROR_TAG
                  "field:routeLookupConfig" CHILD_ERROR_TAG
                  "field:grpcKeybuilders error:does not exist.*"
                  "field:lookupService error:does not exist"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, RouteLookupConfigFieldsWrongTypes) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":1,\n"
      "        \"name\":1,\n"
      "        \"lookupService\":1,\n"
      "        \"lookupServiceTimeout\":{},\n"
      "        \"maxAge\":{},\n"
      "        \"staleAge\":{},\n"
      "        \"cacheSizeBytes\":\"xxx\",\n"
      "        \"defaultTarget\":1\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "errors parsing RLS LB policy config" CHILD_ERROR_TAG
                  "field:routeLookupConfig" CHILD_ERROR_TAG
                  "field:grpcKeybuilders error:type should be ARRAY.*"
                  "field:lookupService error:type should be STRING.*"
                  "field:maxAge error:type should be STRING.*"
                  "field:staleAge error:type should be STRING.*"
                  "field:cacheSizeBytes error:failed to parse.*"
                  "field:defaultTarget error:type should be STRING"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, RouteLookupConfigFieldsInvalidValues) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"lookupService\":\"\",\n"
      "        \"cacheSizeBytes\":0\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "errors parsing RLS LB policy config" CHILD_ERROR_TAG
                  "field:routeLookupConfig" CHILD_ERROR_TAG
                  "field:lookupService error:must be valid gRPC target URI.*"
                  "field:cacheSizeBytes error:must be greater than 0"))
      << service_config.status();
}

//
// grpcKeybuilder fields
//

TEST_F(RlsConfigParsingTest, GrpcKeybuilderRequiredFieldsMissing) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "errors parsing RLS LB policy config" CHILD_ERROR_TAG
                  "field:routeLookupConfig" CHILD_ERROR_TAG
                  "field:grpcKeybuilders" CHILD_ERROR_TAG
                  "index:0" CHILD_ERROR_TAG "field:names error:does not exist"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, GrpcKeybuilderWrongFieldTypes) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":1,\n"
      "            \"headers\":1,\n"
      "            \"extraKeys\":1,\n"
      "            \"constantKeys\":1\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupConfig" CHILD_ERROR_TAG
          "field:grpcKeybuilders" CHILD_ERROR_TAG "index:0" CHILD_ERROR_TAG
          "field:names error:type should be ARRAY.*"
          "field:headers error:type should be ARRAY.*"
          "field:extraKeys error:type should be OBJECT.*"
          "field:constantKeys error:type should be OBJECT"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, GrpcKeybuilderInvalidValues) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[],\n"
      "            \"extraKeys\":{\n"
      "              \"host\":1,\n"
      "              \"service\":1,\n"
      "              \"method\":1\n"
      "            },\n"
      "            \"constantKeys\":{\n"
      "              \"key\":1\n"
      "            }\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "errors parsing RLS LB policy config" CHILD_ERROR_TAG
                  "field:routeLookupConfig" CHILD_ERROR_TAG
                  "field:grpcKeybuilders" CHILD_ERROR_TAG
                  "index:0" CHILD_ERROR_TAG "field:names error:list is empty.*"
                  "field:extraKeys" CHILD_ERROR_TAG
                  "field:host error:type should be STRING.*"
                  "field:service error:type should be STRING.*"
                  "field:method error:type should be STRING.*"
                  "field:constantKeys" CHILD_ERROR_TAG
                  "field:key error:type should be STRING"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, GrpcKeybuilderInvalidHeaders) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"headers\":[\n"
      "              1,\n"
      "              {\n"
      "                \"key\":1,\n"
      "                \"names\":1\n"
      "              },\n"
      "              {\n"
      "                \"names\":[]\n"
      "              },\n"
      "              {\n"
      "                \"key\":\"\",\n"
      "                \"names\":[1, \"\"]\n"
      "              }\n"
      "            ],\n"
      "            \"extraKeys\":{\n"
      "              \"host\": \"\"\n"
      "            },\n"
      "            \"constantKeys\":{\n"
      "              \"\":\"foo\"\n"
      "            }\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupConfig" CHILD_ERROR_TAG
          "field:grpcKeybuilders" CHILD_ERROR_TAG "index:0" CHILD_ERROR_TAG
          "field:headers index:0 error:type should be OBJECT.*"
          "field:headers index:1" CHILD_ERROR_TAG
          "field:key error:type should be STRING.*"
          "field:names error:type should be ARRAY.*"
          "field:headers index:2" CHILD_ERROR_TAG
          "field:key error:does not exist.*"
          "field:names error:list is empty.*"
          "field:headers index:3" CHILD_ERROR_TAG
          "field:key error:must be non-empty.*"
          "field:names index:0 error:type should be STRING.*"
          "field:names index:1 error:header name must be non-empty.*"
          "field:extraKeys" CHILD_ERROR_TAG
          "field:host error:must be non-empty.*"
          "field:constantKeys" CHILD_ERROR_TAG "error:keys must be non-empty"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, GrpcKeybuilderNameWrongFieldTypes) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              1,\n"
      "              {\n"
      "                \"service\":1,\n"
      "                \"method\":1\n"
      "              }\n"
      "            ]\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupConfig" CHILD_ERROR_TAG
          "field:grpcKeybuilders" CHILD_ERROR_TAG "index:0" CHILD_ERROR_TAG
          "field:names index:0 error:type should be OBJECT.*"
          "field:names index:1" CHILD_ERROR_TAG
          "field:service error:type should be STRING.*"
          "field:method error:type should be STRING"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, DuplicateMethodNamesInSameKeyBuilder) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\n"
      "                \"service\":\"foo\",\n"
      "                \"method\":\"bar\"\n"
      "              },\n"
      "              {\n"
      "                \"service\":\"foo\",\n"
      "                \"method\":\"bar\"\n"
      "              }\n"
      "            ]\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupConfig" CHILD_ERROR_TAG
          "field:grpcKeybuilders" CHILD_ERROR_TAG "index:0" CHILD_ERROR_TAG
          "field:names error:duplicate entry for /foo/bar"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, DuplicateMethodNamesInDifferentKeyBuilders) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\n"
      "                \"service\":\"foo\",\n"
      "                \"method\":\"bar\"\n"
      "              }\n"
      "            ]\n"
      "          },\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\n"
      "                \"service\":\"foo\",\n"
      "                \"method\":\"bar\"\n"
      "              }\n"
      "            ]\n"
      "          }\n"
      "        ]\n"
      "      }\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex(
          "errors parsing RLS LB policy config" CHILD_ERROR_TAG
          "field:routeLookupConfig" CHILD_ERROR_TAG
          "field:grpcKeybuilders" CHILD_ERROR_TAG "index:1" CHILD_ERROR_TAG
          "field:names error:duplicate entry for /foo/bar"))
      << service_config.status();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
