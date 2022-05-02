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
#include "src/proto/grpc/testing/xds/v3/fault.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "test/cpp/end2end/xds/no_op_http_filter.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::fault::v3::HTTPFault;
using std::chrono::system_clock;

using LdsTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, LdsTest, ::testing::Values(XdsTestType()),
                         &XdsTestType::Name);

// Tests that LDS client should send a NACK if there is no API listener in the
// Listener in the LDS response.
TEST_P(LdsTest, NoApiListener) {
  auto listener = default_listener_;
  listener.clear_api_listener();
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Listener has neither address nor ApiListener"));
}

// Tests that LDS client should send a NACK if the route_specifier in the
// http_connection_manager is neither inlined route_config nor RDS.
TEST_P(LdsTest, WrongRouteSpecifier) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.mutable_scoped_routes();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "HttpConnectionManager neither has inlined route_config nor RDS."));
}

// Tests that LDS client should send a NACK if the rds message in the
// http_connection_manager is missing the config_source field.
TEST_P(LdsTest, RdsMissingConfigSource) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.mutable_rds()->set_route_config_name(
      kDefaultRouteConfigurationName);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "HttpConnectionManager missing config_source for RDS."));
}

// Tests that LDS client should send a NACK if the rds message in the
// http_connection_manager has a config_source field that does not specify
// ADS or SELF.
TEST_P(LdsTest, RdsConfigSourceDoesNotSpecifyAdsOrSelf) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->set_path("/foo/bar");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  balancer_->ads_service()->SetLdsResource(listener);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("HttpConnectionManager ConfigSource for "
                                   "RDS does not specify ADS or SELF."));
}

// Tests that LDS client accepts the rds message in the
// http_connection_manager with a config_source field that specifies ADS.
TEST_P(LdsTest, AcceptsRdsConfigSourceOfTypeAds) {
  CreateAndStartBackends(1);
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_ads();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that we NACK non-terminal filters at the end of the list.
TEST_P(LdsTest, NacksNonTerminalHttpFilterAtEndOfList) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.client_only_http_filter");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "non-terminal filter for config type grpc.testing"
                  ".client_only_http_filter is the last filter in the chain"));
}

// Test that we NACK terminal filters that are not at the end of the list.
TEST_P(LdsTest, NacksTerminalFilterBeforeEndOfList) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  // The default_listener_ has a terminal router filter by default. Add an
  // additional filter.
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("grpc.testing.terminal_http_filter");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.terminal_http_filter");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "terminal filter for config type envoy.extensions.filters.http"
          ".router.v3.Router must be the last filter in the chain"));
}

// Test that we NACK empty filter names.
TEST_P(LdsTest, RejectsEmptyHttpFilterName) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->Clear();
  filter->mutable_typed_config()->PackFrom(Listener());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("empty filter name at index 0"));
}

// Test that we NACK duplicate HTTP filter names.
TEST_P(LdsTest, RejectsDuplicateHttpFilterName) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  http_connection_manager.mutable_http_filters(0)
      ->mutable_typed_config()
      ->PackFrom(HTTPFault());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("duplicate HTTP filter name: router"));
}

// Test that we NACK unknown filter types.
TEST_P(LdsTest, RejectsUnknownHttpFilterType) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(Listener());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types.
TEST_P(LdsTest, IgnoresOptionalUnknownHttpFilterType) {
  CreateAndStartBackends(1);
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(Listener());
  filter->set_is_optional(true);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs.
TEST_P(LdsTest, RejectsHttpFilterWithoutConfig) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->Clear();
  filter->set_name("unknown");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs.
TEST_P(LdsTest, IgnoresOptionalHttpFilterWithoutConfig) {
  CreateAndStartBackends(1);
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->Clear();
  filter->set_name("unknown");
  filter->set_is_optional(true);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter configs.
TEST_P(LdsTest, RejectsUnparseableHttpFilterType) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(listener);
  filter->mutable_typed_config()->set_type_url(
      "type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "filter config for type "
          "envoy.extensions.filters.http.fault.v3.HTTPFault failed to parse"));
}

// Test that we NACK HTTP filters unsupported on client-side.
TEST_P(LdsTest, RejectsHttpFiltersNotSupportedOnClients) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("grpc.testing.server_only_http_filter");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.server_only_http_filter");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("Filter grpc.testing.server_only_http_filter is not "
                           "supported on clients"));
}

