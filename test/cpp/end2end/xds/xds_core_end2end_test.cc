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

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/end2end/xds/xds_server.h"

namespace grpc {
namespace testing {
namespace {

using ClientStats = LrsServiceImpl::ClientStats;

//
// XdsClientTest - basic tests of XdsClient functionality
//

using XdsClientTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsClientTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

// Tests that the client can handle resource wrapped in a Resource message.
TEST_P(XdsClientTest, ResourceWrappedInResourceMessage) {
  CreateAndStartBackends(1);
  balancer_->ads_service()->set_wrap_resources(true);
  const size_t kNumRpcsPerAddress = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Make sure that trying to connect works without a call.
  channel_->GetState(true /* try_to_connect */);
  // We need to wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION);
  // Send kNumRpcsPerAddress RPCs per server.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcsPerAddress * backends_.size());
  // Each backend should have gotten 100 requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // Check LB policy name for the channel.
  EXPECT_EQ("xds_cluster_manager_experimental",
            channel_->GetLoadBalancingPolicyName());
}

TEST_P(XdsClientTest, ResourceTypeVersionPersistsAcrossStreamRestarts) {
  CreateAndStartBackends(2);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for backends to come online.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  // Stop balancer.
  balancer_->Shutdown();
  // Expect minimum version 1 for all resource types.
  balancer_->ads_service()->SetCheckVersionCallback(
      [&](absl::string_view resource_type, int version) {
        EXPECT_GE(version, 1) << "resource_type: " << resource_type;
      });
  // Update backend, just so we can be sure that the client has
  // reconnected to the balancer.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Restart balancer.
  balancer_->Start();
  // Make sure client has reconnected.
  WaitForAllBackends(DEBUG_LOCATION, 1, 2);
}

// Tests that we restart all xDS requests when we reestablish the ADS call.
TEST_P(XdsClientTest, RestartsRequestsUponReconnection) {
  CreateAndStartBackends(2);
  // Manually configure use of RDS.
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancer_->ads_service()->SetLdsResource(listener);
  balancer_->ads_service()->SetRdsResource(default_route_config_);
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  // Now shut down and restart the balancer.  When the client
  // reconnects, it should automatically restart the requests for all
  // resource types.
  balancer_->Shutdown();
  balancer_->Start();
  // Make sure things are still working.
  CheckRpcSendOk(DEBUG_LOCATION, 100);
  // Populate new EDS resource.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  balancer_->ads_service()->SetRdsResource(new_route_config);
  // Wait for all new backends to be used.
  WaitForAllBackends(DEBUG_LOCATION, 1, 2);
}

TEST_P(XdsClientTest, XdsStreamErrorPropagation) {
  const std::string kErrorMessage = "test forced ADS stream failure";
  balancer_->ads_service()->ForceADSFailure(
      Status(StatusCode::RESOURCE_EXHAUSTED, kErrorMessage));
  auto status = SendRpc();
  LOG(INFO) << "XdsStreamErrorPropagation test: RPC got error: code="
            << status.error_code()
            << " message=" << status.error_message().c_str();
  EXPECT_THAT(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_THAT(status.error_message(), ::testing::HasSubstr(kErrorMessage));
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("(node ID:xds_end2end_test)"));
}

//
// XdsServerTlsTest: xDS server using TlsCreds
//

class XdsServerTlsTest : public XdsEnd2endTest {
 protected:
  XdsServerTlsTest()
      : XdsEnd2endTest(/*balancer_credentials=*/CreateTlsServerCredentials()) {}

  void SetUp() override {
    InitClient(MakeBootstrapBuilder().SetXdsChannelCredentials(
                   "tls", absl::StrCat("{\"ca_certificate_file\": \"",
                                       kCaCertPath, "\"}")),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"foo.test.google.fr");
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsServerTlsTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsServerTlsTest, Basic) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// GlobalXdsClientTest - tests that need to run with a global XdsClient
// (this is the default in production)
//

using GlobalXdsClientTest = XdsEnd2endTest;

// Get bootstrap from env var, so that there's a global XdsClient.
INSTANTIATE_TEST_SUITE_P(XdsTest, GlobalXdsClientTest,
                         ::testing::Values(XdsTestType().set_bootstrap_source(
                             XdsTestType::kBootstrapFromEnvVar)),
                         &XdsTestType::Name);

TEST_P(GlobalXdsClientTest, MultipleChannelsSameTargetShareXdsClient) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  // Create second channel and tell it to connect to the same server.
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kServerName);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(channel2->WaitForConnected(grpc_timeout_seconds_to_deadline(1)));
  // Make sure there's only one client connected.
  EXPECT_EQ(1UL, balancer_->ads_service()->clients().size());
}

TEST_P(GlobalXdsClientTest,
       MultipleChannelsDifferentTargetDoNotShareXdsClient) {
  CreateAndStartBackends(1);
  const char* kNewServerName = "new-server.example.com";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  // Create second channel and tell it to connect to kNewServerName.
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kNewServerName);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(channel2->WaitForConnected(grpc_timeout_seconds_to_deadline(1)));
  // Make sure there are two clients connected.
  EXPECT_EQ(2UL, balancer_->ads_service()->clients().size());
}

TEST_P(
    GlobalXdsClientTest,
    MultipleChannelsShareXdsClientWithResourceUpdateAfterOneChannelGoesAway) {
  CreateAndStartBackends(2);
  // Test for https://github.com/grpc/grpc/issues/28468. Makes sure that the
  // XdsClient properly handles the case where there are multiple watchers on
  // the same resource and one of them unsubscribes.
  const char* kNewServerName = "new-server.example.com";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Create second channel and tell it to connect to kNewServerName.
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kNewServerName);
  channel2->GetState(/*try_to_connect=*/true);
  ASSERT_TRUE(channel2->WaitForConnected(grpc_timeout_seconds_to_deadline(1)));
  // Now, destroy the new channel, send an EDS update to use a different backend
  // and test that the channel switches to that backend.
  channel2.reset();
  // This sleep is needed to be able to reproduce the bug and to give time for
  // the buggy unsubscription to take place.
  // TODO(yashykt): Figure out a way to do this without the sleep.
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(10));
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  })));
  WaitForBackend(DEBUG_LOCATION, 1);
}

