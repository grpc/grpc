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

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/lib/config/config_vars.h"
#include "src/proto/grpc/testing/xds/v3/fault.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using std::chrono::system_clock;

using LdsTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, LdsTest, ::testing::Values(XdsTestType()),
                         &XdsTestType::Name);

// Testing just one example of an invalid resource here.
// Unit tests for XdsListenerResourceType have exhaustive tests for all
// of the invalid cases.
TEST_P(LdsTest, NacksInvalidListener) {
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Listener has neither address nor ApiListener"));
}

// Tests that we go into TRANSIENT_FAILURE if the Listener is not an API
// listener.
TEST_P(LdsTest, NotAnApiListener) {
  Listener listener = default_server_listener_;
  listener.set_name(kServerName);
  auto hcm = ServerHcmAccessor().Unpack(listener);
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  ServerHcmAccessor().Pack(hcm, &listener);
  balancer_->ads_service()->SetLdsResource(listener);
  // RPCs should fail.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat(kServerName, ": UNAVAILABLE: not an API listener"));
  // We should have ACKed the LDS resource.
  const auto deadline =
      absl::Now() + (absl::Seconds(30) * grpc_test_slowdown_factor());
  while (true) {
    ASSERT_LT(absl::Now(), deadline) << "timed out waiting for LDS ACK";
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (response_state.has_value()) {
      EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
      break;
    }
    absl::SleepFor(absl::Seconds(1) * grpc_test_slowdown_factor());
  }
}

class LdsDeletionTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {}  // Individual tests call InitClient().
};

INSTANTIATE_TEST_SUITE_P(XdsTest, LdsDeletionTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

// Tests that we go into TRANSIENT_FAILURE if the Listener is deleted.
TEST_P(LdsDeletionTest, ListenerDeleted) {
  InitClient();
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION);
  // Unset LDS resource.
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  // Wait for RPCs to start failing.
  SendRpcsUntil(DEBUG_LOCATION, [](const RpcResult& result) {
    if (result.status.ok()) return true;  // Keep going.
    EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
    EXPECT_EQ(result.status.error_message(),
              absl::StrCat("empty address list: ", kServerName,
                           ": xDS listener resource does not exist"));
    return false;
  });
  // Make sure we ACK'ed the update.
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that we ignore Listener deletions if configured to do so.
TEST_P(LdsDeletionTest, ListenerDeletionIgnored) {
  InitClient(BootstrapBuilder().SetIgnoreResourceDeletion());
  CreateAndStartBackends(2);
  // Bring up client pointing to backend 0 and wait for it to connect.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  // Make sure we ACKed the LDS update.
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Unset LDS resource and wait for client to ACK the update.
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  const auto deadline =
      absl::Now() + (absl::Seconds(30) * grpc_test_slowdown_factor());
  while (true) {
    ASSERT_LT(absl::Now(), deadline) << "timed out waiting for LDS ACK";
    response_state = balancer_->ads_service()->lds_response_state();
    if (response_state.has_value()) break;
    absl::SleepFor(absl::Seconds(1) * grpc_test_slowdown_factor());
  }
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Make sure we can still send RPCs.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Now recreate the LDS resource pointing to a different CDS and EDS
  // resource, pointing to backend 1, and make sure the client uses it.
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsResourceName = "new_eds_resource_name";
  auto cluster = default_cluster_;
  cluster.set_name(kNewClusterName);
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(cluster);
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Wait for client to start using backend 1.
  WaitForAllBackends(DEBUG_LOCATION, 1, 2);
}

using LdsRdsInteractionTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(
    XdsTest, LdsRdsInteractionTest,
    ::testing::Values(XdsTestType().set_enable_rds_testing()),
    &XdsTestType::Name);

TEST_P(LdsRdsInteractionTest, SwitchFromRdsToInlineRouteConfig) {
  CreateAndStartBackends(2);
  // Bring up client pointing to backend 0 and wait for it to connect.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0);
  // RDS should have been ACKed.
  auto response_state = balancer_->ads_service()->rds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Now recreate the LDS resource with an inline route config pointing to a
  // different CDS and EDS resource, pointing to backend 1, and make sure
  // the client uses it.
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsResourceName = "new_eds_resource_name";
  auto cluster = default_cluster_;
  cluster.set_name(kNewClusterName);
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(cluster);
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  Listener listener = default_listener_;
  HttpConnectionManager http_connection_manager =
      ClientHcmAccessor().Unpack(listener);
  *http_connection_manager.mutable_route_config() = new_route_config;
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  balancer_->ads_service()->SetLdsResource(listener);
  // Wait for client to start using backend 1.
  WaitForBackend(DEBUG_LOCATION, 1);
  // Send an update to the original RDS resource, which the client
  // should no longer be subscribed to.  We need this RouteConfig to be
  // different than the original one so that the update does not get
  // squelched by XdsClient, so we add a second domain to the vhost that
  // will not actually be used.
  new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)->add_domains("foo.example.com");
  balancer_->ads_service()->SetRdsResource(new_route_config);
  // Wait for RDS ACK to know that the client saw the change.
  // TODO(roth): The client does not actually ACK here, it just sends an
  // unsubscription request, but our fake xDS server is incorrectly treating
  // that as an ACK.  When we have time, fix the behavior of the fake
  // xDS server, and then change this test to ensure that there is no RDS
  // ACK within the 30-second timeout period.
  const auto deadline =
      absl::Now() + (absl::Seconds(30) * grpc_test_slowdown_factor());
  while (true) {
    ASSERT_LT(absl::Now(), deadline) << "timed out waiting for RDS ACK";
    response_state = balancer_->ads_service()->rds_response_state();
    if (response_state.has_value()) break;
    absl::SleepFor(absl::Seconds(1) * grpc_test_slowdown_factor());
  }
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Make sure RPCs are still going to backend 1.  This shows that the
  // client did not replace its route config with the one from the RDS
  // resource that it should no longer be using.
  ResetBackendCounters();
  CheckRpcSendOk(DEBUG_LOCATION);
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service()->request_count());
}