// Test that we ignore optional HTTP filters unsupported on client-side.
TEST_P(LdsTest, IgnoresOptionalHttpFiltersNotSupportedOnClients) {
  CreateAndStartBackends(1);
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  *http_connection_manager.add_http_filters() =
      http_connection_manager.http_filters(0);
  auto* filter = http_connection_manager.mutable_http_filters(0);
  filter->set_name("grpc.testing.server_only_http_filter");
  filter->mutable_typed_config()->set_type_url(
      "grpc.testing.server_only_http_filter");
  filter->set_is_optional(true);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK non-zero xff_num_trusted_hops
TEST_P(LdsTest, RejectsNonZeroXffNumTrusterHops) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.set_xff_num_trusted_hops(1);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("'xff_num_trusted_hops' must be zero"));
}

// Test that we NACK non-empty original_ip_detection_extensions
TEST_P(LdsTest, RejectsNonEmptyOriginalIpDetectionExtensions) {
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  http_connection_manager.add_original_ip_detection_extensions();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("'original_ip_detection_extensions' must be empty"));
}

using LdsV2Test = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, LdsV2Test,
                         ::testing::Values(XdsTestType().set_use_v2()),
                         &XdsTestType::Name);

// Tests that we ignore the HTTP filter list in v2.
// TODO(roth): The test framework is not set up to allow us to test
// the server sending v2 resources when the client requests v3, so this
// just tests a pure v2 setup.  When we have time, fix this.
TEST_P(LdsV2Test, IgnoresHttpFilters) {
  CreateAndStartBackends(1);
  auto listener = default_listener_;
  HttpConnectionManager http_connection_manager;
  listener.mutable_api_listener()->mutable_api_listener()->UnpackTo(
      &http_connection_manager);
  auto* filter = http_connection_manager.add_http_filters();
  filter->set_name("unknown");
  filter->mutable_typed_config()->PackFrom(Listener());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(
      http_connection_manager);
  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION);
}

using LdsRdsTest = XdsEnd2endTest;

// Test with and without RDS.
// Also test with v2 and RDS to ensure that we handle those cases.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, LdsRdsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_rds_testing(),
                      XdsTestType().set_enable_rds_testing().set_use_v2()),
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

// Tests that XdsClient sends an ACK for the RouteConfiguration, whether or
// not it was inlined into the LDS response.
TEST_P(LdsRdsTest, Vanilla) {
  (void)SendRpc();
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Make sure we actually used the RPC service for the right version of xDS.
  EXPECT_EQ(balancer_->ads_service()->seen_v2_client(), GetParam().use_v2());
  EXPECT_NE(balancer_->ads_service()->seen_v3_client(), GetParam().use_v2());
}

TEST_P(LdsRdsTest, DefaultRouteSpecifiesSlashPrefix) {
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_prefix("/");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION);
}

// Tests that we go into TRANSIENT_FAILURE if the Listener is removed.
TEST_P(LdsRdsTest, ListenerRemoved) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION);
  // Unset LDS resource.
  balancer_->ads_service()->UnsetResource(kLdsTypeUrl, kServerName);
  // Wait for RPCs to start failing.
  do {
  } while (SendRpc(RpcOptions(), nullptr).ok());
  // Make sure RPCs are still failing.
  CheckRpcSendFailure(DEBUG_LOCATION,
                      CheckRpcSendFailureOptions().set_times(1000));
  // Make sure we ACK'ed the update.
  auto response_state = balancer_->ads_service()->lds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client ACKs but fails if matching domain can't be found in