// Tests that the NACK for multiple bad LDS resources includes both errors.
// This needs to use xDS server as this is the only scenario when XdsClient
// is shared.
TEST_P(GlobalXdsClientTest, MultipleBadLdsResources) {
  CreateBackends(2, true);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  auto listener = default_server_listener_;
  listener.clear_address();
  listener.set_name(GetServerListenerName(backends_[0]->port()));
  balancer_->ads_service()->SetLdsResource(listener);
  backends_[0]->Start();
  auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_EQ(
      response_state->error_message,
      absl::StrFormat(
          "xDS response validation errors: ["
          "resource index 0: "
          "grpc/server?xds.resource.listening_address=127.0.0.1:%lu: "
          "INVALID_ARGUMENT: Listener has neither address nor ApiListener]",
          backends_[0]->port()));
  listener = default_server_listener_;
  listener.clear_address();
  listener.set_name(GetServerListenerName(backends_[1]->port()));
  balancer_->ads_service()->SetLdsResource(listener);
  backends_[1]->Start();
  constexpr absl::string_view kMessageFormat =
      "xDS response validation errors: ["
      "resource index 0: "
      "grpc/server?xds.resource.listening_address=127.0.0.1:%d: "
      "INVALID_ARGUMENT: Listener has neither address nor "
      "ApiListener; "
      "resource index 1: "
      "grpc/server?xds.resource.listening_address=127.0.0.1:%d: "
      "INVALID_ARGUMENT: Listener has neither address nor "
      "ApiListener"
      "]";
  const std::string expected_message1 = absl::StrFormat(
      kMessageFormat, backends_[0]->port(), backends_[1]->port());
  const std::string expected_message2 = absl::StrFormat(
      kMessageFormat, backends_[1]->port(), backends_[0]->port());
  response_state = WaitForNack(
      DEBUG_LOCATION, [&]() -> std::optional<AdsServiceImpl::ResponseState> {
        auto response = balancer_->ads_service()->lds_response_state();
        if (response.has_value() &&
            response->state == AdsServiceImpl::ResponseState::NACKED) {
          if (response->error_message == expected_message1 ||
              response->error_message == expected_message2) {
            return response;
          }
          LOG(INFO) << "non-matching NACK message: "
                    << response->error_message.c_str();
        }
        return std::nullopt;
      });
  EXPECT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
}

// Tests that we don't trigger does-not-exist callbacks for a resource
// that was previously valid but is updated to be invalid.
TEST_P(GlobalXdsClientTest, InvalidListenerStillExistsIfPreviouslyCached) {
  CreateAndStartBackends(1);
  // Set up valid resources and check that the channel works.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION);
  // Now send an update changing the Listener to be invalid.
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state =
      WaitForLdsNack(DEBUG_LOCATION, RpcOptions(), StatusCode::OK);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_EQ(response_state->error_message,
            "xDS response validation errors: ["
            "resource index 0: server.example.com: "
            "INVALID_ARGUMENT: Listener has neither address nor ApiListener]");
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// TimeoutTest - tests xDS initial timeout handling
//

class TimeoutTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    InitClient(MakeBootstrapBuilder(), /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/2000);
  }
};

// Enable RDS, so that we can test all resource types.
// Run with bootstrap from env var so that multiple channels share the same
// XdsClient (needed for testing the timeout for the 2nd LDS and RDS resource).
INSTANTIATE_TEST_SUITE_P(
    XdsTest, TimeoutTest,
    ::testing::Values(
        XdsTestType().set_enable_rds_testing().set_bootstrap_source(
            XdsTestType::kBootstrapFromEnvVar)),
    &XdsTestType::Name);

TEST_P(TimeoutTest, LdsServerIgnoresRequest) {
  balancer_->ads_service()->IgnoreResourceType(kLdsTypeUrl);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("empty address list \\(LDS resource ", kServerName,
                   ": does not exist \\(node ID:xds_end2end_test\\)\\)"),
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, LdsResourceNotPresentInRequest) {
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("empty address list \\(LDS resource ", kServerName,
                   ": does not exist \\(node ID:xds_end2end_test\\)\\)"),
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, LdsSecondResourceNotPresentInRequest) {
  ASSERT_NE(GetParam().bootstrap_source(),
            XdsTestType::kBootstrapFromChannelArg)
      << "This test cannot use bootstrap from channel args, because it "
         "needs two channels to use the same XdsClient instance.";
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(4000));
  // Create second channel for a new server name.
  // This should fail because there is no LDS resource for this server name.
  const char* kNewServerName = "new-server.example.com";
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kNewServerName);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  EchoResponse response;
  RpcOptions rpc_options;
  rpc_options.set_timeout_ms(4000).SetupRpc(&context, &request);
  auto status =
      SendRpcMethod(stub2.get(), rpc_options, &context, request, &response);
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
  EXPECT_EQ(status.error_message(),
            absl::StrCat("empty address list (LDS resource ", kNewServerName,
                         ": does not exist (node ID:xds_end2end_test))"));
}

