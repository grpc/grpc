// Copyright 2017 gRPC authors.
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
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_format.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>

#include "src/core/client_channel/backup_poller.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/env.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/endpoint.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.grpc.pb.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/end2end/xds/xds_utils.h"

namespace grpc {
namespace testing {
namespace {

constexpr char const* kErrorMessage = "test forced ADS stream failure";

class XdsFallbackTest : public XdsEnd2endTest {
 public:
  XdsFallbackTest()
      : fallback_balancer_(CreateAndStartBalancer("Fallback Balancer")) {}

  void SetUp() override {
    // Overrides SetUp from a base class so we can call InitClient per-test case
  }

  void TearDown() override {
    fallback_balancer_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  // Sets resources and returns target name
  std::string SetXdsResourcesForTarget(BalancerServerThread* balancer,
                                       size_t backend, int target_id = 0,
                                       absl::string_view authority = "") {
    Listener listener = default_listener_;
    RouteConfiguration route_config = default_route_config_;
    Cluster cluster = default_cluster_;
    // Server 0 uses default resources when no authority, to enable using more
    // test framework functions
    if (target_id > 0 && authority.empty()) {
      listener.set_name(absl::StrFormat("server%d.example.com", target_id));
      cluster.set_name(absl::StrFormat("cluster%d", target_id));
      cluster.mutable_eds_cluster_config()->set_service_name(
          absl::StrFormat("eds%d", target_id));
      route_config.set_name(absl::StrFormat("route%d", target_id));
      route_config.mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster(cluster.name());
    } else if (!authority.empty()) {
    }
    SetListenerAndRouteConfiguration(balancer, listener, route_config);
    balancer->ads_service()->SetCdsResource(cluster);
    balancer->ads_service()->SetEdsResource(BuildEdsResource(
        EdsResourceArgs(
            {{"locality0", CreateEndpointsForBackends(backend, backend + 1)}}),
        cluster.eds_cluster_config().service_name()));
    return listener.name();
  }

 protected:
  std::unique_ptr<BalancerServerThread> fallback_balancer_;
};

TEST_P(XdsFallbackTest, FallbackAndRecover) {
  auto broken_balancer = CreateAndStartBalancer("Broken balancer");
  broken_balancer->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  InitClient(XdsBootstrapBuilder().SetServers({
      absl::StrCat("localhost:", balancer_->port()),
      absl::StrCat("localhost:", broken_balancer->port()),
      absl::StrCat("localhost:", fallback_balancer_->port()),
  }));
  // Primary xDS server has backends_[0] configured and fallback server has
  // backends_[1]
  CreateAndStartBackends(2);
  SetXdsResourcesForTarget(balancer_.get(), 0, 0);
  SetXdsResourcesForTarget(fallback_balancer_.get(), 1, 0);
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  // Primary server down, fallback server data is used (backends_[1])
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 1);
  // Primary server is back. backends_[0] will be used when the data makes it
  // all way to the client
  balancer_->ads_service()->ClearADSFailure();
  WaitForBackend(DEBUG_LOCATION, 0);
  broken_balancer->Shutdown();
}

TEST_P(XdsFallbackTest, PrimarySecondaryNotAvailable) {
  InitClient(XdsBootstrapBuilder().SetServers({
      absl::StrCat("localhost:", balancer_->port()),
      absl::StrCat("localhost:", fallback_balancer_->port()),
  }));
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  fallback_balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrFormat(
          "server.example.com: UNAVAILABLE: xDS channel for server "
          "localhost:%d: xDS call failed with no responses received; "
          "status: RESOURCE_EXHAUSTED: test forced ADS stream failure \\(node "
          "ID:xds_end2end_test\\)",
          fallback_balancer_->port()));
}

TEST_P(XdsFallbackTest, UsesCachedResourcesAfterFailure) {
  InitClient(XdsBootstrapBuilder().SetServers({
      absl::StrCat("localhost:", balancer_->port()),
      absl::StrCat("localhost:", fallback_balancer_->port()),
  }));
  // 4 backends - cross product of two data plane targets and two balancers
  CreateAndStartBackends(4);
  SetXdsResourcesForTarget(balancer_.get(), 0, 0);
  SetXdsResourcesForTarget(fallback_balancer_.get(), 1, 0);
  auto server_name = SetXdsResourcesForTarget(balancer_.get(), 2, 2);
  SetXdsResourcesForTarget(fallback_balancer_.get(), 3, 2);
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  auto channel = CreateChannel(0, server_name.c_str());
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  ClientContext context;
  EchoRequest request;
  EchoResponse response;
  // server2.example.com is configured from the fallback server
  Status status = stub->Echo(&context, request, &response);
  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(backends_[2]->backend_service()->request_count(), 0);
  EXPECT_EQ(backends_[3]->backend_service()->request_count(), 1);
  // Calling server.example.com still uses cached value
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 2);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 0);
}

TEST_P(XdsFallbackTest, PerAuthorityServers) {
  auto authority_xds2_fallback = CreateAndStartBalancer("Authority B Fallback");
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  InitClient(XdsBootstrapBuilder().AddAuthority(
      "xds1.example.com",
      {
          absl::StrCat("localhost:", balancer_->port()),
          absl::StrCat("localhost:", fallback_balancer_->port()),
      }));
  InitClient(XdsBootstrapBuilder().AddAuthority(
      "xds2.example.com",
      {
          absl::StrCat("localhost:", balancer_->port()),
          absl::StrCat("localhost:", authority_xds2_fallback->port()),
      }));

  CreateAndStartBackends(2);

  authority_xds2_fallback->Shutdown();
}

TEST_P(XdsFallbackTest, DISABLED_FallbackAfterSetup) {}

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsFallbackTest,
                         ::testing::Values(XdsTestType().set_bootstrap_source(
                             XdsTestType::kBootstrapFromEnvVar)),
                         &XdsTestType::Name);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