// the LDS response.
TEST_P(LdsRdsTest, NoMatchedDomain) {
  RouteConfiguration route_config = default_route_config_;
  route_config.mutable_virtual_hosts(0)->clear_domains();
  route_config.mutable_virtual_hosts(0)->add_domains("unmatched_domain");
  SetRouteConfiguration(balancer_.get(), route_config);
  CheckRpcSendFailure(DEBUG_LOCATION);
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

// Tests that LDS client should ignore route which has query_parameters.
TEST_P(LdsRdsTest, RouteMatchHasQueryParameters) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_match()->add_query_parameters();
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should send a ACK if route match has a prefix
// that is either empty or a single slash
TEST_P(LdsRdsTest, RouteMatchHasValidPrefixEmptyOrSingleSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("");
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("/");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  (void)SendRpc();
  const auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that LDS client should ignore route which has a path
// prefix string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixNoLeadingSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has a prefix
// string with more than 2 slashes.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixExtraContent) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/Echo1/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has a prefix
// string "//".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPrefixDoubleSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("//");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// but it's empty.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathEmptyPath) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string does not start with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathNoLeadingSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("grpc.testing.EchoTest1Service/Echo1");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that has too many slashes; for example, ends with "/".
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathTooManySlashes) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/Echo1/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that has only 1 slash: missing "/" between service and method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathOnlyOneSlash) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service.Echo1");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that is missing service.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingService) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("//Echo1");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Tests that LDS client should ignore route which has path
// string that is missing method.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathMissingMethod) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_path("/grpc.testing.EchoTest1Service/");
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("No valid routes specified."));
}

// Test that LDS client should reject route which has invalid path regex.
TEST_P(LdsRdsTest, RouteMatchHasInvalidPathRegex) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->mutable_safe_regex()->set_regex("a[z-a]");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "path matcher: Invalid regex string specified in matcher."));
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
  CheckRpcSendFailure(DEBUG_LOCATION,
                      CheckRpcSendFailureOptions().set_expected_error_code(
                          StatusCode::UNAVAILABLE));
}

TEST_P(LdsRdsTest, RouteActionClusterHasEmptyClusterName) {
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  route1->mutable_route()->set_cluster("");
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("RouteAction cluster contains empty cluster name."));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetHasIncorrectTotalWeightSet) {
  const size_t kWeight75 = 75;
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + 1);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "RouteAction weighted_cluster has incorrect total weight"));
}

TEST_P(LdsRdsTest, RouteActionWeightedClusterHasZeroTotalWeight) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  weighted_cluster1->mutable_weight()->set_value(0);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(0);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "RouteAction weighted_cluster has no valid clusters specified."));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetClusterHasEmptyClusterName) {
  const size_t kWeight75 = 75;
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name("");
  weighted_cluster1->mutable_weight()->set_value(kWeight75);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("RouteAction weighted_cluster cluster "
                                   "contains empty cluster name."));
}

TEST_P(LdsRdsTest, RouteActionWeightedTargetClusterHasNoWeight) {
  const size_t kWeight75 = 75;
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* weighted_cluster1 =
      route1->mutable_route()->mutable_weighted_clusters()->add_clusters();
  weighted_cluster1->set_name(kNewCluster1Name);
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75);
  auto* default_route = route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "RouteAction weighted_cluster cluster missing weight"));
}

TEST_P(LdsRdsTest, RouteHeaderMatchInvalidRegex) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->mutable_safe_regex_match()->set_regex("a[z-a]");
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "header matcher: Invalid regex string specified in matcher."));
}