TEST_P(LdsRdsInteractionTest, SwitchFromInlineRouteConfigToRds) {
  CreateAndStartBackends(2);
  // Create an LDS resource with an inline RouteConfig pointing to a
  // different CDS and EDS resource, sending traffic to backend 0.
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsResourceName = "new_eds_resource_name";
  auto cluster = default_cluster_;
  cluster.set_name(kNewClusterName);
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  Listener listener = default_listener_;
  HttpConnectionManager http_connection_manager =
      ClientHcmAccessor().Unpack(listener);
  *http_connection_manager.mutable_route_config() = route_config;
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  balancer_->ads_service()->SetLdsResource(listener);
  // Start the client and make sure traffic goes to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  // RDS should not have been ACKed, because the RouteConfig was inlined.
  ASSERT_FALSE(balancer_->ads_service()->rds_response_state().has_value());
  // Change the LDS resource to point to an RDS resource.  The LDS resource
  // configures the fault injection filter with a config that fails all RPCs.
  // However, the RDS resource has a typed_per_filter_config override that
  // disables the fault injection filter.  The RDS resource points to a
  // new cluster that sends traffic to backend 1.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  route_config = default_route_config_;
  auto* config_map = route_config.mutable_virtual_hosts(0)
                         ->mutable_routes(0)
                         ->mutable_typed_per_filter_config();
  (*config_map)["envoy.fault"].PackFrom(
      envoy::extensions::filters::http::fault::v3::HTTPFault());
  envoy::extensions::filters::http::fault::v3::HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(100);
  abort_percentage->set_denominator(abort_percentage->HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  listener = default_listener_;
  http_connection_manager = ClientHcmAccessor().Unpack(listener);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("envoy.fault");
  filter->mutable_typed_config()->PackFrom(http_fault);
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   route_config);
  // Wait for traffic to switch to backend 1.  There should be no RPC
  // failures here; if there are, that indicates that the client started
  // using the new LDS resource before it saw the new RDS resource.
  WaitForBackend(DEBUG_LOCATION, 1);
}

TEST_P(LdsRdsInteractionTest, HcmConfigUpdatedWithoutRdsChange) {
  CreateAndStartBackends(1);
  // Bring up client pointing to backend 0 and wait for it to connect.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0);
  // LDS should have been ACKed.
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Now update the LDS resource to add the fault injection filter with
  // a config that fails all RPCs.
  envoy::extensions::filters::http::fault::v3::HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(100);
  abort_percentage->set_denominator(abort_percentage->HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  Listener listener = default_listener_;
  HttpConnectionManager http_connection_manager =
      ClientHcmAccessor().Unpack(listener);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("envoy.fault");
  filter->mutable_typed_config()->PackFrom(http_fault);
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   default_route_config_);
  // Wait for the LDS update to be ACKed.
  const auto deadline =
      absl::Now() + (absl::Seconds(30) * grpc_test_slowdown_factor());
  while (true) {
    ASSERT_LT(absl::Now(), deadline) << "timed out waiting for LDS ACK";
    response_state = balancer_->ads_service()->lds_response_state();
    if (response_state.has_value()) break;
    absl::SleepFor(absl::Seconds(1) * grpc_test_slowdown_factor());
  }
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Now RPCs should fail with ABORTED status.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::ABORTED, "Fault injected");
}

TEST_P(LdsRdsInteractionTest, LdsUpdateChangesHcmConfigAndRdsResourceName) {
  CreateAndStartBackends(2);
  // Bring up client pointing to backend 0 and wait for it to connect.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Change the LDS resource to point to an RDS resource.  The LDS resource
  // configures the fault injection filter with a config that fails all RPCs.
  // However, the RDS resource has a typed_per_filter_config override that
  // disables the fault injection filter.  The RDS resource points to a
  // new cluster that sends traffic to backend 1.
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsResourceName = "new_eds_resource_name";
  auto cluster = default_cluster_;
  cluster.set_name(kNewClusterName);
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(cluster);
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  RouteConfiguration route_config = default_route_config_;
  route_config.set_name("new_route_config");
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  auto* config_map = route_config.mutable_virtual_hosts(0)
                         ->mutable_routes(0)
                         ->mutable_typed_per_filter_config();
  (*config_map)["envoy.fault"].PackFrom(
      envoy::extensions::filters::http::fault::v3::HTTPFault());
  envoy::extensions::filters::http::fault::v3::HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(100);
  abort_percentage->set_denominator(abort_percentage->HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  Listener listener = default_listener_;
  HttpConnectionManager http_connection_manager =
      ClientHcmAccessor().Unpack(listener);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("envoy.fault");
  filter->mutable_typed_config()->PackFrom(http_fault);
  ClientHcmAccessor().Pack(http_connection_manager, &listener);
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   route_config);
  // Wait for traffic to switch to backend 1.  There should be no RPC
  // failures here; if there are, that indicates that the client started
  // using the new LDS resource before it saw the new RDS resource.
  WaitForBackend(DEBUG_LOCATION, 1);
}

using LdsRdsTest = XdsEnd2endTest;

// Test with and without RDS.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, LdsRdsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_rds_testing()),
    &XdsTestType::Name);

