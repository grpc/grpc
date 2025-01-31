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

#include <grpc/grpc.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/client_channel_service_config.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/load_balancing/rls/rls.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

using internal::ClientChannelGlobalParsedConfig;
using internal::ClientChannelServiceConfigParser;

class RlsConfigParsingTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() { grpc_init(); }

  static void TearDownTestSuite() { grpc_shutdown_blocking(); }

  static std::string KeyBuilderMapString(
      const RlsLbConfig::KeyBuilderMap& key_builder_map) {
    std::vector<std::string> parts;
    parts.push_back("{");
    for (const auto& [key, key_builder] : key_builder_map) {
      parts.push_back(absl::StrCat("  \"", key, "\"={"));
      parts.push_back("    header_keys=[");
      for (const auto& [header_key, names] : key_builder.header_keys) {
        parts.push_back(absl::StrCat("      \"", header_key, "\"=[",
                                     absl::StrJoin(names, ", "), "]"));
      }
      parts.push_back("    ]");
      parts.push_back(
          absl::StrCat("    host_key=\"", key_builder.host_key, "\""));
      parts.push_back(
          absl::StrCat("    service_key=\"", key_builder.service_key, "\""));
      parts.push_back(
          absl::StrCat("    method_key=\"", key_builder.method_key, "\""));
      parts.push_back("    constant_keys={");
      for (const auto& [k, v] : key_builder.constant_keys) {
        parts.push_back(absl::StrCat("      \"", k, "\"=\"", v, "\""));
      }
      parts.push_back("    }");
      parts.push_back("  }");
    }
    parts.push_back("}");
    return absl::StrJoin(parts, "\n");
  }
};

TEST_F(RlsConfigParsingTest, MinimumValidConfig) {
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
      "      \"childPolicy\":[\n"
      "        {\"grpclb\":{}}\n"
      "      ],\n"
      "      \"childPolicyConfigTargetFieldName\":\"target\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  ASSERT_NE(*service_config, nullptr);
  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);
  ASSERT_EQ(lb_config->name(), RlsLbConfig::Name());
  auto* rls_lb_config = DownCast<RlsLbConfig*>(lb_config.get());
  EXPECT_THAT(
      rls_lb_config->key_builder_map(),
      ::testing::ElementsAre(::testing::Pair(
          "/foo/",
          ::testing::AllOf(
              ::testing::Field(&RlsLbConfig::KeyBuilder::header_keys,
                               ::testing::ElementsAre()),
              ::testing::Field(&RlsLbConfig::KeyBuilder::host_key, ""),
              ::testing::Field(&RlsLbConfig::KeyBuilder::service_key, ""),
              ::testing::Field(&RlsLbConfig::KeyBuilder::method_key, ""),
              ::testing::Field(&RlsLbConfig::KeyBuilder::constant_keys,
                               ::testing::ElementsAre())))))
      << KeyBuilderMapString(rls_lb_config->key_builder_map());
  EXPECT_EQ(rls_lb_config->lookup_service(), "rls.example.com:80");
  EXPECT_EQ(rls_lb_config->lookup_service_timeout(), Duration::Seconds(10));
  EXPECT_EQ(rls_lb_config->max_age(), rls_lb_config->kMaxMaxAge);
  EXPECT_EQ(rls_lb_config->stale_age(), rls_lb_config->kMaxMaxAge);
  EXPECT_EQ(rls_lb_config->cache_size_bytes(), 1);
  EXPECT_EQ(rls_lb_config->default_target(), "");
  EXPECT_EQ(rls_lb_config->rls_channel_service_config(), "");
  EXPECT_EQ(JsonDump(rls_lb_config->child_policy_config()),
            "[{\"grpclb\":{\"target\":\"fake_target_field_value\"}}]");
  EXPECT_EQ(rls_lb_config->child_policy_config_target_field_name(), "target");
  EXPECT_EQ(rls_lb_config->default_child_policy_parsed_config(), nullptr);
}

