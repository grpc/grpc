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

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/useful.h"
#include "src/core/resolver/xds/xds_config.h"
#include "test/core/filters/filter_test.h"

namespace grpc_core {
namespace {

class GcpAuthenticationFilterTest : public FilterTest<GcpAuthenticationFilter> {
 protected:
  static RefCountedPtr<ServiceConfig> MakeServiceConfig(
      absl::string_view service_config_json) {
    auto service_config = ServiceConfigImpl::Create(
        ChannelArgs()
            .Set(GRPC_ARG_PARSE_GCP_AUTHENTICATION_METHOD_CONFIG, true),
        service_config_json);
    CHECK(service_config.ok()) << service_config.status();
    return *service_config;
  }

  static RefCountedPtr<const XdsConfig> MakeXdsConfig(
      absl::string_view cluster, absl::string_view filter_instance_name,
      std::unique_ptr<XdsStructMetadataValue> audience_metadata) {
    auto cluster_resource = std::make_shared<XdsClusterResource>();
    if (audience_metadata != nullptr) {
      cluster_resource->metadata.Insert(filter_instance_name,
                                        std::move(audience_metadata));
    }
    auto xds_config = MakeRefCounted<XdsConfig>();
    xds_config->clusters[cluster].emplace(
        std::move(cluster_resource), nullptr, "");
    return xds_config;
  }

  ChannelArgs MakeChannelArgs(
      absl::string_view service_config_json, absl::string_view cluster,
      absl::string_view filter_instance_name,
      std::unique_ptr<XdsStructMetadataValue> audience_metadata) {
    auto service_config = MakeServiceConfig(service_config_json);
    auto xds_config = MakeXdsConfig(cluster, filter_instance_name,
                                    std::move(audience_metadata));
    return ChannelArgs()
               .SetObject(std::move(service_config))
               .SetObject(std::move(xds_config));
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
      std::move(channel_args), ChannelFilter::Args(/*instance_id=*/0));
  EXPECT_TRUE(filter.ok()) << filter.status();
}

TEST_F(GcpAuthenticationFilterTest, CreateFailsWithoutServiceConfig) {
  constexpr absl::string_view kClusterName = "foo";
  constexpr absl::string_view kFilterInstanceName = "gcp_authn_filter";
  auto channel_args = ChannelArgs().SetObject(
      MakeXdsConfig(kClusterName, kFilterInstanceName, nullptr));
  auto filter = GcpAuthenticationFilter::Create(
      std::move(channel_args), ChannelFilter::Args(/*instance_id=*/0));
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
      std::move(channel_args), ChannelFilter::Args(/*instance_id=*/0));
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
      std::move(channel_args), ChannelFilter::Args(/*instance_id=*/0));
  EXPECT_EQ(filter.status(),
            absl::InvalidArgumentError(
                "gcp_auth: xds config not found in channel args"));
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
// FIXME: not working
  Call call(MakeChannel(std::move(channel_args)).value());
  call.Start(call.NewClientMetadata());
  EXPECT_EVENT(Finished(
      &call, HasMetadataResult(absl::OkStatus())));
  Step();
}

#if 0
TEST_F(GcpAuthenticationFilterTest, CallCredsFails) {
  Call call(MakeChannelWithCallCredsResult(
      absl::UnauthenticatedError("access denied")));
  call.Start(call.NewClientMetadata({{":authority", target()}}));
  EXPECT_EVENT(Finished(
      &call, HasMetadataResult(absl::UnauthenticatedError("access denied"))));
  Step();
}

TEST_F(GcpAuthenticationFilterTest, RewritesInvalidStatusFromCallCreds) {
  Call call(MakeChannelWithCallCredsResult(absl::AbortedError("nope")));
  call.Start(call.NewClientMetadata({{":authority", target()}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::InternalError(
                                   "Illegal status code from call credentials; "
                                   "original status: ABORTED: nope"))));
  Step();
}
#endif

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
