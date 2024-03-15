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
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/endpoint.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.grpc.pb.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/end2end/xds/xds_utils.h"

#ifndef DISABLED_XDS_PROTO_IN_CC

#include "src/cpp/server/csds/csds.h"
#include "src/proto/grpc/testing/xds/v3/csds.grpc.pb.h"

namespace grpc {
namespace testing {
namespace {

class XdsClientTest : public XdsEnd2endTest {
 public:
  XdsClientTest()
      : fallback_balancer_(CreateAndStartBalancer("Fallback Balancer #1")),
        fallback_balancer2_(CreateAndStartBalancer("Fallback Balancer #2")) {}

  void SetUp() override {
    // Overrides SetUp from a base class so we can call InitClient per-test case
  }

  void TearDown() override {
    fallback_balancer2_->Shutdown();
    fallback_balancer_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

 protected:
  std::unique_ptr<BalancerServerThread> fallback_balancer_;
  std::unique_ptr<BalancerServerThread> fallback_balancer2_;
};

TEST_P(XdsClientTest, FallbackToSecondaryAndTertiary) {
  InitClient(XdsBootstrapBuilder().SetServers({
      absl::StrCat("localhost:", balancer_->port()),
      absl::StrCat("localhost:", fallback_balancer_->port()),
  }));
  CreateAndStartBackends(1);
  SetListenerAndRouteConfiguration(fallback_balancer_.get(), default_listener_,
                                   default_route_config_);
  fallback_balancer_->ads_service()->SetCdsResource(default_cluster_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  fallback_balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const std::string kErrorMessage = "test forced ADS stream failure";
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  auto status = SendRpc();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsClientTest, PrimarySecondaryNotAvailable) {
  InitClient(XdsBootstrapBuilder().SetServers({
      absl::StrCat("localhost:", balancer_->port()),
      absl::StrCat("localhost:", fallback_balancer_->port()),
  }));
  CreateAndStartBackends(1);
  SetListenerAndRouteConfiguration(fallback_balancer_.get(), default_listener_,
                                   default_route_config_);
  fallback_balancer_->ads_service()->SetCdsResource(default_cluster_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  fallback_balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const std::string kErrorMessage = "test forced ADS stream failure";
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  fallback_balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  auto status = SendRpc();
  ASSERT_FALSE(status.ok()) << status.error_message();
  ASSERT_EQ(
      status.error_message(),
      absl::StrFormat(
          "server.example.com: UNAVAILABLE: xDS channel for server "
          "localhost:%d: xDS call failed with no responses received; "
          "status: RESOURCE_EXHAUSTED: test forced ADS stream failure (node "
          "ID:xds_end2end_test)",
          fallback_balancer_->port()));
}

TEST_P(XdsClientTest, DISABLED_AuthorityServers) {}
TEST_P(XdsClientTest, DISABLED_UsesCachedResourcesAfterFailure) {}

TEST_P(XdsClientTest, DISABLED_FallForward) {
  InitClient(XdsBootstrapBuilder().SetServers({
      absl::StrCat("localhost:", balancer_->port()),
      absl::StrCat("localhost:", fallback_balancer_->port()),
  }));
  CreateAndStartBackends(1);
  SetListenerAndRouteConfiguration(fallback_balancer_.get(), default_listener_,
                                   default_route_config_);
  fallback_balancer_->ads_service()->SetCdsResource(default_cluster_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  fallback_balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const std::string kErrorMessage = "test forced ADS stream failure";
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  auto status = SendRpc();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsClientTest, DISABLED_FallbackToBrokenToFixed) {}
TEST_P(XdsClientTest, DISABLED_FallbackAfterSetup) {}

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsClientTest,
                         ::testing::Values(XdsTestType().set_bootstrap_source(
                             XdsTestType::kBootstrapFromEnvVar)),
                         &XdsTestType::Name);

}  // namespace
}  // namespace testing
}  // namespace grpc

#endif  // DISABLED_XDS_PROTO_IN_CC

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