TEST_F(RlsConfigParsingTest, WithOptionalFields) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"lookupService\":\"rls.example.com:80\",\n"
      "        \"lookupServiceTimeout\":\"31s\",\n"
      "        \"defaultTarget\":\"foobar\",\n"
      "        \"cacheSizeBytes\":1,\n"
      "        \"maxAge\":\"182s\",\n"
      "        \"staleAge\":\"151s\",\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\"service\":\"foo\"},\n"
      "              {\"service\":\"bar\", \"method\":\"baz\"}\n"
      "            ],\n"
      "            \"headers\":[{\n"
      "              \"key\":\"k\",\n"
      "              \"names\":[\"n1\",\"n2\"]\n"
      "            }],\n"
      "            \"extraKeys\":{\n"
      "              \"host\":\"host\",\n"
      "              \"service\":\"service\",\n"
      "              \"method\":\"method\"\n"
      "            },\n"
      "            \"constantKeys\":{\n"
      "              \"quux\":\"mumble\"\n"
      "            }\n"
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
  ASSERT_NE(*service_config, nullptr);
  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);
  ASSERT_EQ(lb_config->name(), RlsLbConfig::Name());
  auto* rls_lb_config = DownCast<RlsLbConfig*>(lb_config.get());
  EXPECT_THAT(
      rls_lb_config->key_builder_map(),
      ::testing::UnorderedElementsAre(
          ::testing::Pair(
              "/foo/",
              ::testing::AllOf(
                  ::testing::Field(
                      &RlsLbConfig::KeyBuilder::header_keys,
                      ::testing::ElementsAre(::testing::Pair(
                          "k", ::testing::ElementsAre("n1", "n2")))),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::host_key, "host"),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::service_key,
                                   "service"),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::method_key,
                                   "method"),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::constant_keys,
                                   ::testing::ElementsAre(
                                       ::testing::Pair("quux", "mumble"))))),
          ::testing::Pair(
              "/bar/baz",
              ::testing::AllOf(
                  ::testing::Field(
                      &RlsLbConfig::KeyBuilder::header_keys,
                      ::testing::ElementsAre(::testing::Pair(
                          "k", ::testing::ElementsAre("n1", "n2")))),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::host_key, "host"),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::service_key,
                                   "service"),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::method_key,
                                   "method"),
                  ::testing::Field(&RlsLbConfig::KeyBuilder::constant_keys,
                                   ::testing::ElementsAre(
                                       ::testing::Pair("quux", "mumble")))))))
      << KeyBuilderMapString(rls_lb_config->key_builder_map());
  EXPECT_EQ(rls_lb_config->lookup_service(), "rls.example.com:80");
  EXPECT_EQ(rls_lb_config->lookup_service_timeout(), Duration::Seconds(31));
  EXPECT_EQ(rls_lb_config->max_age(), Duration::Seconds(182));
  EXPECT_EQ(rls_lb_config->stale_age(), Duration::Seconds(151));
  EXPECT_EQ(rls_lb_config->cache_size_bytes(), 1);
  EXPECT_EQ(rls_lb_config->default_target(), "foobar");
  EXPECT_EQ(rls_lb_config->rls_channel_service_config(),
            "{\"loadBalancingPolicy\":\"ROUND_ROBIN\"}");
  EXPECT_EQ(JsonDump(rls_lb_config->child_policy_config()),
            "[{\"grpclb\":{\"target\":\"foobar\"}}]");
  EXPECT_EQ(rls_lb_config->child_policy_config_target_field_name(), "target");
  ASSERT_NE(rls_lb_config->default_child_policy_parsed_config(), nullptr);
  EXPECT_EQ(rls_lb_config->default_child_policy_parsed_config()->name(),
            "grpclb");
}

TEST_F(RlsConfigParsingTest, ClampMaxAge) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"lookupService\":\"rls.example.com:80\",\n"
      "        \"cacheSizeBytes\":1,\n"
      "        \"maxAge\":\"301s\",\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\"service\":\"foo\"}\n"
      "            ]\n"
      "          }\n"
      "        ]\n"
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
  ASSERT_NE(*service_config, nullptr);
  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);
  ASSERT_EQ(lb_config->name(), RlsLbConfig::Name());
  auto* rls_lb_config = DownCast<RlsLbConfig*>(lb_config.get());
  EXPECT_EQ(rls_lb_config->max_age(), rls_lb_config->kMaxMaxAge);
  EXPECT_EQ(rls_lb_config->stale_age(), rls_lb_config->kMaxMaxAge);
}

TEST_F(RlsConfigParsingTest, ClampStaleAgeToMaxAge) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"lookupService\":\"rls.example.com:80\",\n"
      "        \"cacheSizeBytes\":1,\n"
      "        \"maxAge\":\"182s\",\n"
      "        \"staleAge\":\"200s\",\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\"service\":\"foo\"}\n"
      "            ]\n"
      "          }\n"
      "        ]\n"
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
  ASSERT_NE(*service_config, nullptr);
  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);
  ASSERT_EQ(lb_config->name(), RlsLbConfig::Name());
  auto* rls_lb_config = DownCast<RlsLbConfig*>(lb_config.get());
  EXPECT_EQ(rls_lb_config->max_age(), Duration::Seconds(182));
  EXPECT_EQ(rls_lb_config->stale_age(), Duration::Seconds(182));
}