TEST_P(LdsRdsTest, RouteHeaderMatchInvalidRange) {
  const char* kNewCluster1Name = "new_cluster_1";
  RouteConfiguration route_config = default_route_config_;
  auto* route1 = route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  route1->mutable_match()->set_prefix("/grpc.testing.EchoTest1Service/");
  auto* header_matcher1 = route1->mutable_match()->add_headers();
  header_matcher1->set_name("header1");
  header_matcher1->mutable_range_match()->set_start(1001);
  header_matcher1->mutable_range_match()->set_end(1000);
  route1->mutable_route()->set_cluster(kNewCluster1Name);
  SetRouteConfiguration(balancer_.get(), route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "header matcher: Invalid range specifier specified: end cannot be "
          "smaller than start."));
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
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
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
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  WaitForAllBackends(DEBUG_LOCATION, 1, 3, WaitForBackendOptions(),
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
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
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
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  WaitForAllBackends(DEBUG_LOCATION, 1, 3, WaitForBackendOptions(),
                     RpcOptions().set_rpc_service(SERVICE_ECHO1));
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs7525,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
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
  WaitForAllBackends(DEBUG_LOCATION, 3, 4);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEchoRpcs);
  CheckRpcSendOk(DEBUG_LOCATION, kNumEcho1Rpcs5050,
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
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
  route1->mutable_route()
      ->mutable_weighted_clusters()
      ->mutable_total_weight()
      ->set_value(kWeight75 + kWeight25);
  auto* default_route = new_route_config.mutable_virtual_hosts(0)->add_routes();
  default_route->mutable_match()->set_prefix("");
  default_route->mutable_route()->set_cluster(kDefaultClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  WaitForBackend(DEBUG_LOCATION, 0);
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(),
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
  WaitForBackend(DEBUG_LOCATION, 2, WaitForBackendOptions(),
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
  WaitForBackend(DEBUG_LOCATION, 3, WaitForBackendOptions(),
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
  // Bring down the current backend: 0, this will delay route picking time,
  // resulting in un-committed RPCs.
  ShutdownBackend(0);
  // Send a RouteConfiguration with a default route that points to
  // backend 0.
  RouteConfiguration new_route_config = default_route_config_;
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Send exactly one RPC with no deadline and with wait_for_ready=true.
  // This RPC will not complete until after backend 0 is started.
  std::thread sending_rpc([this]() {
    CheckRpcSendOk(DEBUG_LOCATION, 1,
                   RpcOptions().set_wait_for_ready(true).set_timeout_ms(0));
  });
  // Send a non-wait_for_ready RPC which should fail, this will tell us
  // that the client has received the update and attempted to connect.
  const Status status = SendRpc(RpcOptions().set_timeout_ms(0));
  EXPECT_FALSE(status.ok());
  // Send a update RouteConfiguration to use backend 1.
  auto* default_route =
      new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  default_route->mutable_route()->set_cluster(kNewClusterName);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // Wait for RPCs to go to the new backend: 1, this ensures that the client
  // has processed the update.
  WaitForBackend(
      DEBUG_LOCATION, 1,
      WaitForBackendOptions().set_reset_counters(false).set_allow_failures(
          true));
  // Bring up the previous backend: 0, this will allow the delayed RPC to
  // finally call on_call_committed upon completion.
  StartBackend(0);
  sending_rpc.join();
  // Make sure RPCs go to the correct backend:
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(1, backends_[1]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRoutingApplyXdsTimeout) {
  const int64_t kTimeoutMillis = 500;
  const int64_t kTimeoutNano = kTimeoutMillis * 1000000;
  const int64_t kTimeoutGrpcTimeoutHeaderMaxSecond = 1;
  const int64_t kTimeoutMaxStreamDurationSecond = 2;
  const int64_t kTimeoutHttpMaxStreamDurationSecond = 3;
  const int64_t kTimeoutApplicationSecond = 4;
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
  auto* duration =
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutHttpMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
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
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
  duration = max_stream_duration->mutable_grpc_timeout_header_max();
  duration->set_seconds(kTimeoutGrpcTimeoutHeaderMaxSecond);
  duration->set_nanos(kTimeoutNano);
  // route 2: Set max_stream_duration of 2.5 seconds
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  max_stream_duration = route2->mutable_route()->mutable_max_stream_duration();
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
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
      t0 + grpc_core::Duration::Seconds(kTimeoutGrpcTimeoutHeaderMaxSecond) +
      grpc_core::Duration::Milliseconds(kTimeoutMillis);
  grpc_core::Timestamp t2 =
      t0 + grpc_core::Duration::Seconds(kTimeoutMaxStreamDurationSecond) +
      grpc_core::Duration::Milliseconds(kTimeoutMillis);
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions()
                               .set_rpc_service(SERVICE_ECHO1)
                               .set_rpc_method(METHOD_ECHO1)
                               .set_wait_for_ready(true)
                               .set_timeout_ms(grpc_core::Duration::Seconds(
                                                   kTimeoutApplicationSecond)
                                                   .millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
  // Test max_stream_duration of 2.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + grpc_core::Duration::Seconds(kTimeoutMaxStreamDurationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  t2 = t0 + grpc_core::Duration::Seconds(kTimeoutHttpMaxStreamDurationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions()
                               .set_rpc_service(SERVICE_ECHO2)
                               .set_rpc_method(METHOD_ECHO2)
                               .set_wait_for_ready(true)
                               .set_timeout_ms(grpc_core::Duration::Seconds(
                                                   kTimeoutApplicationSecond)
                                                   .millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
  // Test http_stream_duration of 3.5 seconds applied
  t0 = NowFromCycleCounter();
  t1 = t0 + grpc_core::Duration::Seconds(kTimeoutHttpMaxStreamDurationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  t2 = t0 + grpc_core::Duration::Seconds(kTimeoutApplicationSecond) +
       grpc_core::Duration::Milliseconds(kTimeoutMillis);
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              grpc_core::Duration::Seconds(kTimeoutApplicationSecond).millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_THAT(NowFromCycleCounter(), AdjustedClockInRange(t1, t2));
}

TEST_P(LdsRdsTest, XdsRoutingApplyApplicationTimeoutWhenXdsTimeoutExplicit0) {
  const int64_t kTimeoutNano = 500000000;
  const int64_t kTimeoutMaxStreamDurationSecond = 2;
  const int64_t kTimeoutHttpMaxStreamDurationSecond = 3;
  const int64_t kTimeoutApplicationSecond = 4;
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
  auto* duration =
      http_connection_manager.mutable_common_http_protocol_options()
          ->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutHttpMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
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
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(kTimeoutMaxStreamDurationSecond);
  duration->set_nanos(kTimeoutNano);
  duration = max_stream_duration->mutable_grpc_timeout_header_max();
  duration->set_seconds(0);
  duration->set_nanos(0);
  // route 2: Set max_stream_duration to 0
  auto* route2 = new_route_config.mutable_virtual_hosts(0)->add_routes();
  route2->mutable_match()->set_path("/grpc.testing.EchoTest2Service/Echo2");
  route2->mutable_route()->set_cluster(kNewCluster2Name);
  max_stream_duration = route2->mutable_route()->mutable_max_stream_duration();
  duration = max_stream_duration->mutable_max_stream_duration();
  duration->set_seconds(0);
  duration->set_nanos(0);
  // Set listener and route config.
  SetListenerAndRouteConfiguration(balancer_.get(), std::move(listener),
                                   new_route_config);
  // Test application timeout is applied for route 1
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_rpc_service(SERVICE_ECHO1)
                  .set_rpc_method(METHOD_ECHO1)
                  .set_wait_for_ready(true)
                  .set_timeout_ms(kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto ellapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
  // Test application timeout is applied for route 2
  t0 = system_clock::now();
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions()
                  .set_rpc_service(SERVICE_ECHO2)
                  .set_rpc_method(METHOD_ECHO2)
                  .set_wait_for_ready(true)
                  .set_timeout_ms(kTimeoutApplicationSecond * 1000))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  ellapsed_nano_seconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
      system_clock::now() - t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
}

TEST_P(LdsRdsTest, XdsRoutingApplyApplicationTimeoutWhenHttpTimeoutExplicit0) {
  const int64_t kTimeoutApplicationSecond = 4;
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
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              grpc_core::Duration::Seconds(kTimeoutApplicationSecond).millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto ellapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
}

// Test to ensure application-specified deadline won't be affected when
// the xDS config does not specify a timeout.
TEST_P(LdsRdsTest, XdsRoutingWithOnlyApplicationTimeout) {
  const int64_t kTimeoutApplicationSecond = 4;
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  auto t0 = system_clock::now();
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_wait_for_ready(true).set_timeout_ms(
              grpc_core::Duration::Seconds(kTimeoutApplicationSecond).millis()))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  auto ellapsed_nano_seconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(system_clock::now() -
                                                           t0);
  EXPECT_GT(ellapsed_nano_seconds.count(),
            kTimeoutApplicationSecond * 1000000000);
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
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_server_expected_error(StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::CANCELLED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_server_expected_error(StatusCode::INTERNAL))
          .set_expected_error_code(StatusCode::INTERNAL));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::RESOURCE_EXHAUSTED))
          .set_expected_error_code(StatusCode::RESOURCE_EXHAUSTED));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_server_expected_error(StatusCode::UNAVAILABLE))
          .set_expected_error_code(StatusCode::UNAVAILABLE));
  EXPECT_EQ(kNumRetries + 1, backends_[0]->backend_service()->request_count());
  ResetBackendCounters();
  // Ensure we don't retry on an unsupported status.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::UNAUTHENTICATED))
          .set_expected_error_code(StatusCode::UNAUTHENTICATED));
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
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
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
  auto base_interval =
      retry_policy->mutable_retry_back_off()->mutable_base_interval();
  // Set backoff to 1 second, 1/2 of rpc timeout of 2 second.
  base_interval->set_seconds(1 * grpc_test_slowdown_factor());
  base_interval->set_nanos(0);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // No need to set max interval and just let it be the default of 10x of base.
  // We expect 1 retry before the RPC times out with DEADLINE_EXCEEDED.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_timeout_ms(2500).set_server_expected_error(
                  StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
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
  auto base_interval =
      retry_policy->mutable_retry_back_off()->mutable_base_interval();
  // Set backoff to 1 second.
  base_interval->set_seconds(1 * grpc_test_slowdown_factor());
  base_interval->set_nanos(0);
  auto max_interval =
      retry_policy->mutable_retry_back_off()->mutable_max_interval();
  // Set max interval to be the same as base, so 2 retries will take 2 seconds
  // and both retries will take place before the 2.5 seconds rpc timeout.
  // Tested to ensure if max is not set, this test will be the same as
  // XdsRetryPolicyLongBackOff and we will only see 1 retry in that case.
  max_interval->set_seconds(1 * grpc_test_slowdown_factor());
  max_interval->set_nanos(0);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  // We expect 2 retry before the RPC times out with DEADLINE_EXCEEDED.
  CheckRpcSendFailure(
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(
              RpcOptions().set_timeout_ms(2500).set_server_expected_error(
                  StatusCode::CANCELLED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
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
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
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
      DEBUG_LOCATION,
      CheckRpcSendFailureOptions()
          .set_rpc_options(RpcOptions().set_server_expected_error(
              StatusCode::DEADLINE_EXCEEDED))
          .set_expected_error_code(StatusCode::DEADLINE_EXCEEDED));
  EXPECT_EQ(1, backends_[0]->backend_service()->request_count());
}

TEST_P(LdsRdsTest, XdsRetryPolicyInvalidNumRetriesZero) {
  CreateAndStartBackends(1);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("deadline-exceeded");
  // Setting num_retries to zero is not valid.
  retry_policy->mutable_num_retries()->set_value(0);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "RouteAction RetryPolicy num_retries set to invalid value 0."));
}