MATCHER_P2(AdjustedClockInRange, t1, t2, "equals time") {
  gpr_cycle_counter cycle_now = gpr_get_cycle_counter();
  grpc_core::Timestamp cycle_time =
      grpc_core::Timestamp::FromCycleCounterRoundDown(cycle_now);
  grpc_core::Timestamp time_spec =
      grpc_core::Timestamp::FromTimespecRoundDown(gpr_now(GPR_CLOCK_MONOTONIC));
  grpc_core::Timestamp now = arg + (time_spec - cycle_time);
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(::testing::Ge(t1), now, result_listener);
  ok &= ::testing::ExplainMatchResult(::testing::Lt(t2), now, result_listener);
  return ok;
}

// Tests that LDS client ACKs but fails if matching domain can't be found in
// the LDS response.
TEST_P(LdsRdsTest, NoMatchedDomain) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(balancer_.get(), route_config);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      absl::StrCat(
          (GetParam().enable_rds_testing() ? kDefaultRouteConfigurationName
                                           : kServerName),
          ": UNAVAILABLE: could not find VirtualHost for ", kServerName,
          " in RouteConfiguration"));
  // Do a bit of polling, to allow the ACK to get to the ADS server.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100));
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should choose the virtual host with matching domain
// if multiple virtual hosts exist in the LDS response.
TEST_P(LdsRdsTest, ChooseMatchedDomain) {
  RouteConfiguration route_config = default_route_config_;
  *(route_config.add_virtual_hosts()) = route_config.virtual_hosts(0);
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(balancer_.get(), route_config);
  (void)SendRpc();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should choose the last route in the virtual host if
// multiple routes exist in the LDS response.
TEST_P(LdsRdsTest, ChooseLastRoute) {
  RouteConfiguration route_config = default_route_config_;
  *(route_config.mutable_virtual_hosts(0)->add_routes()) =
      route_config.virtual_hosts(0).routes(0);
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->mutable_cluster_header();
  SetRouteConfiguration(balancer_.get(), route_config);
  (void)SendRpc();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, NoMatchingRoute) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("/unknown/method");
  SetRouteConfiguration(balancer_.get(), route_config);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "No matching route found in xDS route config");
  // Do a bit of polling, to allow the ACK to get to the ADS server.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100));
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, EmptyRouteList) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)->clear_routes();
  SetRouteConfiguration(balancer_.get(), route_config);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "No matching route found in xDS route config");
  // Do a bit of polling, to allow the ACK to get to the ADS server.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100));
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Testing just one example of an invalid resource here.
// Unit tests for XdsRouteConfigResourceType have exhaustive tests for all
// of the invalid cases.
TEST_P(LdsRdsTest, NacksInvalidRouteConfig) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)->mutable_routes(0)->clear_match();
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_EQ(
      response_state->error_message,
      absl::StrCat(
          "xDS response validation errors: [resource index 0: ",
          GetParam().enable_rds_testing()
              ? "route_config_name: INVALID_ARGUMENT: "
                "errors validating RouteConfiguration resource: ["
                "field:"
              : "server.example.com: INVALID_ARGUMENT: "
                "errors validating ApiListener: ["
                "field:api_listener.api_listener.value["
                "envoy.extensions.filters.network.http_connection_manager.v3"
                ".HttpConnectionManager].route_config.",
          "virtual_hosts[0].routes[0].match "
          "error:field not present]]"));
}

// Tests that LDS client should fail RPCs with UNAVAILABLE status code if the
// matching route has an action other than RouteAction.
TEST_P(LdsRdsTest, MatchingRouteHasNoRouteAction) {
  RouteConfiguration route_config = default_route_config_;
  // Set a route with an inappropriate route action
  auto* vhost = route_config.mutable_virtual_hosts(0);
  vhost->mutable_routes(0)->mutable_redirect();
  // Add another route to make sure that the resolver code actually tries to
  // match to a route instead of using a shorthand logic to error out.
  auto* route = vhost->add_routes();
  route->mutable_match()->set_prefix("");
  route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "Matching route has inappropriate action");
}