TEST_F(RlsConfigParsingTest, DoNotClampMaxAgeIfStaleAgeIsSet) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"rls_experimental\":{\n"
      "      \"routeLookupConfig\":{\n"
      "        \"lookupService\":\"rls.example.com:80\",\n"
      "        \"cacheSizeBytes\":1,\n"
      "        \"maxAge\":\"350s\",\n"
      "        \"staleAge\":\"310s\",\n"
      "        \"grpcKeybuilders\":[\n"
      "          {\n"
      "            \"names\":[\n"
      "              {\"service\":\"foo\"}\n"
      "            ]\n"
      "          }\n"
      "        ]\n"
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
  ASSERT_NE(*service_config, nullptr);
  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);
  ASSERT_EQ(lb_config->name(), RlsLbConfig::Name());
  auto* rls_lb_config = DownCast<RlsLbConfig*>(lb_config.get());
  // Allow maxAge to exceed 300s if staleAge is set, but still clamp
  // staleAge to 300s.
  EXPECT_EQ(rls_lb_config->max_age(), Duration::Seconds(350));
  EXPECT_EQ(rls_lb_config->stale_age(), Duration::Seconds(300));
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
          "errors validating RLS LB policy config: ["
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
            "error:errors validating RLS LB policy config: ["
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
          "errors validating RLS LB policy config: ["
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
      ::testing::HasSubstr("errors validating RLS LB policy config: ["
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
            "error:errors validating RLS LB policy config: ["
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
          "errors validating RLS LB policy config: ["
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
          "errors validating RLS LB policy config: ["
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
      "        \"defaultTarget\":\"\",\n"
      "        \"staleAge\":\"2s\",\n"
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
          "errors validating RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:"
          "must be greater than 0; "
          "field:routeLookupConfig.defaultTarget "
          "error:must be non-empty if set; "
          "field:routeLookupConfig.grpcKeybuilders error:field not present; "
          "field:routeLookupConfig.lookupService error:"
          "must be valid gRPC target URI; "
          "field:routeLookupConfig.maxAge error:"
          "must be set if staleAge is set]"))
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
          "errors validating RLS LB policy config: ["
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
          "errors validating RLS LB policy config: ["
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
          "errors validating RLS LB policy config: ["
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
      "                \"names\":1,\n"
      "                \"requiredMatch\":1\n"
      "              },\n"
      "              {\n"
      "                \"names\":[],\n"
      "                \"requiredMatch\":true\n"
      "              },\n"
      "              {\n"
      "                \"key\":\"\",\n"
      "                \"names\":[1, \"\"]\n"
      "              }\n"
      "            ],\n"
      "            \"extraKeys\":{\n"
      "              \"host\": \"\",\n"
      "              \"service\": \"\",\n"
      "              \"method\": \"\"\n"
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
          "errors validating RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].constantKeys[\"\"] "
          "error:key must be non-empty; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.host "
          "error:must be non-empty if set; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.method "
          "error:must be non-empty if set; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.service "
          "error:must be non-empty if set; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[0] "
          "error:is not an object; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[1].key "
          "error:is not a string; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[1].names "
          "error:is not an array; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[1].requiredMatch "
          "error:is not a boolean; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[2].key "
          "error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[2].names "
          "error:must be non-empty; "
          "field:routeLookupConfig.grpcKeybuilders[0].headers[2].requiredMatch "
          "error:must not be present; "
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
          "errors validating RLS LB policy config: ["
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
          "errors validating RLS LB policy config: ["
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
          "errors validating RLS LB policy config: ["
          "field:childPolicy error:field not present; "
          "field:childPolicyConfigTargetFieldName error:field not present; "
          "field:routeLookupConfig.cacheSizeBytes error:field not present; "
          "field:routeLookupConfig.grpcKeybuilders[1] "
          "error:duplicate entry for \"/foo/bar\"; "
          "field:routeLookupConfig.lookupService error:field not present]"))
      << service_config.status();
}

TEST_F(RlsConfigParsingTest, KeyBuilderDuplicateKeys) {
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
      "            ],\n"
      "            \"headers\":[\n"
      "              {\n"
      "                \"key\":\"host\",\n"
      "                \"names\":[\"n1\"]\n"
      "              },\n"
      "              {\n"
      "                \"key\":\"service\",\n"
      "                \"names\":[\"n1\"]\n"
      "              },\n"
      "              {\n"
      "                \"key\":\"method\",\n"
      "                \"names\":[\"n1\"]\n"
      "              },\n"
      "              {\n"
      "                \"key\":\"constant\",\n"
      "                \"names\":[\"n1\"]\n"
      "              }\n"
      "            ],\n"
      "            \"extraKeys\":{\n"
      "              \"host\":\"host\",\n"
      "              \"service\":\"service\",\n"
      "              \"method\":\"method\"\n"
      "            },\n"
      "            \"constantKeys\":{\n"
      "              \"constant\":\"mumble\"\n"
      "            }\n"
      "          }\n"
      "        ]\n"
      "      },\n"
      "      \"childPolicy\":[\n"
      "        {\"grpclb\":{}}\n"
      "      ],\n"
      "      \"childPolicyConfigTargetFieldName\":\"target\"\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr(
          "errors validating RLS LB policy config: ["
          "field:routeLookupConfig.grpcKeybuilders[0].constantKeys["
          "\"constant\"] error:duplicate key \"constant\"; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.host "
          "error:duplicate key \"host\"; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.method "
          "error:duplicate key \"method\"; "
          "field:routeLookupConfig.grpcKeybuilders[0].extraKeys.service "
          "error:duplicate key \"service\"]"))
      << service_config.status();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