TEST_P(LdsRdsTest, XdsRetryPolicyRetryBackOffMissingBaseInterval) {
  CreateAndStartBackends(1);
  // Populate new EDS resources.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct route config to set retry policy.
  RouteConfiguration new_route_config = default_route_config_;
  auto* route1 = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* retry_policy = route1->mutable_route()->mutable_retry_policy();
  retry_policy->set_retry_on("deadline-exceeded");
  retry_policy->mutable_num_retries()->set_value(1);
  // RetryBackoff is there but base interval is missing.
  auto max_interval =
      retry_policy->mutable_retry_back_off()->mutable_max_interval();
  max_interval->set_seconds(0);
  max_interval->set_nanos(250000000);
  SetRouteConfiguration(balancer_.get(), new_route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr(
          "RouteAction RetryPolicy RetryBackoff missing base interval."));
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
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(),
                 header_match_rpc_options);
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
  WaitForBackend(DEBUG_LOCATION, 0,
                 WaitForBackendOptions().set_reset_counters(false));
  WaitForBackend(DEBUG_LOCATION, 1,
                 WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  WaitForBackend(DEBUG_LOCATION, 0,
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
  WaitForBackend(DEBUG_LOCATION, 1, WaitForBackendOptions(),
                 RpcOptions().set_rpc_service(SERVICE_ECHO2));
  // Now repeat the earlier test, making sure all traffic goes to the
  // right place.
  WaitForBackend(DEBUG_LOCATION, 0,
                 WaitForBackendOptions().set_reset_counters(false));
  WaitForBackend(DEBUG_LOCATION, 0,
                 WaitForBackendOptions().set_reset_counters(false),
                 RpcOptions().set_rpc_service(SERVICE_ECHO1));
  WaitForBackend(DEBUG_LOCATION, 1,
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

// Test that we NACK unknown filter types in VirtualHost.
TEST_P(LdsRdsTest, RejectsUnknownHttpFilterTypeInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(Listener());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in VirtualHost.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInVirtualHost) {
  CreateAndStartBackends(1);
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in VirtualHost.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we NACK filters without configs in FilterConfig in VirtualHost.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInFilterConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      ::envoy::config::route::v3::FilterConfig());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in VirtualHost.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter types in VirtualHost.
TEST_P(LdsRdsTest, RejectsUnparseableHttpFilterTypeInVirtualHost) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config =
      route_config.mutable_virtual_hosts(0)->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("router filter does not support config override"));
}

// Test that we NACK unknown filter types in Route.
TEST_P(LdsRdsTest, RejectsUnknownHttpFilterTypeInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(Listener());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in Route.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in Route.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we NACK filters without configs in FilterConfig in Route.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInFilterConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      ::envoy::config::route::v3::FilterConfig());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in Route.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter types in Route.
TEST_P(LdsRdsTest, RejectsUnparseableHttpFilterTypeInRoute) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* per_filter_config = route_config.mutable_virtual_hosts(0)
                                ->mutable_routes(0)
                                ->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("router filter does not support config override"));
}

