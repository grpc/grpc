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

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "test/core/util/test_config.h"

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
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig error:field not present]"))
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
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:loadBalancingConfig "
            "error:errors validing RLS LB policy config: ["
            "field:childPolicy error:is not an array; "
            "field:childPolicyConfigTargetFieldName error:is not a string; "
            "field:routeLookupChannelServiceConfig error:is not an object; "
            "field:routeLookupConfig error:is not an object]]")
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
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:No known policies in list: unknown; "
          "field:childPolicyConfigTargetFieldName error:must be non-empty; "
          "field:routeLookupConfig error:field not present]"))
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
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr("errors validing RLS LB policy config: ["
                           "field:childPolicy error:"
                           "errors validating grpclb LB policy config: ["
                           "field:childPolicy error:type should be array]; "
                           "field:routeLookupConfig error:field not present]"))
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
  EXPECT_EQ(service_config.status().message(),
            "errors validating service config: ["
            "field:loadBalancingConfig "
            "error:errors validing RLS LB policy config: ["
            "field:routeLookupChannelServiceConfig.loadBalancingPolicy "
            "error:unknown LB policy \"unknown\"; "
            "field:routeLookupConfig error:field not present]]")
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
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders error:field not present; "
          "field:routeLookupConfig.lookupService error:field not present]"))
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
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:"
          "failed to parse number; "
          "field:routeLookupConfig.defaultTarget error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders error:is not an array; "
          "field:routeLookupConfig.lookupService error:is not a string; "
          "field:routeLookupConfig.lookupServiceTimeout error:is not a string; "
          "field:routeLookupConfig.maxAge error:is not a string; "
          "field:routeLookupConfig.staleAge error:is not a string]"))
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
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:"
          "must be greater than 0; "
          "field:routeLookupConfig.grpcKeybuilders error:field not present; "
          "field:routeLookupConfig.lookupService error:"
          "must be valid gRPC target URI]"))
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
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].names error:"
          "field not present; "
          "field:routeLookupConfig.lookupService error:field not present]"))
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
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].constantKeys error:"
          "is not an object; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys error:"
          "is not an object; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers error:"
          "is not an array; "
          "field:routeLookupConfig.grpcKeybuilders[0].names error:"
          "is not an array; "
          "field:routeLookupConfig.lookupService error:field not present]"))
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
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].constantKeys[\"key\"] "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.host "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.method "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.service "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].names "
          "error:must be non-empty; "
          "field:routeLookupConfig.lookupService error:field not present]"))
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
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].constantKeys[\"\"] "
          "error:key must be non-empty; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.host "
          "error:must be non-empty if set; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[0] "
          "error:is not an object; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[1].key "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[1].names "
          "error:is not an array; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[2].key "
          "error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[2].names "
          "error:must be non-empty; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[3].key "
          "error:must be non-empty; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[3].names[0] "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[3].names[1] "
          "error:must be non-empty; "
          "field:routeLookupConfig.grpcKeybuilders[0].names "
          "error:field not present; "
          "field:routeLookupConfig.lookupService error:field not present]"))
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
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].names[0] "
          "error:is not an object; "
          "field:routeLookupConfig.grpcKeybuilders[0].names[1].method "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].names[1].service "
          "error:is not a string; "
          "field:routeLookupConfig.lookupService error:field not present]"))
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
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0] "
          "error:duplicate entry for \"/foo/bar\"; "
          "field:routeLookupConfig.lookupService error:field not present]"))
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
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validing RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[1] "
          "error:duplicate entry for \"/foo/bar\"; "
          "field:routeLookupConfig.lookupService error:field not present]"))
      << service_config.status();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
