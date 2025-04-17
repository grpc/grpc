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
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/config_vars.h"
#include "src/proto/grpc/testing/echo.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
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

  void SetXdsResourcesForServer(BalancerServerThread* balancer, size_t backend,
                                absl::string_view server_name = "",
                                absl::string_view authority = "") {
    Listener listener = default_listener_;
    RouteConfiguration route_config = default_route_config_;
    Cluster cluster = default_cluster_;
    // Default server uses default resources when no authority, to enable using
    // more test framework functions.
    if (!server_name.empty() || !authority.empty()) {
      auto get_resource_name = [&](absl::string_view resource_type) {
        absl::string_view stripped_resource_type =
            absl::StripPrefix(resource_type, "type.googleapis.com/");
        if (authority.empty()) {
          if (resource_type == kLdsTypeUrl) return std::string(server_name);
          return absl::StrFormat("%s_%s", stripped_resource_type, server_name);
        }
        return absl::StrFormat("xdstp://%s/%s/%s", authority,
                               stripped_resource_type, server_name);
      };
      listener.set_name(get_resource_name(kLdsTypeUrl));
      cluster.set_name(get_resource_name(kCdsTypeUrl));
      cluster.mutable_eds_cluster_config()->set_service_name(
          get_resource_name(kEdsTypeUrl));
      route_config.set_name(get_resource_name(kRdsTypeUrl));
      route_config.mutable_virtual_hosts(0)
          ->mutable_routes(0)
          ->mutable_route()
          ->set_cluster(cluster.name());
    }
    SetListenerAndRouteConfiguration(balancer, listener, route_config);
    balancer->ads_service()->SetCdsResource(cluster);
    balancer->ads_service()->SetEdsResource(BuildEdsResource(
        EdsResourceArgs(
            {{"locality0", CreateEndpointsForBackends(backend, backend + 1)}}),
        cluster.eds_cluster_config().service_name()));
  }

  void ExpectBackendCall(EchoTestService::Stub* stub, int backend,
                         grpc_core::DebugLocation location) {
    ClientContext context;
    EchoRequest request;
    EchoResponse response;
    RpcOptions().SetupRpc(&context, &request);
    Status status = stub->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message() << "\n"
                             << location.file() << ':' << location.line();
    EXPECT_EQ(1U, backends_[backend]->backend_service()->request_count())
        << "\n"
        << location.file() << ':' << location.line();
  }

 protected:
  std::unique_ptr<BalancerServerThread> fallback_balancer_;
};

TEST_P(XdsFallbackTest, FallbackAndRecover) {
  auto broken_balancer = CreateAndStartBalancer("Broken balancer");
  broken_balancer->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  InitClient(XdsBootstrapBuilder().SetServers({
      balancer_->target(),
      broken_balancer->target(),
      fallback_balancer_->target(),
  }));
  // Primary xDS server has backends_[0] configured and fallback server has
  // backends_[1]
  CreateAndStartBackends(2);
  SetXdsResourcesForServer(balancer_.get(), 0);
  SetXdsResourcesForServer(fallback_balancer_.get(), 1);
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
  InitClient(XdsBootstrapBuilder().SetServers(
      {balancer_->target(), fallback_balancer_->target()}));
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  fallback_balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrFormat(
          "empty address list \\(LDS resource server.example.com: "
          "xDS channel for server localhost:%d: "
          "xDS call failed with no responses received; "
          "status: RESOURCE_EXHAUSTED: test forced ADS stream failure "
          "\\(node ID:xds_end2end_test\\)\\)",
          fallback_balancer_->port()));
}