TEST_P(TimeoutTest, RdsServerIgnoresRequest) {
  balancer_->ads_service()->IgnoreResourceType(kRdsTypeUrl);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("empty address list \\(RDS resource ",
                   kDefaultRouteConfigurationName,
                   ": does not exist \\(node ID:xds_end2end_test\\)\\)"),
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, RdsResourceNotPresentInRequest) {
  balancer_->ads_service()->UnsetResource(kRdsTypeUrl,
                                          kDefaultRouteConfigurationName);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("empty address list \\(RDS resource ",
                   kDefaultRouteConfigurationName,
                   ": does not exist \\(node ID:xds_end2end_test\\)\\)"),
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, RdsSecondResourceNotPresentInRequest) {
  ASSERT_NE(GetParam().bootstrap_source(),
            XdsTestType::kBootstrapFromChannelArg)
      << "This test cannot use bootstrap from channel args, because it "
         "needs two channels to use the same XdsClient instance.";
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(4000));
  // Add listener for 2nd channel, but no RDS resource.
  const char* kNewServerName = "new-server.example.com";
  const char* kNewRouteConfigName = "rds_resource_does_not_exist";
  Listener listener = default_listener_;
  listener.set_name(kNewServerName);
  HttpConnectionManager http_connection_manager =
      ClientHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kNewRouteConfigName);
  rds->mutable_config_source()->mutable_self();
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  balancer_->ads_service()->SetLdsResource(listener);
  // Create second channel for a new server name.
  // This should fail because the LDS resource points to a non-existent RDS
  // resource.
  auto channel2 = CreateChannel(/*failover_timeout_ms=*/0, kNewServerName);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  EchoResponse response;
  RpcOptions rpc_options;
  rpc_options.set_timeout_ms(4000).SetupRpc(&context, &request);
  auto status =
      SendRpcMethod(stub2.get(), rpc_options, &context, request, &response);
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
  EXPECT_EQ(
      status.error_message(),
      absl::StrCat("empty address list (RDS resource ", kNewRouteConfigName,
                   ": does not exist (node ID:xds_end2end_test))"));
}

TEST_P(TimeoutTest, CdsServerIgnoresRequest) {
  balancer_->ads_service()->IgnoreResourceType(kCdsTypeUrl);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("CDS resource ", kDefaultClusterName,
                   ": does not exist \\(node ID:xds_end2end_test\\)"),
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, CdsResourceNotPresentInRequest) {
  balancer_->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("CDS resource ", kDefaultClusterName,
                   ": does not exist \\(node ID:xds_end2end_test\\)"),
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, CdsSecondResourceNotPresentInRequest) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(4000));
  // Change route config to point to non-existing cluster.
  const char* kNewClusterName = "new_cluster_name";
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  balancer_->ads_service()->SetRdsResource(route_config);
  // New cluster times out.
  // May need to wait a bit for the change to propagate to the client.
  SendRpcsUntilFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat("CDS resource ", kNewClusterName,
                   ": does not exist \\(node ID:xds_end2end_test\\)"),
      /*timeout_ms=*/30000, RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, EdsServerIgnoresRequest) {
  balancer_->ads_service()->IgnoreResourceType(kEdsTypeUrl);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      "no children in weighted_target policy \\(EDS resource "
      "eds_service_name: does not exist \\(node ID:xds_end2end_test\\)\\)",
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, EdsResourceNotPresentInRequest) {
  // No need to remove EDS resource, since the test suite does not add it
  // by default.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      "no children in weighted_target policy \\(EDS resource "
      "eds_service_name: does not exist \\(node ID:xds_end2end_test\\)\\)",
      RpcOptions().set_timeout_ms(4000));
}

TEST_P(TimeoutTest, EdsSecondResourceNotPresentInRequest) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(4000));
  // New cluster that points to a non-existent EDS resource.
  const char* kNewClusterName = "new_cluster_name";
  Cluster cluster = default_cluster_;
  cluster.set_name(kNewClusterName);
  cluster.mutable_eds_cluster_config()->set_service_name(
      "eds_service_name_does_not_exist");
  balancer_->ads_service()->SetCdsResource(cluster);
  // Now add a route pointing to the new cluster.
  RouteConfiguration route_config = default_route_config_;
  auto* route = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  *route_config.mutable_virtual_hosts(0)->add_routes() = *route;
  route->mutable_match()->set_path("/grpc.testing.EchoTestService/Echo1");
  route->mutable_route()->set_cluster(kNewClusterName);
  balancer_->ads_service()->SetRdsResource(route_config);
  // New EDS resource times out.
  // May need to wait a bit for the RDS change to propagate to the client.
  SendRpcsUntilFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      "no children in weighted_target policy \\("
      "EDS resource eds_service_name_does_not_exist: "
      "does not exist \\(node ID:xds_end2end_test\\)\\)",
      /*timeout_ms=*/30000,
      RpcOptions().set_rpc_method(METHOD_ECHO1).set_timeout_ms(4000));
}

