// Copyright 2024 gRPC authors.
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

#include "src/core/ext/filters/gcp_authentication/gcp_authentication_filter.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/security_context.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/call/gcp_service_account_identity/gcp_service_account_identity_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/resolver/xds/xds_config.h"
#include "src/core/resolver/xds/xds_resolver_attributes.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "test/core/filters/filter_test.h"

namespace grpc_core {
namespace {

class GcpAuthenticationFilterTest : public FilterTest<GcpAuthenticationFilter> {
 protected:
  static RefCountedPtr<ServiceConfig> MakeServiceConfig(
      absl::string_view service_config_json) {
    auto service_config = ServiceConfigImpl::Create(
        ChannelArgs().Set(GRPC_ARG_PARSE_GCP_AUTHENTICATION_METHOD_CONFIG,
                          true),
        service_config_json);
    CHECK(service_config.ok()) << service_config.status();
    return *service_config;
  }

  static RefCountedPtr<const XdsConfig> MakeXdsConfig(
      absl::string_view cluster, absl::string_view filter_instance_name,
      std::unique_ptr<XdsMetadataValue> audience_metadata) {
    auto xds_config = MakeRefCounted<XdsConfig>();
    if (!cluster.empty()) {
      auto cluster_resource = std::make_shared<XdsClusterResource>();
      if (audience_metadata != nullptr) {
        cluster_resource->metadata.Insert(filter_instance_name,
                                          std::move(audience_metadata));
      }
      xds_config->clusters[cluster].emplace(std::move(cluster_resource),
                                            nullptr, "");
    }
    return xds_config;
  }

  static RefCountedPtr<const XdsConfig> MakeXdsConfigWithCluster(
      absl::string_view cluster,
      absl::StatusOr<XdsConfig::ClusterConfig> cluster_config) {
    auto xds_config = MakeRefCounted<XdsConfig>();
    xds_config->clusters[cluster] = std::move(cluster_config);
    return xds_config;
  }

  ChannelArgs MakeChannelArgs(
      absl::string_view service_config_json, absl::string_view cluster,
      absl::string_view filter_instance_name,
      std::unique_ptr<XdsMetadataValue> audience_metadata) {
    auto service_config = MakeServiceConfig(service_config_json);
    auto xds_config = MakeXdsConfig(cluster, filter_instance_name,
                                    std::move(audience_metadata));
    return ChannelArgs()
        .SetObject(std::move(service_config))
        .SetObject(std::move(xds_config));
  }

  static RefCountedPtr<grpc_call_credentials> GetCallCreds(const Call& call) {
    auto* security_ctx = DownCast<grpc_client_security_context*>(
        call.arena()->GetContext<SecurityContext>());
    if (security_ctx == nullptr) return nullptr;
    return security_ctx->creds;
  }
};

TEST_F(GcpAuthenticationFilterTest, CreateSucceeds) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args = MakeChannelArgs(kServiceConfigJson, kClusterName,
                                      kFilterInstanceName, nullptr);
  auto filter = GcpAuthenticationFilter::Create(
      channel_args, ChannelFilter::Args(/*instance_id=*/0));
  EXPECT_TRUE(filter.ok()) << filter.status();
}

TEST_F(GcpAuthenticationFilterTest, CreateFailsWithoutServiceConfig) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  auto channel_args = ChannelArgs().SetObject(
      MakeXdsConfig(kClusterName, kFilterInstanceName, nullptr));
  auto filter = GcpAuthenticationFilter::Create(
      channel_args, ChannelFilter::Args(/*instance_id=*/0));
  EXPECT_EQ(filter.status(),
            absl::InvalidArgumentError(
                "gcp_auth: no service config in channel args"));
}

TEST_F(GcpAuthenticationFilterTest,
       CreateFailsFilterConfigMissingFromServiceConfig) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  constexpr absl::string_view kServiceConfigJson = "{}";
  auto channel_args = MakeChannelArgs(kServiceConfigJson, kClusterName,
                                      kFilterInstanceName, nullptr);
  auto filter = GcpAuthenticationFilter::Create(
      channel_args, ChannelFilter::Args(/*instance_id=*/0));
  EXPECT_EQ(filter.status(),
            absl::InvalidArgumentError(
                "gcp_auth: filter instance ID not found in filter config"));
}