// Tests that LDS client should choose the default route (with no matching
// specified) after unable to find a match with previous routes.
TEST_P(LdsRdsTest, XdsRoutingPathMatching) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* route3 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route3->mutable_match()->set_path("/grpc.testing.EchoTest3Service/Echo3");
  route3->mutable_route()->set_cluster(kDefaultClusterName);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(5000));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs,
                 RpcOptions()
                     .set_rpc_service(SERVICE_ECHO1)
                     .set_rpc_method(METHOD_ECHO1)
                     .set_wait_for_ready(true));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho2Rpcs,
                 RpcOptions()
                     .set_rpc_service(SERVICE_ECHO2)
                     .set_rpc_method(METHOD_ECHO2)
                     .set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPathMatchingCaseInsensitive) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEchoRpcs = 30;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  // First route will not match, since it's case-sensitive.
  // Second route will match with same path.
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/GrPc.TeStInG.EcHoTeSt1SErViCe/EcHo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/GrPc.TeStInG.EcHoTeSt1SErViCe/EcHo1");
  route2->mutable_match()->mutable_case_sensitive()->set_value(false);
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs,
                 RpcOptions()
                     .set_rpc_service(SERVICE_ECHO1)
                     .set_rpc_method(METHOD_ECHO1)
                     .set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPrefixMatching) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_prefix("/grpc.testing.EchoTest2Service/");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(
      DEBUG_LOCATION, kNumEcho1Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_wait_for_ready(true));
  CheckRpcSendOk(
      DEBUG_LOCATION, kNumEcho2Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO2).set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPrefixMatchingCaseInsensitive) {
  CreateAndStartBackends(3);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEchoRpcs = 30;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  // First route will not match, since it's case-sensitive.
  // Second route will match with same path.
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/GrPc.TeStInG.EcHoTeSt1SErViCe");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_prefix("/GrPc.TeStInG.EcHoTeSt1SErViCe");
  route2->mutable_match()->mutable_case_sensitive()->set_value(false);
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs,
                 RpcOptions()
                     .set_rpc_service(SERVICE_ECHO1)
                     .set_rpc_method(METHOD_ECHO1)
                     .set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingPathRegexMatching) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEcho1Rpcs = 10;
  const size_t kNumEcho2Rpcs = 20;
  const size_t kNumEchoRpcs = 30;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  // Will match "/grpc.testing.EchoTest1Service/"
  route1->mutable_match()->mutable_safe_regex()->set_regex(".*1.*");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  // Will match "/grpc.testing.EchoTest2Service/"
  route2->mutable_match()->mutable_safe_regex()->set_regex(".*2.*");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_wait_for_ready(true));
  CheckRpcSendOk(
      DEBUG_LOCATION, kNumEcho1Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_wait_for_ready(true));
  CheckRpcSendOk(
      DEBUG_LOCATION, kNumEcho2Rpcs,
      RpcOptions().set_rpc_service(SERVICE_ECHO2).set_wait_for_ready(true));
  // Make sure RPCs all go to the correct backend.
  for (size_t i = 0; i < 2; ++i) {
    EXPECT_EQ(kNumEchoRpcs / 2,
              backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_EQ(kNumEcho2Rpcs, backends_[3]->backend_service2()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingWeightedCluster) {
  CreateAndStartBackends(3);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNotUsedClusterName = "not_used_cluster";
  const size_t kNumEchoRpcs = 10;  // RPCs that will go to a fixed backend.
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const size_t kNumEcho1Rpcs =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  // Cluster with weight 0 will not be used.
  auto* weighted_cluster3 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster3->set_name(kNotUsedClusterName);
  weighted_cluster3->mutable_weight()->set_value(0);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  WaitForAllBackends(DEBUG_LOCATION, 1, 3, /*check_status=*/nullptr,
                     WaitForBackendOptions(),
                     RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_75_request_count =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_25_request_count =
      backends_[2]->backend_service1()->request_count();
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, XdsRoutingWeightedClusterNoIntegerOverflow) {
  CreateAndStartBackends(3);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kNumEchoRpcs = 10;  // RPCs that will go to a fixed backend.
  const uint32_t kWeight1 = std::numeric_limits<uint32_t>::max() / 3;
  const uint32_t kWeight2 = std::numeric_limits<uint32_t>::max() - kWeight1;
  const double kErrorTolerance = 0.05;
  const double kWeight1Percent =
      static_cast<double>(kWeight1) / std::numeric_limits<uint32_t>::max();
  const double kWeight2Percent =
      static_cast<double>(kWeight2) / std::numeric_limits<uint32_t>::max();
  const size_t kNumEcho1Rpcs =
      ComputeIdealNumRpcs(kWeight2Percent, kErrorTolerance);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight1);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight2);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  WaitForAllBackends(DEBUG_LOCATION, 1, 3, /*check_status=*/nullptr,
                     WaitForBackendOptions(),
                     RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight1_request_count =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight2_request_count =
      backends_[2]->backend_service1()->request_count();
  gpr_log(GPR_INFO, "target1 received %d rpcs and target2 received %d rpcs",
          weight1_request_count, weight2_request_count);
  EXPECT_THAT(static_cast<double>(weight1_request_count) / kNumEcho1Rpcs,
              ::testing::DoubleNear(kWeight1Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight2_request_count) / kNumEcho1Rpcs,
              ::testing::DoubleNear(kWeight2Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetDefaultRoute) {
  CreateAndStartBackends(3);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const size_t kNumEchoRpcs =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populating Route Configurations for LDS.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 1, 3);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  const int weight_75_request_count =
      backends_[1]->backend_service()->request_count();
  const int weight_25_request_count =
      backends_[2]->backend_service()->request_count();
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEchoRpcs,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEchoRpcs,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, XdsRoutingWeightedClusterUpdateWeights) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const size_t kNumEchoRpcs = 10;
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const size_t kWeight50 = 50;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const double kWeight50Percent = static_cast<double>(kWeight50) / 100;
  const size_t kNumEcho1Rpcs7525 =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  const size_t kNumEcho1Rpcs5050 =
      ComputeIdealNumRpcs(kWeight50Percent, kErrorTolerance);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args3({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
  // Populating Route Configurations.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 1, /*check_status=*/nullptr,
                     WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(5000));
  WaitForAllBackends(
      DEBUG_LOCATION, 1, 3, /*check_status=*/nullptr, WaitForBackendOptions(),
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_timeout_ms(5000));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_timeout_ms(5000));
  CheckRpcSendOk(
      DEBUG_LOCATION, kNumEcho1Rpcs7525,
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_timeout_ms(5000));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_75_request_count =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[1]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_25_request_count =
      backends_[2]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
  // Change Route Configurations: same clusters different weights.
  weighted_cluster1->mutable_weight()->set_value(kWeight50);
  weighted_cluster2->mutable_weight()->set_value(kWeight50);
  // Change default route to a new cluster to help to identify when new
  // polices are seen by the client.
  default_route->mutable_route()->set_cluster(kNewCluster3Name);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  ResetBackendCounters();
  WaitForAllBackends(DEBUG_LOCATION, 3, 4, /*check_status=*/nullptr,
                     WaitForBackendOptions(),
                     RpcOptions().set_timeout_ms(5000));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_timeout_ms(5000));
  CheckRpcSendOk(
      DEBUG_LOCATION, kNumEcho1Rpcs5050,
      RpcOptions().set_rpc_service(SERVICE_ECHO1).set_timeout_ms(5000));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(0, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_50_request_count_1 =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_50_request_count_2 =
      backends_[2]->backend_service1()->request_count();
  EXPECT_EQ(kNumEchoRpcs, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_1) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_2) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, XdsRoutingWeightedClusterUpdateClusters) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const size_t kNumEchoRpcs = 10;
  const size_t kWeight75 = 75;
  const size_t kWeight25 = 25;
  const size_t kWeight50 = 50;
  const double kErrorTolerance = 0.05;
  const double kWeight75Percent = static_cast<double>(kWeight75) / 100;
  const double kWeight25Percent = static_cast<double>(kWeight25) / 100;
  const double kWeight50Percent = static_cast<double>(kWeight50) / 100;
  const size_t kNumEcho1Rpcs7525 =
      ComputeIdealNumRpcs(kWeight75Percent, kErrorTolerance);
  const size_t kNumEcho1Rpcs5050 =
      ComputeIdealNumRpcs(kWeight50Percent, kErrorTolerance);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args3({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
  // Populating Route Configurations.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  auto* weighted_cluster2 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster2->set_name(kDefaultClusterName);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForBackend(DEBUG_LOCATION, 0);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs7525,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  int weight_25_request_count =
      backends_[0]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  int weight_75_request_count =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
  // Change Route Configurations: new set of clusters with different weights.
  weighted_cluster1->mutable_weight()->set_value(kWeight50);
  weighted_cluster2->set_name(kNewCluster2Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight50);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  ResetBackendCounters();
  WaitForBackend(DEBUG_LOCATION, 2, /*check_status=*/nullptr,
                 WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs5050,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  const int weight_50_request_count_1 =
      backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  const int weight_50_request_count_2 =
      backends_[2]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service1()->request_count());
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_1) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(weight_50_request_count_2) / kNumEcho1Rpcs5050,
      ::testing::DoubleNear(kWeight50Percent, kErrorTolerance));
  // Change Route Configurations.
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  weighted_cluster2->set_name(kNewCluster3Name);
  weighted_cluster2->mutable_weight()->set_value(kWeight25);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  ResetBackendCounters();
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs7525,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  weight_75_request_count = backends_[1]->backend_service1()->request_count();
  EXPECT_EQ(0, backends_[2]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[2]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[3]->backend_service()->request_count());
  weight_25_request_count = backends_[3]->backend_service1()->request_count();
  gpr_log(GPR_INFO, "target_75 received %d rpcs and target_25 received %d rpcs",
          weight_75_request_count, weight_25_request_count);
  EXPECT_THAT(static_cast<double>(weight_75_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight75Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_25_request_count) / kNumEcho1Rpcs7525,
              ::testing::DoubleNear(kWeight25Percent, kErrorTolerance));
}