TEST_P(TimeoutTest, ServerDoesNotResendAfterAdsStreamRestart) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(4000));
  // Stop balancer.
  balancer_->Shutdown();
  // Expect minimum version 1 for all resource types.
  balancer_->ads_service()->SetCheckVersionCallback(
      [&](absl::string_view resource_type, int version) {
        EXPECT_GE(version, 1) << "resource_type: " << resource_type;
      });
  // Tell balancer not to reply to the requests.
  balancer_->ads_service()->IgnoreResourceType(kLdsTypeUrl);
  balancer_->ads_service()->IgnoreResourceType(kRdsTypeUrl);
  balancer_->ads_service()->IgnoreResourceType(kCdsTypeUrl);
  balancer_->ads_service()->IgnoreResourceType(kEdsTypeUrl);
  // Restart balancer.
  balancer_->Start();
  // Send RPCs for long enough to cover the ADS stream restart delay,
  // the stream restart, and then the resulting timeout period, just to
  // be sure that the channel continues to use the resources from before
  // the restart.
  absl::Time deadline =
      absl::Now() + (absl::Seconds(30) * grpc_test_slowdown_factor());
  do {
    CheckRpcSendOk(DEBUG_LOCATION);
  } while (absl::Now() < deadline);
}

//
// BootstrapSourceTest - tests different bootstrap sources
//

using BootstrapSourceTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(
    XdsTest, BootstrapSourceTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromFile)),
    &XdsTestType::Name);

TEST_P(BootstrapSourceTest, Vanilla) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Increase timeout, since kBootstrapFromFile takes more time on busy
  // test machines.  (We've seen at least one occurrence where it's
  // taken over 10 seconds.)
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(15000));
}

//
// XdsFederationTest - tests xDS federation
//

class XdsFederationTest : public XdsEnd2endTest {
 protected:
  XdsFederationTest() : authority_balancer_(CreateAndStartBalancer()) {}

  void SetUp() override {
    // Each test will use a slightly different bootstrap config,
    // so SetUp() is intentionally empty here, and the real
    // setup (calling of InitClient()) is moved into each test.
  }

  void TearDown() override {
    authority_balancer_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  std::unique_ptr<BalancerServerThread> authority_balancer_;
};

// Get bootstrap from env var, so that there's a global XdsClient.
// Runs with RDS so that we know all resource types work properly.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsFederationTest,
    ::testing::Values(
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing()),
    &XdsTestType::Name);

// Channel is created with URI "xds:server.example.com".
// Bootstrap config default client listener template uses new-style name with
// authority "xds.example.com".
TEST_P(XdsFederationTest, FederationTargetNoAuthorityWithResourceTemplate) {
  const char* kAuthority = "xds.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "new_edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "new_cluster_name";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.SetClientDefaultListenerResourceNameTemplate(kNewListenerTemplate);
  builder.AddAuthority(
      kAuthority, absl::StrCat("localhost:", authority_balancer_->port()),
      // Note we will not use the client_listener_resource_name_template field
      // in the authority.
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener"
      "client/%s?client_listener_resource_name_template_not_in_use");
  InitClient(builder);
  CreateAndStartBackends(2);
  // Eds for the new authority balancer.
  EdsResourceArgs args =
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  WaitForAllBackends(DEBUG_LOCATION);
}

// Channel is created with URI "xds://xds.example.com/server.example.com".
// In bootstrap config, authority has no client listener template, so we use the
// default.
TEST_P(XdsFederationTest, FederationTargetAuthorityDefaultResourceTemplate) {
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "whee%25/server.example.com";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()));
  InitClient(builder);
  CreateAndStartBackends(2);
  // Eds for 2 balancers to ensure RPCs sent using current stub go to backend 0
  // and RPCs sent using the new stub go to backend 1.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Ensure update has reached and send 10 RPCs to the current stub.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  // Create second channel to new target uri and send 1 RPC.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  // We should be reaching backend 1, not 0, as balanced by the authority xds
  // server.
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1U, backends_[1]->backend_service()->request_count());
}

// Channel is created with URI "xds://xds.example.com/server.example.com".
// Bootstrap entry for that authority specifies a client listener name template.
TEST_P(XdsFederationTest, FederationTargetAuthorityWithResourceTemplate) {
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/whee%25/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()),
                       kNewListenerTemplate);
  InitClient(builder);
  CreateAndStartBackends(2);
  // Eds for 2 balancers to ensure RPCs sent using current stub go to backend 0
  // and RPCs sent using the new stub go to backend 1.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Ensure update has reached and send 10 RPCs to the current stub.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  // Create second channel to new target uri and send 1 RPC.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  // We should be reaching backend 1, not 0, as balanced by the authority xds
  // server.
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1U, backends_[1]->backend_service()->request_count());
}

TEST_P(XdsFederationTest, TargetUriAuthorityUnknown) {
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.AddAuthority(
      kAuthority, absl::StrCat("localhost:", grpc_pick_unused_port_or_die()),
      kNewListenerTemplate);
  InitClient(builder);
  auto channel2 = CreateChannel(
      /*failover_timeout_ms=*/0, kNewServerName, "xds.unknown.com");
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_EQ(status.error_message(),
            "Invalid target URI -- authority not found for xds.unknown.com");
  ASSERT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel2->GetState(false));
}