// Test that we NACK unknown filter types in ClusterWeight.
TEST_P(LdsRdsTest, RejectsUnknownHttpFilterTypeInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(Listener());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("no filter registered for config type "
                                   "envoy.config.listener.v3.Listener"));
}

// Test that we ignore optional unknown filter types in ClusterWeight.
TEST_P(LdsRdsTest, IgnoresOptionalUnknownHttpFilterTypeInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.mutable_config()->PackFrom(Listener());
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK filters without configs in ClusterWeight.
TEST_P(LdsRdsTest, RejectsHttpFilterWithoutConfigInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"];
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we NACK filters without configs in FilterConfig in ClusterWeight.
TEST_P(LdsRdsTest,
       RejectsHttpFilterWithoutConfigInFilterConfigInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      ::envoy::config::route::v3::FilterConfig());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "no filter config specified for filter name unknown"));
}

// Test that we ignore optional filters without configs in ClusterWeight.
TEST_P(LdsRdsTest, IgnoresOptionalHttpFilterWithoutConfigInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  CreateAndStartBackends(1);
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  ::envoy::config::route::v3::FilterConfig filter_config;
  filter_config.set_is_optional(true);
  (*per_filter_config)["unknown"].PackFrom(filter_config);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  auto response_state = RouteConfigurationResponseState(balancer_.get());
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Test that we NACK unparseable filter types in ClusterWeight.
TEST_P(LdsRdsTest, RejectsUnparseableHttpFilterTypeInClusterWeight) {
  if (GetParam().use_v2()) return;  // Filters supported in v3 only.
  RouteConfiguration route_config = default_route_config_;
  auto* cluster_weight = route_config.mutable_virtual_hosts(0)
                             ->mutable_routes(0)
                             ->mutable_route()
                             ->mutable_weighted_clusters()
                             ->add_clusters();
  cluster_weight->set_name(kDefaultClusterName);
  cluster_weight->mutable_weight()->set_value(100);
  auto* per_filter_config = cluster_weight->mutable_typed_per_filter_config();
  (*per_filter_config)["unknown"].PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   route_config);
  const auto response_state = WaitForRdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("router filter does not support config override"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  GPR_GLOBAL_CONFIG_SET(grpc_client_channel_backup_poll_interval_ms, 1);
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  gpr_setenv("grpc_cfstream", "0");
#endif
  grpc_init();
  grpc_core::XdsHttpFilterRegistry::RegisterFilter(
      absl::make_unique<grpc::testing::NoOpHttpFilter>(
          "grpc.testing.client_only_http_filter",
          /* supported_on_clients = */ true, /* supported_on_servers = */ false,
          /* is_terminal_filter */ false),
      {"grpc.testing.client_only_http_filter"});
  grpc_core::XdsHttpFilterRegistry::RegisterFilter(
      absl::make_unique<grpc::testing::NoOpHttpFilter>(
          "grpc.testing.server_only_http_filter",
          /* supported_on_clients = */ false, /* supported_on_servers = */ true,
          /* is_terminal_filter */ false),
      {"grpc.testing.server_only_http_filter"});
  grpc_core::XdsHttpFilterRegistry::RegisterFilter(
      absl::make_unique<grpc::testing::NoOpHttpFilter>(
          "grpc.testing.terminal_http_filter",
          /* supported_on_clients = */ true, /* supported_on_servers = */ true,
          /* is_terminal_filter */ true),
      {"grpc.testing.terminal_http_filter"});
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