TEST_P(LdsRdsTest, XdsRoutingClusterUpdateClusters) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 5;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Send Route Configuration.
  RouteConfiguration new_route_config = default_route_config_;
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  // Change Route Configurations: new default cluster.
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster(kNewClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 1, 2);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  // Make sure RPCs all go to the correct backend.
  EXPECT_EQ(kNumEchoRpcs, backends_[1]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingClusterUpdateClustersWithPickingDelays) {
  // Start with only backend 1 up, but the default cluster pointing to
  // backend 0, which is down.
  CreateBackends(2);
  StartBackend(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Start an RPC with wait_for_ready=true and no deadline.  This will
  // stay pending until backend 0 is reachable.
  LongRunningRpc rpc;
  rpc.StartRpc(stub_.get(),
               RpcOptions().set_wait_for_ready(true).set_timeout_ms(0));
  // Send a non-wait_for_ready RPC, which should fail.  This tells us
  // that the client has received the update and attempted to connect.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
  // Now create a new cluster, pointing to backend 1.
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  EdsResourceArgs args1({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Send a update RouteConfiguration to use backend 1.
  RouteConfiguration new_route_config = default_route_config_;
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster(kNewClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Wait for RPCs to go to the new backend: 1, this ensures that the client
  // has processed the update.
  WaitForBackend(
      DEBUG_LOCATION, 1,
      [&](const RpcResult& result) {
        if (!result.status.ok()) {
          EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
          EXPECT_THAT(
              result.status.error_message(),
              ::testing::MatchesRegex(MakeConnectionFailureRegex(
                  "connections to all backends failing; last error: ")));
        }
      },
      WaitForBackendOptions().set_reset_counters(false));
  // Bring up the backend 0.  Yhis will allow the delayed RPC to finally
  // complete.
  StartBackend(0);
  Status status = rpc.GetStatus();
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  // Make sure RPCs went to the correct backends.
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingApplyXdsTimeout) {
  const auto kTimeoutGrpcHeaderMax = grpc_core::Duration::Milliseconds(1500);
  const auto kTimeoutMaxStreamDuration =
      grpc_core::Duration::Milliseconds(2500);
  const auto kTimeoutHttpMaxStreamDuration =
      grpc_core::Duration::Milliseconds(3500);
  const auto kTimeoutApplication = grpc_core::Duration::Milliseconds(4500);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args2({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args3({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
  // Construct listener.
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // Set up HTTP max_stream_duration of 3.5 seconds
  SetProtoDuration(
      kTimeoutHttpMaxStreamDuration,
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  // Construct route config.
  RouteConfiguration new_route_config = default_route_config_;
  // route 1: Set max_stream_duration of 2.5 seconds, Set
  // grpc_timeout_header_max of 1.5
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* max_stream_duration =
      route1->mutable_route()->mutable_max_stream_duration();
  SetProtoDuration(kTimeoutMaxStreamDuration,
                   max_stream_duration->mutable_max_stream_duration());
  SetProtoDuration(kTimeoutGrpcHeaderMax,
                   max_stream_duration->mutable_grpc_timeout_header_max());
  // route 2: Set max_stream_duration of 2.5 seconds
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  max_stream_duration = route2->mutable_route()->mutable_max_stream_duration();
  SetProtoDuration(kTimeoutMaxStreamDuration,
                   max_stream_duration->mutable_max_stream_duration());
  // route 3: No timeout values in route configuration
  auto* route3 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route3->mutable_match()->set_path("/grpc.testing.EchoTestService/Echo");
  route3->mutable_route()->set_cluster(kNewCluster3Name);
  // Set listener and route config.
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   new_route_config);
  // Test grpc_timeout_header_max of 1.5 seconds applied
  grpc_core::Timestamp t0 = NowFromCycleCounter();
  grpc_core::Timestamp t1 =
      t0 + (kTimeoutGrpcHeaderMax * grpc_test_slowdown_factor());
  grpc_core::Timestamp t2 =
      t0 + (kTimeoutMaxStreamDuration * grpc_test_slowdown_factor());
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED,
                      "Deadline Exceeded",
                      RpcOptions()
                          .set_rpc_service(SERVICE_ECHO1)
                          .set_rpc_method(METHOD_ECHO1)
                          .set_wait_for_ready(true)
                          .set_timeout(kTimeoutApplication));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
  // Test max_stream_duration of 2.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + (kTimeoutMaxStreamDuration * grpc_test_slowdown_factor());
  t2 = t0 + (kTimeoutHttpMaxStreamDuration * grpc_test_slowdown_factor());
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED,
                      "Deadline Exceeded",
                      RpcOptions()
                          .set_rpc_service(SERVICE_ECHO2)
                          .set_rpc_method(METHOD_ECHO2)
                          .set_wait_for_ready(true)
                          .set_timeout(kTimeoutApplication));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
  // Test http_stream_duration of 3.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + (kTimeoutHttpMaxStreamDuration * grpc_test_slowdown_factor());
  t2 = t0 + (kTimeoutApplication * grpc_test_slowdown_factor());
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "Deadline Exceeded",
      RpcOptions().set_wait_for_ready(true).set_timeout(kTimeoutApplication));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
}

TEST_P(LdsRdsTest, XdsRoutingApplyApplicationTimeoutWhenXdsTimeoutExplicit) {
  const auto kTimeoutMaxStreamDuration =
      grpc_core::Duration::Milliseconds(2500);
  const auto kTimeoutHttpMaxStreamDuration =
      grpc_core::Duration::Milliseconds(3500);
  const auto kTimeoutApplication = grpc_core::Duration::Milliseconds(4500);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  EdsResourceArgs args2({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Construct listener.
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // Set up HTTP max_stream_duration of 3.5 seconds
  SetProtoDuration(
      kTimeoutHttpMaxStreamDuration,
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  // Construct route config.
  RouteConfiguration new_route_config = default_route_config_;
  // route 1: Set max_stream_duration of 2.5 seconds, Set
  // grpc_timeout_header_max of 0
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* max_stream_duration =
      route1->mutable_route()->mutable_max_stream_duration();
  SetProtoDuration(kTimeoutMaxStreamDuration,
                   max_stream_duration->mutable_max_stream_duration());
  SetProtoDuration(grpc_core::Duration::Zero(),
                   max_stream_duration->mutable_grpc_timeout_header_max());
  // route 2: Set max_stream_duration to 0
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  max_stream_duration = route2->mutable_route()->mutable_max_stream_duration();
  SetProtoDuration(grpc_core::Duration::Zero(),
                   max_stream_duration->mutable_max_stream_duration());
  // Set listener and route config.
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   new_route_config);
  // Test application timeout is applied for route 1
  auto t0 = system_clock::now();
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED,
                      "Deadline Exceeded",
                      RpcOptions()
                          .set_rpc_service(SERVICE_ECHO1)
                          .set_rpc_method(METHOD_ECHO1)
                          .set_wait_for_ready(true)
                          .set_timeout(kTimeoutApplication));
  auto elapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(
      elapsed_nano_seconds.count(),
      (kTimeoutApplication * grpc_test_slowdown_factor()).millis() * 1000);
  // Test application timeout is applied for route 2
  t0 = system_clock::now();
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED,
                      "Deadline Exceeded",
                      RpcOptions()
                          .set_rpc_service(SERVICE_ECHO2)
                          .set_rpc_method(METHOD_ECHO2)
                          .set_wait_for_ready(true)
                          .set_timeout(kTimeoutApplication));
  elapsed_nano_seconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
      system_clock::now() - t0);
  EXPECT_GT(
      elapsed_nano_seconds.count(),
      (kTimeoutApplication * grpc_test_slowdown_factor()).millis() * 1000);
}

TEST_P(LdsRdsTest, XdsRoutingApplyApplicationTimeoutWhenHttpTimeoutExplicit) {
  const auto kTimeoutApplication = grpc_core::Duration::Milliseconds(4500);
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // Set up HTTP max_stream_duration to be explicit 0
  auto* duration =
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration();
  duration->set_seconds(0);
  duration->set_nanos(0);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  // Set listener and route config.
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   default_route_config_);
  // Test application timeout is applied for route 1
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "Deadline Exceeded",
      RpcOptions().set_wait_for_ready(true).set_timeout(kTimeoutApplication));
  auto elapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(
      elapsed_nano_seconds.count(),
      (kTimeoutApplication * grpc_test_slowdown_factor()).millis() * 1000);
}

// Test to ensure application-specified deadline won't be affected when
// the xDS config does not specify a timeout.
TEST_P(LdsRdsTest, XdsRoutingWithOnlyApplicationTimeout) {
  const auto kTimeoutApplication = grpc_core::Duration::Milliseconds(4500);
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "Deadline Exceeded",
      RpcOptions().set_wait_for_ready(true).set_timeout(kTimeoutApplication));
  auto elapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(
      elapsed_nano_seconds.count(),
      (kTimeoutApplication * grpc_test_slowdown_factor()).millis() * 1000);
}

TEST_P(LdsRdsTest, XdsRetryPolicyNumRetries) {
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on(
      "5xx,cancelled,deadline-exceeded,internal,resource-exhausted,"
      "unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Ensure we retried the correct number of times on all supported status.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::CANCELLED, "",
      RpcOptions().set_server_expected_error(StatusCode::CANCELLED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "",
      RpcOptions().set_server_expected_error(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::INTERNAL, "",
      RpcOptions().set_server_expected_error(StatusCode::INTERNAL));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::RESOURCE_EXHAUSTED, "",
      RpcOptions().set_server_expected_error(StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE, "",
      RpcOptions().set_server_expected_error(StatusCode::UNAVAILABLE));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  // Ensure we don't retry on an unsupported status.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAUTHENTICATED, "",
      RpcOptions().set_server_expected_error(StatusCode::UNAUTHENTICATED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyAtVirtualHostLevel) {
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* retry_policy =
      new_route_config.mutable_virtual_hosts(0)->mutable_retry_policy();
  retry_policy->set_retry_on(
      "cancelled,deadline-exceeded,internal,resource-exhausted,unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Ensure we retried the correct number of times on a supported status.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "",
      RpcOptions().set_server_expected_error(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyLongBackOff) {
  CreateAndStartBackends(1);
  // Set num retries to 3, but due to longer back off, we expect only 1 retry
  // will take place.
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on(
      "5xx,cancelled,deadline-exceeded,internal,resource-exhausted,"
      "unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  // Set backoff to 1 second, 1/2 of rpc timeout of 2 second.
  SetProtoDuration(
      grpc_core::Duration::Seconds(1),
      retry_policy->mutable_retry_back_off()->mutable_base_interval());
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // No need to set max interval and just let it be the default of 10x of base.
  // We expect 1 retry before the RPC times out with DEADLINE_EXCEEDED.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "Deadline Exceeded",
      RpcOptions().set_timeout_ms(2500).set_server_expected_error(
          StatusCode::CANCELLED));
  EXPECT_EQ(1 + 1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyMaxBackOff) {
  CreateAndStartBackends(1);
  // Set num retries to 3, but due to longer back off, we expect only 2 retry
  // will take place, while the 2nd one will obey the max backoff.
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on(
      "5xx,cancelled,deadline-exceeded,internal,resource-exhausted,"
      "unavailable");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  // Set backoff to 1 second.
  SetProtoDuration(
      grpc_core::Duration::Seconds(1),
      retry_policy->mutable_retry_back_off()->mutable_base_interval());
  // Set max interval to be the same as base, so 2 retries will take 2 seconds
  // and both retries will take place before the 2.5 seconds rpc timeout.
  // Tested to ensure if max is not set, this test will be the same as
  // XdsRetryPolicyLongBackOff and we will only see 1 retry in that case.
  SetProtoDuration(
      grpc_core::Duration::Seconds(1),
      retry_policy->mutable_retry_back_off()->mutable_max_interval());
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Send an initial RPC to make sure we get connected (we don't want
  // the channel startup time to affect the retry timing).
  CheckRpcSendOk(DEBUG_LOCATION);
  ResetBackendCounters();
  // We expect 2 retry before the RPC times out with DEADLINE_EXCEEDED.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "Deadline Exceeded",
      RpcOptions().set_timeout_ms(2500).set_server_expected_error(
          StatusCode::CANCELLED));
  EXPECT_EQ(2 + 1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyUnsupportedStatusCode) {
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("5xx");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // We expect no retry.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "",
      RpcOptions().set_server_expected_error(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest,
       XdsRetryPolicyUnsupportedStatusCodeWithVirtualHostLevelRetry) {
  CreateAndStartBackends(1);
  const size_t kNumRetries = 3;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy with no supported retry_on
  // statuses.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("5xx");
  retry_policy->mutable_num_retries()->set_value(kNumRetries);
  // Construct a virtual host level retry policy with supported statuses.
  auto* virtual_host_retry_policy =
      new_route_config.mutable_virtual_hosts(0)->mutable_retry_policy();
  virtual_host_retry_policy->set_retry_on(
      "cancelled,deadline-exceeded,internal,resource-exhausted,unavailable");
  virtual_host_retry_policy->mutable_num_retries()->set_value(kNumRetries);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // We expect no retry.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::DEADLINE_EXCEEDED, "",
      RpcOptions().set_server_expected_error(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatching) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEcho1Rpcs = 100;
  const size_t kNumEchoRpcs = 5;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->set_exact_match("POST,PUT,GET");
  auto* header_matcher2 = route1->mutable_match()->add_headers();
  header_matcher2->set_name("header2");
  header_matcher2->mutable_safe_regex_match()->set_regex("[a-z]*");
  auto* header_matcher3 = route1->mutable_match()->add_headers();
  header_matcher3->set_name("header3");
  header_matcher3->mutable_range_match()->set_start(1);
  header_matcher3->mutable_range_match()->set_end(1000);
  auto* header_matcher4 = route1->mutable_match()->add_headers();
  header_matcher4->set_name("header4");
  header_matcher4->set_present_match(false);
  auto* header_matcher5 = route1->mutable_match()->add_headers();
  header_matcher5->set_name("header5");
  header_matcher5->set_present_match(true);
  auto* header_matcher6 = route1->mutable_match()->add_headers();
  header_matcher6->set_name("header6");
  header_matcher6->set_prefix_match("/grpc");
  auto* header_matcher7 = route1->mutable_match()->add_headers();
  header_matcher7->set_name("header7");
  header_matcher7->set_suffix_match(".cc");
  header_matcher7->set_invert_match(true);
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"header1", "POST"},
      {"header2", "blah"},
      {"header3", "1"},
      {"header5", "anything"},
      {"header6", "/grpc.testing.EchoTest1Service/"},
      {"header1", "PUT"},
      {"header7", "grpc.java"},
      {"header1", "GET"},
  };
  const auto header_match_rpc_options = RpcOptions()
                                            .set_rpc_service(SERVICE_ECHO1)
                                            .set_rpc_method(METHOD_ECHO1)
                                            .set_metadata(std::move(metadata));
  // Make sure all backends are up.
  WaitForBackend(DEBUG_LOCATION, 0);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), header_match_rpc_options);
  // Send RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs, header_match_rpc_options);
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service2()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service2()->request_count());
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingSpecialHeaderContentType) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const size_t kNumEchoRpcs = 100;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("content-type");
  header_matcher1->set_exact_match("notapplication/grpc");
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  auto* header_matcher2 = default_route->mutable_match()->add_headers();
  header_matcher2->set_name("content-type");
  header_matcher2->set_exact_match("application/grpc");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  // Make sure the backend is up.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  // Send RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingSpecialCasesToIgnore) {
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const size_t kNumEchoRpcs = 100;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("grpc-foo-bin");
  header_matcher1->set_present_match(true);
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  // Send headers which will mismatch each route
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"grpc-foo-bin", "grpc-foo-bin"},
  };
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_metadata(metadata));
  // Verify that only the default backend got RPCs since all previous routes
  // were mismatched.
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingRuntimeFractionMatching) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  const double kErrorTolerance = 0.05;
  const size_t kRouteMatchNumerator = 25;
  const double kRouteMatchPercent =
      static_cast<double>(kRouteMatchNumerator) / 100;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kRouteMatchPercent, kErrorTolerance);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()
      ->mutable_runtime_fraction()
      ->mutable_default_value()
      ->set_numerator(kRouteMatchNumerator);
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  const int default_backend_count =
      backends_[0]->backend_service()->request_count();
  const int matched_backend_count =
      backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(default_backend_count) / kNumRpcs,
              ::testing::DoubleNear(1 - kRouteMatchPercent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(matched_backend_count) / kNumRpcs,
              ::testing::DoubleNear(kRouteMatchPercent, kErrorTolerance));
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingHeadersMatchingUnmatchCases) {
  CreateAndStartBackends(4);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kNewCluster3Name = "new_cluster_3";
  const char* kNewEdsService3Name = "new_eds_service_name_3";
  const size_t kNumEcho1Rpcs = 100;
  const size_t kNumEchoRpcs = 5;
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(2, 3)},
  });
  EdsResourceArgs args3({
      {"locality0", CreateEndpointsForBackends(3, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args3, kNewEdsService3Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  Cluster new_cluster3 = default_cluster_;
  new_cluster3.set_name(kNewCluster3Name);
  new_cluster3.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService3Name);
  balancer_->ads_service()->SetCdsResource(new_cluster3);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->set_exact_match("POST");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  auto route2 = route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher2 = route2->mutable_match()->add_headers();
  header_matcher2->set_name("header2");
  header_matcher2->mutable_range_match()->set_start(1);
  header_matcher2->mutable_range_match()->set_end(1000);
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  auto route3 = route_config.mutable_virtual_hosts(0)->add_routes();
  route3->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher3 = route3->mutable_match()->add_headers();
  header_matcher3->set_name("header3");
  header_matcher3->mutable_safe_regex_match()->set_regex("[a-z]*");
  route3->mutable_route()->set_cluster(kNewCluster3Name);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  // Send headers which will mismatch each route
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"header1", "POST"},
      {"header2", "1000"},
      {"header3", "123"},
      {"header1", "GET"},
  };
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs,
                 RpcOptions().set_metadata(metadata));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs,
                 RpcOptions()
                     .set_rpc_service(SERVICE_ECHO1)
                     .set_rpc_method(METHOD_ECHO1)
                     .set_metadata(metadata));
  // Verify that only the default backend got RPCs since all previous routes
  // were mismatched.
  for (size_t i = 1; i < 4; ++i) {
    EXPECT_EQ(0, backends_[i]->backend_service()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service1()->request_count());
    EXPECT_EQ(0, backends_[i]->backend_service2()->request_count());
  }
  EXPECT_EQ(kNumEchoRpcs, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(kNumEcho1Rpcs, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service2()->request_count());
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(LdsRdsTest, XdsRoutingChangeRoutesWithoutChangingClusters) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster";
  const char* kNewEdsServiceName = "new_eds_service_name";
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName));
  // Populate new CDS resources.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populating Route Configurations for LDS.
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster(kNewClusterName);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  // Make sure all backends are up and that requests for each RPC
  // service go to the right backends.
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false));
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO2));
  // Requests for services Echo and Echo2 should have gone to backend 0.
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(1, backends_[0]->backend_service2()->request_count());
  // Requests for service Echo1 should have gone to backend 1.
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service2()->request_count());
  // Now send an update that changes the first route to match a
  // different RPC service, and wait for the client to make the change.
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest2Service/");
  SetRouteConfiguration(balancer_.get(), route_config);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO2));
  // Now repeat the earlier test, making sure all traffic goes to the
  // right place.
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO2));
  // Requests for services Echo and Echo1 should have gone to backend 0.
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[0]->backend_service1()->request_count());
  EXPECT_EQ(0, backends_[0]->backend_service2()->request_count());
  // Requests for service Echo2 should have gone to backend 1.
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service1()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service2()->request_count());
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