TEST_P(XdsFederationTest, RdsResourceNameAuthorityUnknown) {
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/whee%25/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.unknown.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()),
                       kNewListenerTemplate);
  InitClient(builder);
  // New RouteConfig
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Channel should report TRANSIENT_FAILURE.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_EQ(
      status.error_message(),
      absl::StrCat("empty address list (RDS resource ", kNewRouteConfigName,
                   ": authority \"xds.unknown.com\" not present in "
                   "bootstrap config (node ID:xds_end2end_test))"));
  ASSERT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel2->GetState(false));
}

TEST_P(XdsFederationTest, CdsResourceNameAuthorityUnknown) {
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/whee%25/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewClusterName =
      "xdstp://xds.unknown.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()),
                       kNewListenerTemplate);
  InitClient(builder);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Channel should report TRANSIENT_FAILURE.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_EQ(status.error_message(),
            absl::StrCat("CDS resource ", kNewClusterName,
                         ": authority \"xds.unknown.com\" not present in "
                         "bootstrap config (node ID:xds_end2end_test)"));
  ASSERT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel2->GetState(false));
}

TEST_P(XdsFederationTest, EdsResourceNameAuthorityUnknown) {
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/whee%25/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.unknown.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()),
                       kNewListenerTemplate);
  InitClient(builder);
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Channel should report TRANSIENT_FAILURE.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_EQ(
      status.error_message(),
      "no children in weighted_target policy (EDS resource "
      "xdstp://xds.unknown.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name: authority \"xds.unknown.com\" not "
      "present in bootstrap config (node ID:xds_end2end_test))");
  ASSERT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel2->GetState(false));
}

// Setting server_listener_resource_name_template to start with "xdstp:" and
// look up xds server under an authority map.
TEST_P(XdsFederationTest, FederationServer) {
  const char* kAuthority = "xds.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewServerListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "server/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewServerRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_server_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "new_edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "new_cluster_name";
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.SetClientDefaultListenerResourceNameTemplate(kNewListenerTemplate);
  builder.SetServerListenerResourceNameTemplate(kNewServerListenerTemplate);
  builder.AddAuthority(
      kAuthority, absl::StrCat("localhost:", authority_balancer_->port()),
      // Note we will not use the client_listener_resource_name_template field
      // in the authority.
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener"
      "client/%s?client_listener_resource_name_template_not_in_use");
  InitClient(builder);
  CreateBackends(2, /*xds_enabled=*/true);
  // Eds for new authority balancer.
  EdsResourceArgs args =
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New RouteConfig
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // New Server RouteConfig
  RouteConfiguration new_server_route_config = default_server_route_config_;
  new_server_route_config.set_name(kNewServerRouteConfigName);
  // New Server Listeners
  for (int port : GetBackendPorts()) {
    Listener server_listener = default_server_listener_;
    server_listener.set_name(absl::StrCat(
        "xdstp://xds.example.com/envoy.config.listener.v3.Listener/server/",
        grpc_core::LocalIp(), ":", port, "?psm_project_id=1234"));
    server_listener.mutable_address()->mutable_socket_address()->set_port_value(
        port);
    SetListenerAndRouteConfiguration(authority_balancer_.get(), server_listener,
                                     new_server_route_config,
                                     ServerHcmAccessor());
  }
  // Start backends and wait for them to start serving.
  StartAllBackends();
  for (const auto& backend : backends_) {
    ASSERT_TRUE(backend->notifier()->WaitOnServingStatusChange(
        grpc_core::LocalIpAndPort(backend->port()), grpc::StatusCode::OK));
  }
  // Make sure everything works.
  WaitForAllBackends(DEBUG_LOCATION);
}

//
// XdsMetricsTest - tests xDS metrics
//

class XdsMetricsTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    stats_plugin_ =
        grpc_core::FakeStatsPluginBuilder()
            .UseDisabledByDefaultMetrics(true)
            .SetChannelFilter(
                [](const grpc_core::experimental::StatsPluginChannelScope&
                       scope) {
                  return scope.target() == absl::StrCat("xds:", kServerName) &&
                         scope.default_authority() == kServerName &&
                         scope.experimental_args().GetString("test_only.arg") ==
                             "test_only.value";
                })
            .BuildAndRegister();
    ChannelArguments args;
    args.SetString("test_only.arg", "test_only.value");
    InitClient(/*builder=*/std::nullopt, /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/&args);
  }

  std::shared_ptr<grpc_core::FakeStatsPlugin> stats_plugin_;
};

// Runs with RDS so that we know all resource types work properly.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsMetricsTest,
    ::testing::Values(XdsTestType().set_enable_rds_testing()),
    &XdsTestType::Name);