TEST_F(GcpAuthenticationFilterTest, CreateFailsXdsConfigNotFoundInChannelArgs) {
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args =
      ChannelArgs().SetObject(MakeServiceConfig(kServiceConfigJson));
  auto filter = GcpAuthenticationFilter::Create(
      channel_args, ChannelFilter::Args(/*instance_id=*/0));
  EXPECT_EQ(filter.status(),
            absl::InvalidArgumentError(
                "gcp_auth: xds config not found in channel args"));
}

TEST_F(GcpAuthenticationFilterTest, FailsCallIfNoXdsClusterAttribute) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args = MakeChannelArgs(kServiceConfigJson, kClusterName,
                                      kFilterInstanceName, nullptr);
  Call call(MakeChannel(channel_args).value());
  call.arena()->New<ServiceConfigCallData>(call.arena());
  call.Start(call.NewClientMetadata());
  EXPECT_EVENT(Finished(
      &call,
      HasMetadataResult(absl::InternalError(
          "GCP authentication filter: call has no xDS cluster attribute"))));
  Step();
  // Call creds were not set.
  EXPECT_EQ(GetCallCreds(call), nullptr);
}

TEST_F(GcpAuthenticationFilterTest, NoOpIfClusterAttributeHasWrongPrefix) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  constexpr absl::string_view kAudience = "bar";
  auto channel_args = MakeChannelArgs(
      kServiceConfigJson, kClusterName, kFilterInstanceName,
      std::make_unique<XdsGcpAuthnAudienceMetadataValue>(kAudience));
  Call call(MakeChannel(channel_args).value());
  auto* service_config_call_data =
      call.arena()->New<ServiceConfigCallData>(call.arena());
  XdsClusterAttribute xds_cluster_attribute(kClusterName);
  service_config_call_data->SetCallAttribute(&xds_cluster_attribute);
  EXPECT_EVENT(Started(&call, ::testing::_));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata({{"grpc-status", "0"}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  Step();
  // Call creds were not set.
  EXPECT_EQ(GetCallCreds(call), nullptr);
}

TEST_F(GcpAuthenticationFilterTest, FailsCallIfClusterNotPresentInXdsConfig) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args = MakeChannelArgs(kServiceConfigJson, /*cluster=*/"",
                                      /*filter_instance_name=*/"", nullptr);
  Call call(MakeChannel(channel_args).value());
  auto* service_config_call_data =
      call.arena()->New<ServiceConfigCallData>(call.arena());
  std::string cluster_name_with_prefix = absl::StrCat("cluster:", kClusterName);
  XdsClusterAttribute xds_cluster_attribute(cluster_name_with_prefix);
  service_config_call_data->SetCallAttribute(&xds_cluster_attribute);
  call.Start(call.NewClientMetadata());
  EXPECT_EVENT(
      Finished(&call, HasMetadataResult(absl::InternalError(absl::StrCat(
                          "GCP authentication filter: xDS cluster ",
                          kClusterName, " not found in XdsConfig")))));
  Step();
  // Call creds were not set.
  EXPECT_EQ(GetCallCreds(call), nullptr);
}

TEST_F(GcpAuthenticationFilterTest, FailsCallIfClusterNotOkayInXdsConfig) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args = ChannelArgs()
                          .SetObject(MakeServiceConfig(kServiceConfigJson))
                          .SetObject(MakeXdsConfigWithCluster(
                              kClusterName, absl::UnavailableError("nope")));
  Call call(MakeChannel(channel_args).value());
  auto* service_config_call_data =
      call.arena()->New<ServiceConfigCallData>(call.arena());
  std::string cluster_name_with_prefix = absl::StrCat("cluster:", kClusterName);
  XdsClusterAttribute xds_cluster_attribute(cluster_name_with_prefix);
  service_config_call_data->SetCallAttribute(&xds_cluster_attribute);
  call.Start(call.NewClientMetadata());
  EXPECT_EVENT(Finished(
      &call, HasMetadataResult(absl::UnavailableError(absl::StrCat(
                 "GCP authentication filter: CDS resource unavailable for ",
                 kClusterName)))));
  Step();
  // Call creds were not set.
  EXPECT_EQ(GetCallCreds(call), nullptr);
}