TEST_P(XdsFallbackTest, UsesCachedResourcesAfterFailure) {
  constexpr absl::string_view kServerName2 = "server2.example.com";
  InitClient(XdsBootstrapBuilder().SetServers(
      {balancer_->target(), fallback_balancer_->target()}));
  // 4 backends - cross product of two data plane targets and two balancers
  CreateAndStartBackends(4);
  SetXdsResourcesForServer(balancer_.get(), 0);
  SetXdsResourcesForServer(fallback_balancer_.get(), 1);
  SetXdsResourcesForServer(balancer_.get(), 2, kServerName2);
  SetXdsResourcesForServer(fallback_balancer_.get(), 3, kServerName2);
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  auto channel = CreateChannel(0, std::string(kServerName2).c_str());
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  // server2.example.com is configured from the fallback server
  ExpectBackendCall(stub.get(), 3, DEBUG_LOCATION);
  // Calling server.example.com still uses cached value
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 2);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 0);
}

TEST_P(XdsFallbackTest, PerAuthorityFallback) {
  auto fallback_balancer2 = CreateAndStartBalancer("Fallback for Authority2");
  // Use cleanup in case test assertion fails
  auto balancer2_cleanup =
      absl::MakeCleanup([&]() { fallback_balancer2->Shutdown(); });
  grpc_core::testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_FEDERATION");
  const char* kAuthority1 = "xds1.example.com";
  const char* kAuthority2 = "xds2.example.com";
  constexpr absl::string_view kServer1Name = "server1.example.com";
  constexpr absl::string_view kServer2Name = "server2.example.com";
  // Authority1 uses balancer_ and fallback_balancer_
  // Authority2 uses balancer_ and fallback_balancer2
  XdsBootstrapBuilder builder;
  builder.SetServers({balancer_->target()});
  builder.AddAuthority(kAuthority1,
                       {balancer_->target(), fallback_balancer_->target()});
  builder.AddAuthority(kAuthority2,
                       {balancer_->target(), fallback_balancer2->target()});
  InitClient(builder);
  CreateAndStartBackends(4);
  SetXdsResourcesForServer(fallback_balancer_.get(), 0, kServer1Name,
                           kAuthority1);
  SetXdsResourcesForServer(fallback_balancer2.get(), 1, kServer2Name,
                           kAuthority2);
  SetXdsResourcesForServer(balancer_.get(), 2, kServer1Name, kAuthority1);
  SetXdsResourcesForServer(balancer_.get(), 3, kServer2Name, kAuthority2);
  // Primary balancer is down, using the fallback servers
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  // Create second channel to new target URI and send 1 RPC.
  auto authority1_stub = grpc::testing::EchoTestService::NewStub(CreateChannel(
      /*failover_timeout_ms=*/0, std::string(kServer1Name).c_str(),
      kAuthority1));
  auto authority2_stub = grpc::testing::EchoTestService::NewStub(CreateChannel(
      /*failover_timeout_ms=*/0, std::string(kServer2Name).c_str(),
      kAuthority2));
  ExpectBackendCall(authority1_stub.get(), 0, DEBUG_LOCATION);
  ExpectBackendCall(authority2_stub.get(), 1, DEBUG_LOCATION);
  // Primary balancer is up, its data will be used now.
  balancer_->ads_service()->ClearADSFailure();
  auto deadline =
      absl::Now() + (absl::Seconds(5) * grpc_test_slowdown_factor());
  while (absl::Now() < deadline &&
         (backends_[2]->backend_service()->request_count() == 0 ||
          backends_[3]->backend_service()->request_count() == 0)) {
    ClientContext context;
    EchoRequest request;
    EchoResponse response;
    RpcOptions().SetupRpc(&context, &request);
    Status status = authority1_stub->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();
    ClientContext context2;
    EchoRequest request2;
    EchoResponse response2;
    RpcOptions().SetupRpc(&context2, &request2);
    status = authority2_stub->Echo(&context2, request2, &response2);
    EXPECT_TRUE(status.ok()) << status.error_message();
  }
  ASSERT_LE(1U, backends_[2]->backend_service()->request_count());
  ASSERT_LE(1U, backends_[3]->backend_service()->request_count());
}

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