TEST_P(XdsMetricsTest, MetricDefinitionResourceUpdatesValid) {
  const auto* descriptor =
      grpc_core::GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.xds_client.resource_updates_valid");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            grpc_core::GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            grpc_core::GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.xds_client.resource_updates_valid");
  EXPECT_EQ(descriptor->unit, "{resource}");
  EXPECT_THAT(descriptor->label_keys,
              ::testing::ElementsAre("grpc.target", "grpc.xds.server",
                                     "grpc.xds.resource_type"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_P(XdsMetricsTest, MetricDefinitionResourceUpdatesInvalid) {
  const auto* descriptor =
      grpc_core::GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.xds_client.resource_updates_invalid");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            grpc_core::GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            grpc_core::GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.xds_client.resource_updates_invalid");
  EXPECT_EQ(descriptor->unit, "{resource}");
  EXPECT_THAT(descriptor->label_keys,
              ::testing::ElementsAre("grpc.target", "grpc.xds.server",
                                     "grpc.xds.resource_type"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_P(XdsMetricsTest, MetricDefinitionServerFailure) {
  const auto* descriptor =
      grpc_core::GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.xds_client.server_failure");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            grpc_core::GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            grpc_core::GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.xds_client.server_failure");
  EXPECT_EQ(descriptor->unit, "{failure}");
  EXPECT_THAT(descriptor->label_keys,
              ::testing::ElementsAre("grpc.target", "grpc.xds.server"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_P(XdsMetricsTest, MetricDefinitionConnected) {
  const auto* descriptor =
      grpc_core::GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.xds_client.connected");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            grpc_core::GlobalInstrumentsRegistry::ValueType::kInt64);
  EXPECT_EQ(
      descriptor->instrument_type,
      grpc_core::GlobalInstrumentsRegistry::InstrumentType::kCallbackGauge);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.xds_client.connected");
  EXPECT_EQ(descriptor->unit, "{bool}");
  EXPECT_THAT(descriptor->label_keys,
              ::testing::ElementsAre("grpc.target", "grpc.xds.server"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_P(XdsMetricsTest, MetricDefinitionResources) {
  const auto* descriptor =
      grpc_core::GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.xds_client.resources");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            grpc_core::GlobalInstrumentsRegistry::ValueType::kInt64);
  EXPECT_EQ(
      descriptor->instrument_type,
      grpc_core::GlobalInstrumentsRegistry::InstrumentType::kCallbackGauge);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.xds_client.resources");
  EXPECT_EQ(descriptor->unit, "{resource}");
  EXPECT_THAT(
      descriptor->label_keys,
      ::testing::ElementsAre("grpc.target", "grpc.xds.authority",
                             "grpc.xds.resource_type", "grpc.xds.cache_state"));
  EXPECT_THAT(descriptor->optional_label_keys, ::testing::ElementsAre());
}

TEST_P(XdsMetricsTest, MetricValues) {
  const auto kMetricResourceUpdatesValid =
      grpc_core::GlobalInstrumentsRegistryTestPeer::
          FindUInt64CounterHandleByName(
              "grpc.xds_client.resource_updates_valid")
              .value();
  const auto kMetricResourceUpdatesInvalid =
      grpc_core::GlobalInstrumentsRegistryTestPeer::
          FindUInt64CounterHandleByName(
              "grpc.xds_client.resource_updates_invalid")
              .value();
  const auto kMetricServerFailure =
      grpc_core::GlobalInstrumentsRegistryTestPeer::
          FindUInt64CounterHandleByName("grpc.xds_client.server_failure")
              .value();
  const auto kMetricConnected =
      grpc_core::GlobalInstrumentsRegistryTestPeer::
          FindCallbackInt64GaugeHandleByName("grpc.xds_client.connected")
              .value();
  const auto kMetricResources =
      grpc_core::GlobalInstrumentsRegistryTestPeer::
          FindCallbackInt64GaugeHandleByName("grpc.xds_client.resources")
              .value();
  const std::string kTarget = absl::StrCat("xds:", kServerName);
  const std::string kXdsServer = absl::StrCat("localhost:", balancer_->port());
  CreateAndStartBackends(1, /*xds_enabled=*/true);
  EdsResourceArgs args =
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Use wait_for_ready and increase timeout, in case the client takes a
  // little while to get connected.
  CheckRpcSendOk(DEBUG_LOCATION, /*times=*/1,
                 RpcOptions().set_wait_for_ready(true).set_timeout_ms(15000));
  stats_plugin_->TriggerCallbacks();
  // Check client metrics.
  EXPECT_THAT(stats_plugin_->GetInt64CallbackGaugeValue(
                  kMetricConnected, {kTarget, kXdsServer}, {}),
              ::testing::Optional(1));
  EXPECT_THAT(stats_plugin_->GetUInt64CounterValue(kMetricServerFailure,
                                                   {kTarget, kXdsServer}, {}),
              std::nullopt);
  for (absl::string_view type_url :
       {"envoy.config.listener.v3.Listener",
        "envoy.config.route.v3.RouteConfiguration",
        "envoy.config.cluster.v3.Cluster",
        "envoy.config.endpoint.v3.ClusterLoadAssignment"}) {
    EXPECT_THAT(
        stats_plugin_->GetUInt64CounterValue(
            kMetricResourceUpdatesValid, {kTarget, kXdsServer, type_url}, {}),
        ::testing::Optional(1));
    EXPECT_THAT(
        stats_plugin_->GetUInt64CounterValue(
            kMetricResourceUpdatesInvalid, {kTarget, kXdsServer, type_url}, {}),
        ::testing::Optional(0));
    EXPECT_THAT(stats_plugin_->GetInt64CallbackGaugeValue(
                    kMetricResources, {kTarget, "#old", type_url, "acked"}, {}),
                ::testing::Optional(1));
  }
  // Check server metrics.
  EXPECT_THAT(stats_plugin_->GetInt64CallbackGaugeValue(
                  kMetricConnected, {"#server", kXdsServer}, {}),
              ::testing::Optional(1));
  EXPECT_THAT(stats_plugin_->GetUInt64CounterValue(kMetricServerFailure,
                                                   {"#server", kXdsServer}, {}),
              std::nullopt);
  for (absl::string_view type_url :
       {"envoy.config.listener.v3.Listener",
        "envoy.config.route.v3.RouteConfiguration"}) {
    EXPECT_THAT(
        stats_plugin_->GetUInt64CounterValue(
            kMetricResourceUpdatesValid, {"#server", kXdsServer, type_url}, {}),
        ::testing::Optional(1));
    EXPECT_THAT(stats_plugin_->GetUInt64CounterValue(
                    kMetricResourceUpdatesInvalid,
                    {"#server", kXdsServer, type_url}, {}),
                ::testing::Optional(0));
    EXPECT_THAT(
        stats_plugin_->GetInt64CallbackGaugeValue(
            kMetricResources, {"#server", "#old", type_url, "acked"}, {}),
        ::testing::Optional(1));
  }
  // Shut down balancer and wait for metrics to show the failure.
  balancer_->Shutdown();
  for (const char* target : {kTarget.c_str(), "#server"}) {
    const absl::Time deadline =
        absl::Now() + absl::Seconds(5 * grpc_test_slowdown_factor());
    while (true) {
      auto value = stats_plugin_->GetUInt64CounterValue(
          kMetricServerFailure, {target, kXdsServer}, {});
      if (value.has_value()) {
        EXPECT_EQ(1, *value);
        break;
      }
      ASSERT_LE(absl::Now(), deadline);
      absl::SleepFor(absl::Seconds(1));
    }
    stats_plugin_->TriggerCallbacks();
    EXPECT_THAT(stats_plugin_->GetInt64CallbackGaugeValue(
                    kMetricConnected, {target, kXdsServer}, {}),
                ::testing::Optional(0));
  }
}

//
// XdsFederationDisabledTest
//

using XdsFederationDisabledTest = XdsEnd2endTest;

// Runs with RDS so that we know all resource types work properly.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsFederationDisabledTest,
    ::testing::Values(XdsTestType().set_enable_rds_testing()),
    &XdsTestType::Name);

// TODO(roth,apolcyn): remove this test when the
// GRPC_EXPERIMENTAL_XDS_FEDERATION env var is removed.
TEST_P(XdsFederationDisabledTest, FederationDisabledWithNewStyleNames) {
  grpc_core::testing::ScopedEnvVar env_var("GRPC_EXPERIMENTAL_XDS_FEDERATION",
                                           "false");
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  const char* kNewEdsResourceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  InitClient();
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // New RouteConfig
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Channel should work.
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// XdsFederationLoadReportingTest - xDS federation and load reporting
//

using XdsFederationLoadReportingTest = XdsFederationTest;

// Get bootstrap from env var, so that there's a global XdsClient.
// Runs with and without RDS.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsFederationLoadReportingTest,
    ::testing::Values(
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_load_reporting(),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_load_reporting()
            .set_enable_rds_testing()),
    &XdsTestType::Name);

// Channel is created with URI "xds://xds.example.com/server.example.com".
// Bootstrap entry for that authority specifies a client listener name template.
// Sending traffic to both default balancer and authority balancer and checking
// load reporting with each one.
TEST_P(XdsFederationLoadReportingTest, FederationMultipleLoadReportingTest) {
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerTemplate =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/%s?psm_project_id=1234";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "client/whee%25/server.example.com?psm_project_id=1234";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  const size_t kNumRpcsToDefaultBalancer = 5;
  const size_t kNumRpcsToAuthorityBalancer = 10;
  XdsBootstrapBuilder builder = MakeBootstrapBuilder();
  builder.AddAuthority(kAuthority,
                       absl::StrCat("localhost:", authority_balancer_->port()),
                       kNewListenerTemplate);
  InitClient(builder);
  CreateAndStartBackends(2, /*xds_enabled=*/true);
  // Eds for 2 balancers to ensure RPCs sent using current stub go to backend 0
  // and RPCs sent using the new stub go to backend 1.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  authority_balancer_->lrs_service()->set_cluster_names({kNewClusterName});
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_lrs_server()->mutable_self();
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Send kNumRpcsToDefaultBalancer RPCs to the current stub.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcsToDefaultBalancer,
                 RpcOptions().set_wait_for_ready(true).set_timeout_ms(10000));
  // Create second channel to new target uri.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  // Send kNumRpcsToAuthorityBalancer on the second channel.
  for (size_t i = 0; i < kNumRpcsToAuthorityBalancer; ++i) {
    ClientContext context;
    EchoRequest request;
    RpcOptions().set_wait_for_ready(true).set_timeout_ms(10000).SetupRpc(
        &context, &request);
    EchoResponse response;
    grpc::Status status = stub2->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                             << " message=" << status.error_message();
  }
  // Each backend should have received the expected number of RPCs,
  // and the load report also reflect the correct numbers.
  EXPECT_EQ(kNumRpcsToAuthorityBalancer,
            backends_[1]->backend_service()->request_count());
  EXPECT_EQ(kNumRpcsToDefaultBalancer,
            backends_[0]->backend_service()->request_count());
  // Load report for authority LRS.
  std::vector<ClientStats> authority_load_report =
      authority_balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(authority_load_report.size(), 1UL);
  ClientStats& authority_client_stats = authority_load_report.front();
  EXPECT_EQ(authority_client_stats.cluster_name(), kNewClusterName);
  EXPECT_EQ(authority_client_stats.eds_service_name(), kNewEdsServiceName);
  EXPECT_EQ(kNumRpcsToAuthorityBalancer,
            authority_client_stats.total_successful_requests());
  EXPECT_EQ(0U, authority_client_stats.total_requests_in_progress());
  EXPECT_EQ(kNumRpcsToAuthorityBalancer,
            authority_client_stats.total_issued_requests());
  EXPECT_EQ(0U, authority_client_stats.total_error_requests());
  EXPECT_EQ(0U, authority_client_stats.total_dropped_requests());
  EXPECT_EQ(1U, authority_balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, authority_balancer_->lrs_service()->response_count());
  // Load report for default LRS.
  std::vector<ClientStats> default_load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(default_load_report.size(), 1UL);
  ClientStats& default_client_stats = default_load_report.front();
  EXPECT_EQ(default_client_stats.cluster_name(), kDefaultClusterName);
  EXPECT_EQ(default_client_stats.eds_service_name(), kDefaultEdsServiceName);
  EXPECT_EQ(kNumRpcsToDefaultBalancer,
            default_client_stats.total_successful_requests());
  EXPECT_EQ(0U, default_client_stats.total_requests_in_progress());
  EXPECT_EQ(kNumRpcsToDefaultBalancer,
            default_client_stats.total_issued_requests());
  EXPECT_EQ(0U, default_client_stats.total_error_requests());
  EXPECT_EQ(0U, default_client_stats.total_dropped_requests());
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// This test covers a bug found in the wild whereby we incorrectly failed
// to de-dup xDS servers when the same server is used both in an authority
// and as the top-level server in the bootstrap config.  This resulted in
// the ADS call and LRS call being in two different ChannelState objects,
// which resulted in the LRS load reports not being sent.
TEST_P(XdsFederationLoadReportingTest, SameServerInAuthorityAndTopLevel) {
  grpc_core::testing::ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_FEDERATION");
  const char* kAuthority = "xds.example.com";
  const char* kNewServerName = "whee%/server.example.com";
  const char* kNewListenerName =
      "xdstp://xds.example.com/envoy.config.listener.v3.Listener/"
      "whee%25/server.example.com";
  const char* kNewRouteConfigName =
      "xdstp://xds.example.com/envoy.config.route.v3.RouteConfiguration/"
      "new_route_config_name";
  const char* kNewClusterName =
      "xdstp://xds.example.com/envoy.config.cluster.v3.Cluster/"
      "cluster_name";
  const char* kNewEdsServiceName =
      "xdstp://xds.example.com/envoy.config.endpoint.v3.ClusterLoadAssignment/"
      "edsservice_name";
  std::string xds_server =
      absl::StrCat("localhost:", authority_balancer_->port());
  XdsBootstrapBuilder builder;
  builder.SetServers({xds_server});
  builder.AddAuthority(kAuthority, xds_server);
  InitClient(builder);
  CreateAndStartBackends(1);
  authority_balancer_->lrs_service()->set_send_all_clusters(true);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  authority_balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsServiceName));
  // New cluster
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  authority_balancer_->ads_service()->SetCdsResource(new_cluster);
  // New Route
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.set_name(kNewRouteConfigName);
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  // New Listener
  Listener listener = default_listener_;
  listener.set_name(kNewListenerName);
  SetListenerAndRouteConfiguration(authority_balancer_.get(), listener,
                                   new_route_config);
  // Create second channel to new target URI and send 1 RPC.
  auto channel2 =
      CreateChannel(/*failover_timeout_ms=*/0, kNewServerName, kAuthority);
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  ClientContext context;
  EchoRequest request;
  RpcOptions().SetupRpc(&context, &request);
  EchoResponse response;
  grpc::Status status = stub2->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(1U, backends_[0]->backend_service()->request_count());
  // Wait for load report.
  std::vector<ClientStats> authority_load_report =
      authority_balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(authority_load_report.size(), 1UL);
  ClientStats& authority_client_stats = authority_load_report.front();
  EXPECT_EQ(authority_client_stats.cluster_name(), kNewClusterName);
  EXPECT_EQ(authority_client_stats.eds_service_name(), kNewEdsServiceName);
  EXPECT_EQ(1U, authority_client_stats.total_successful_requests());
  EXPECT_EQ(0U, authority_client_stats.total_requests_in_progress());
  EXPECT_EQ(1U, authority_client_stats.total_issued_requests());
  EXPECT_EQ(0U, authority_client_stats.total_error_requests());
  EXPECT_EQ(0U, authority_client_stats.total_dropped_requests());
  EXPECT_EQ(1U, authority_balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, authority_balancer_->lrs_service()->response_count());
}

//
// SecureNamingTest - test that the right authority is used for the xDS server
//

class SecureNamingTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    // Each test calls InitClient() on its own.
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, SecureNamingTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

// Tests that secure naming check passes if target name is expected.
TEST_P(SecureNamingTest, TargetNameIsExpected) {
  InitClient(MakeBootstrapBuilder(), /*lb_expected_authority=*/"localhost:%d");
  CreateAndStartBackends(4);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION);
}

// Tests that secure naming check fails if target name is unexpected.
TEST_P(SecureNamingTest, TargetNameIsUnexpected) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  InitClient(MakeBootstrapBuilder(),
             /*lb_expected_authority=*/"incorrect_server_name");
  CreateAndStartBackends(4);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Make sure that we blow up (via abort() from the security connector) when
  // the name from the balancer doesn't match expectations.
  ASSERT_DEATH_IF_SUPPORTED({ CheckRpcSendOk(DEBUG_LOCATION); }, "");
}

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