TEST_F(GcpAuthenticationFilterTest,
       FailsCallIfClusterResourceMissingInXdsConfig) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args =
      ChannelArgs()
          .SetObject(MakeServiceConfig(kServiceConfigJson))
          .SetObject(MakeXdsConfigWithCluster(
              kClusterName, XdsConfig::ClusterConfig(nullptr, nullptr, "")));
  Call call(MakeChannel(channel_args).value());
  auto* service_config_call_data =
      call.arena()->New<ServiceConfigCallData>(call.arena());
  std::string cluster_name_with_prefix = absl::StrCat("cluster:", kClusterName);
  XdsClusterAttribute xds_cluster_attribute(cluster_name_with_prefix);
  service_config_call_data->SetCallAttribute(&xds_cluster_attribute);
  call.Start(call.NewClientMetadata());
  EXPECT_EVENT(Finished(
      &call,
      HasMetadataResult(absl::InternalError(absl::StrCat(
          "GCP authentication filter: CDS resource not present for cluster ",
          kClusterName)))));
  Step();
  // Call creds were not set.
  EXPECT_EQ(GetCallCreds(call), nullptr);
}

TEST_F(GcpAuthenticationFilterTest, NoOpIfClusterHasNoAudience) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args = MakeChannelArgs(kServiceConfigJson, kClusterName,
                                      kFilterInstanceName, nullptr);
  Call call(MakeChannel(channel_args).value());
  auto* service_config_call_data =
      call.arena()->New<ServiceConfigCallData>(call.arena());
  std::string cluster_name_with_prefix = absl::StrCat("cluster:", kClusterName);
  XdsClusterAttribute xds_cluster_attribute(cluster_name_with_prefix);
  service_config_call_data->SetCallAttribute(&xds_cluster_attribute);
  EXPECT_EVENT(Started(&call, ::testing::_));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata({{"grpc-status", "0"}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  Step();
  // Call creds were not set.
  EXPECT_EQ(GetCallCreds(call), nullptr);
}

TEST_F(GcpAuthenticationFilterTest, FailsCallIfAudienceMetadataWrongType) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  auto channel_args =
      MakeChannelArgs(kServiceConfigJson, kClusterName, kFilterInstanceName,
                      std::make_unique<XdsStructMetadataValue>(Json()));
  Call call(MakeChannel(channel_args).value());
  auto* service_config_call_data =
      call.arena()->New<ServiceConfigCallData>(call.arena());
  std::string cluster_name_with_prefix = absl::StrCat("cluster:", kClusterName);
  XdsClusterAttribute xds_cluster_attribute(cluster_name_with_prefix);
  service_config_call_data->SetCallAttribute(&xds_cluster_attribute);
  call.Start(call.NewClientMetadata());
  EXPECT_EVENT(Finished(
      &call, HasMetadataResult(absl::UnavailableError(absl::StrCat(
                 "GCP authentication filter: audience metadata in wrong format "
                 "for cluster ",
                 kClusterName)))));
  Step();
  // Call creds were not set.
  EXPECT_EQ(GetCallCreds(call), nullptr);
}

TEST_F(GcpAuthenticationFilterTest, SetsCallCredsIfClusterHasAudience) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  constexpr absl::string_view kServiceConfigJson =
      "{\n"
      "  \"gcp_authentication\": [\n"
      "    {\"filter_instance_name\": \"gcp_authn_filter\"}\n"
      "  ]\n"
      "}";
  constexpr absl::string_view kAudience = "bar";
  auto channel_args = MakeChannelArgs(
      kServiceConfigJson, kClusterName, kFilterInstanceName,
      std::make_unique<XdsGcpAuthnAudienceMetadataValue>(kAudience));
  Call call(MakeChannel(channel_args).value());
  auto* service_config_call_data =
      call.arena()->New<ServiceConfigCallData>(call.arena());
  std::string cluster_name_with_prefix = absl::StrCat("cluster:", kClusterName);
  XdsClusterAttribute xds_cluster_attribute(cluster_name_with_prefix);
  service_config_call_data->SetCallAttribute(&xds_cluster_attribute);
  EXPECT_EVENT(Started(&call, ::testing::_));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata({{"grpc-status", "0"}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  Step();
  // Call creds were set with the right audience.
  auto call_creds = GetCallCreds(call);
  ASSERT_NE(call_creds, nullptr);
  EXPECT_EQ(call_creds->type(),
            GcpServiceAccountIdentityCallCredentials::Type());
  EXPECT_EQ(call_creds->debug_string(),
            absl::StrCat("GcpServiceAccountIdentityCallCredentials(", kAudience,
                         ")"));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
