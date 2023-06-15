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

#include <numeric>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/config/config_vars.h"
#include "src/proto/grpc/testing/xds/v3/orca_load_report.pb.h"
#include "test/cpp/end2end/connection_attempt_injector.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::cluster::v3::CircuitBreakers;
using ::envoy::config::cluster::v3::RoutingPriority;
using ::envoy::config::core::v3::HealthStatus;
using ::envoy::type::v3::FractionalPercent;

using ClientStats = LrsServiceImpl::ClientStats;

constexpr char kLbDropType[] = "lb";
constexpr char kThrottleDropType[] = "throttle";
constexpr char kStatusMessageDropPrefix[] = "EDS-configured drop: ";

//
// CDS tests
//

using CdsTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, CdsTest, ::testing::Values(XdsTestType()),
                         &XdsTestType::Name);

// Tests that CDS client should send an ACK upon correct CDS response.
TEST_P(CdsTest, Vanilla) {
  (void)SendRpc();
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Testing just one example of an invalid resource here.
// Unit tests for XdsClusterResourceType have exhaustive tests for all
// of the invalid cases.
TEST_P(CdsTest, InvalidClusterResource) {
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::STATIC);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_EQ(response_state->error_message,
            "xDS response validation errors: ["
            "resource index 0: cluster_name: "
            "INVALID_ARGUMENT: errors validating Cluster resource: ["
            "field:type error:unknown discovery type]]");
}

// Tests that we don't trigger does-not-exist callbacks for a resource
// that was previously valid but is updated to be invalid.
TEST_P(CdsTest, InvalidClusterStillExistsIfPreviouslyCached) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Check that everything works.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Now send an update changing the Cluster to be invalid.
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::STATIC);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state =
      WaitForCdsNack(DEBUG_LOCATION, RpcOptions(), StatusCode::OK);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_EQ(response_state->error_message,
            "xDS response validation errors: ["
            "resource index 0: cluster_name: "
            "INVALID_ARGUMENT: errors validating Cluster resource: ["
            "field:type error:unknown discovery type]]");
  CheckRpcSendOk(DEBUG_LOCATION);
}

// Tests round robin is not implacted by the endpoint weight, and that the
// localities in a locality map are picked according to their weights.
TEST_P(CdsTest, EndpointWeightDoesNotImpactWeightedRoundRobin) {
  CreateAndStartBackends(2);
  const int kLocalityWeight0 = 2;
  const int kLocalityWeight1 = 8;
  const int kTotalLocalityWeight = kLocalityWeight0 + kLocalityWeight1;
  const double kLocalityWeightRate0 =
      static_cast<double>(kLocalityWeight0) / kTotalLocalityWeight;
  const double kLocalityWeightRate1 =
      static_cast<double>(kLocalityWeight1) / kTotalLocalityWeight;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kLocalityWeightRate0, kErrorTolerance);
  // ADS response contains 2 localities, each of which contains 1 backend.
  EdsResourceArgs args({
      {"locality0",
       {CreateEndpoint(0, HealthStatus::UNKNOWN, 8)},
       kLocalityWeight0},
      {"locality1",
       {CreateEndpoint(1, HealthStatus::UNKNOWN, 2)},
       kLocalityWeight1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for both backends to be ready.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  // The locality picking rates should be roughly equal to the expectation.
  const double locality_picked_rate_0 =
      static_cast<double>(backends_[0]->backend_service()->request_count()) /
      kNumRpcs;
  const double locality_picked_rate_1 =
      static_cast<double>(backends_[1]->backend_service()->request_count()) /
      kNumRpcs;
  EXPECT_THAT(locality_picked_rate_0,
              ::testing::DoubleNear(kLocalityWeightRate0, kErrorTolerance));
  EXPECT_THAT(locality_picked_rate_1,
              ::testing::DoubleNear(kLocalityWeightRate1, kErrorTolerance));
}

// In most of our tests, we use different names for different resource
// types, to make sure that there are no cut-and-paste errors in the code
// that cause us to look at data for the wrong resource type.  So we add
// this test to make sure that the EDS resource name defaults to the
// cluster name if not specified in the CDS resource.
TEST_P(CdsTest, EdsServiceNameDefaultsToClusterName) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kDefaultClusterName));
  Cluster cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->clear_service_name();
  balancer_->ads_service()->SetCdsResource(cluster);
  CheckRpcSendOk(DEBUG_LOCATION, /*times=*/1,
                 RpcOptions().set_timeout_ms(5000));
}

// Tests switching over from one cluster to another.
TEST_P(CdsTest, ChangeClusters) {
  CreateAndStartBackends(2);
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
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
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Wait for all new backends to be used.
  WaitForAllBackends(DEBUG_LOCATION, 1, 2);
}

TEST_P(CdsTest, CircuitBreaking) {
  CreateAndStartBackends(1);
  constexpr size_t kMaxConcurrentRequests = 10;
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Update CDS resource to set max concurrent request.
  CircuitBreakers circuit_breaks;
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Send exactly max_concurrent_requests long RPCs.
  LongRunningRpc rpcs[kMaxConcurrentRequests];
  for (size_t i = 0; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].StartRpc(stub_.get());
  }
  // Wait for all RPCs to be in flight.
  while (backends_[0]->backend_service()->RpcsWaitingForClientCancel() <
         kMaxConcurrentRequests) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_micros(1 * 1000, GPR_TIMESPAN)));
  }
  // Sending a RPC now should fail, the error message should tell us
  // we hit the max concurrent requests limit and got dropped.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "circuit breaker drop");
  // Cancel one RPC to allow another one through.
  rpcs[0].CancelRpc();
  // Add a sleep here to ensure the RPC cancellation has completed correctly
  // before trying the next RPC. There maybe a slight delay between return of
  // CANCELLED RPC status and update of internal state tracking the number of
  // concurrent active requests.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(1000, GPR_TIMESPAN)));
  CheckRpcSendOk(DEBUG_LOCATION);
  // Clean up.
  for (size_t i = 1; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].CancelRpc();
  }
}

TEST_P(CdsTest, CircuitBreakingMultipleChannelsShareCallCounter) {
  CreateAndStartBackends(1);
  constexpr size_t kMaxConcurrentRequests = 10;
  // Populate new EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Update CDS resource to set max concurrent request.
  CircuitBreakers circuit_breaks;
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto channel2 = CreateChannel();
  auto stub2 = grpc::testing::EchoTestService::NewStub(channel2);
  // Send exactly max_concurrent_requests long RPCs, alternating between
  // the two channels.
  LongRunningRpc rpcs[kMaxConcurrentRequests];
  for (size_t i = 0; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].StartRpc(i % 2 == 0 ? stub_.get() : stub2.get());
  }
  // Wait for all RPCs to be in flight.
  while (backends_[0]->backend_service()->RpcsWaitingForClientCancel() <
         kMaxConcurrentRequests) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_micros(1 * 1000, GPR_TIMESPAN)));
  }
  // Sending a RPC now should fail, the error message should tell us
  // we hit the max concurrent requests limit and got dropped.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "circuit breaker drop");
  // Cancel one RPC to allow another one through
  rpcs[0].CancelRpc();
  // Add a sleep here to ensure the RPC cancellation has completed correctly
  // before trying the next RPC. There maybe a slight delay between return of
  // CANCELLED RPC status and update of internal state tracking the number of
  // concurrent active requests.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(1000, GPR_TIMESPAN)));
  CheckRpcSendOk(DEBUG_LOCATION);
  // Clean up.
  for (size_t i = 1; i < kMaxConcurrentRequests; ++i) {
    rpcs[i].CancelRpc();
  }
}

TEST_P(CdsTest, ClusterChangeAfterAdsCallFails) {
  CreateAndStartBackends(2);
  const char* kNewEdsResourceName = "new_eds_resource_name";
  // Populate EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Check that the channel is working.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Stop and restart the balancer.
  balancer_->Shutdown();
  balancer_->Start();
  // Create new EDS resource.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  // Change CDS resource to point to new EDS resource.
  auto cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Make sure client sees the change.
  WaitForBackend(DEBUG_LOCATION, 1);
}

//
// CDS deletion tests
//

class CdsDeletionTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {}  // Individual tests call InitClient().
};

INSTANTIATE_TEST_SUITE_P(XdsTest, CdsDeletionTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

// Tests that we go into TRANSIENT_FAILURE if the Cluster is deleted.
TEST_P(CdsDeletionTest, ClusterDeleted) {
  InitClient();
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // We need to wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION);
  // Unset CDS resource.
  balancer_->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  // Wait for RPCs to start failing.
  SendRpcsUntil(DEBUG_LOCATION, [](const RpcResult& result) {
    if (result.status.ok()) return true;  // Keep going.
    EXPECT_EQ(StatusCode::UNAVAILABLE, result.status.error_code());
    EXPECT_EQ(absl::StrCat("CDS resource \"", kDefaultClusterName,
                           "\" does not exist"),
              result.status.error_message());
    return false;
  });
  // Make sure we ACK'ed the update.
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

// Tests that we ignore Cluster deletions if configured to do so.
TEST_P(CdsDeletionTest, ClusterDeletionIgnored) {
  InitClient(BootstrapBuilder().SetIgnoreResourceDeletion());
  CreateAndStartBackends(2);
  // Bring up client pointing to backend 0 and wait for it to connect.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION, 0, 1);
  // Make sure we ACKed the CDS update.
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Unset CDS resource and wait for client to ACK the update.
  balancer_->ads_service()->UnsetResource(kCdsTypeUrl, kDefaultClusterName);
  const auto deadline = absl::Now() + absl::Seconds(30);
  while (true) {
    ASSERT_LT(absl::Now(), deadline) << "timed out waiting for CDS ACK";
    response_state = balancer_->ads_service()->cds_response_state();
    if (response_state.has_value()) break;
  }
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Make sure we can still send RPCs.
  CheckRpcSendOk(DEBUG_LOCATION);
  // Now recreate the CDS resource pointing to a new EDS resource that
  // specified backend 1, and make sure the client uses it.
  const char* kNewEdsResourceName = "new_eds_resource_name";
  auto cluster = default_cluster_;
  cluster.mutable_eds_cluster_config()->set_service_name(kNewEdsResourceName);
  balancer_->ads_service()->SetCdsResource(cluster);
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kNewEdsResourceName));
  // Wait for client to start using backend 1.
  WaitForAllBackends(DEBUG_LOCATION, 1, 2);
}

//
// EDS tests
//

using EdsTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(
    XdsTest, EdsTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

// Tests that the balancer sends the correct response to the client, and the
// client sends RPCs to the backends using the default child policy.
TEST_P(EdsTest, Vanilla) {
  CreateAndStartBackends(3);
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

TEST_P(EdsTest, IgnoresUnhealthyEndpoints) {
  CreateAndStartBackends(2);
  const size_t kNumRpcsPerAddress = 100;
  auto endpoints = CreateEndpointsForBackends();
  endpoints.push_back(MakeNonExistantEndpoint());
  endpoints.back().health_status = HealthStatus::DRAINING;
  EdsResourceArgs args({
      {"locality0", std::move(endpoints), kDefaultLocalityWeight,
       kDefaultLocalityPriority},
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
}

TEST_P(EdsTest, OneLocalityWithNoEndpoints) {
  CreateAndStartBackends(1);
  // Initial EDS resource has one locality with no endpoints.
  EdsResourceArgs::Locality empty_locality("locality0", {});
  EdsResourceArgs args({std::move(empty_locality)});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // RPCs should fail.
  constexpr char kErrorMessage[] =
      "no children in weighted_target policy: "
      "EDS resource eds_service_name contains empty localities: "
      "\\[\\{region=\"xds_default_locality_region\", "
      "zone=\"xds_default_locality_zone\", sub_zone=\"locality0\"\\}\\]";
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE, kErrorMessage);
  // Send EDS resource that has an endpoint.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // RPCs should eventually succeed.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1, [&](const RpcResult& result) {
    if (!result.status.ok()) {
      EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
      EXPECT_THAT(result.status.error_message(),
                  ::testing::MatchesRegex(kErrorMessage));
    }
  });
}

// This tests the bug described in https://github.com/grpc/grpc/issues/32486.
TEST_P(EdsTest, LocalityBecomesEmptyWithDeactivatedChildStateUpdate) {
  CreateAndStartBackends(1);
  // Initial EDS resource has one locality with no endpoints.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  // EDS update removes all endpoints from the locality.
  EdsResourceArgs::Locality empty_locality("locality0", {});
  args = EdsResourceArgs({std::move(empty_locality)});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for RPCs to start failing.
  constexpr char kErrorMessage[] =
      "no children in weighted_target policy: "
      "EDS resource eds_service_name contains empty localities: "
      "\\[\\{region=\"xds_default_locality_region\", "
      "zone=\"xds_default_locality_zone\", sub_zone=\"locality0\"\\}\\]";
  SendRpcsUntil(DEBUG_LOCATION, [&](const RpcResult& result) {
    if (result.status.ok()) return true;
    EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
    EXPECT_THAT(result.status.error_message(),
                ::testing::MatchesRegex(kErrorMessage));
    return false;
  });
  // Shut down backend.  This triggers a connectivity state update from the
  // deactivated child of the weighted_target policy.
  ShutdownAllBackends();
  // Now restart the backend.
  StartAllBackends();
  // Re-add endpoint.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // RPCs should eventually succeed.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1, [&](const RpcResult& result) {
    if (!result.status.ok()) {
      EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
      EXPECT_THAT(result.status.error_message(),
                  ::testing::MatchesRegex(absl::StrCat(
                      // The error message we see here depends on whether
                      // the client sees the EDS update before or after it
                      // sees the backend come back up.
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "),
                      "|", kErrorMessage)));
    }
  });
}

TEST_P(EdsTest, NoLocalities) {
  CreateAndStartBackends(1);
  // Initial EDS resource has no localities.
  EdsResourceArgs args;
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // RPCs should fail.
  constexpr char kErrorMessage[] =
      "no children in weighted_target policy: EDS resource eds_service_name "
      "contains no localities";
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE, kErrorMessage);
  // Send EDS resource that has an endpoint.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // RPCs should eventually succeed.
  WaitForAllBackends(DEBUG_LOCATION, 0, 1, [&](const RpcResult& result) {
    if (!result.status.ok()) {
      EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
      EXPECT_THAT(result.status.error_message(),
                  ::testing::MatchesRegex(kErrorMessage));
    }
  });
}

// Tests that RPCs will fail with UNAVAILABLE instead of DEADLINE_EXCEEDED if
// all the servers are unreachable.
TEST_P(EdsTest, AllServersUnreachableFailFast) {
  // Set Rpc timeout to 5 seconds to ensure there is enough time
  // for communication with the xDS server to take place upon test start up.
  const uint32_t kRpcTimeoutMs = 5000;
  const size_t kNumUnreachableServers = 5;
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  for (size_t i = 0; i < kNumUnreachableServers; ++i) {
    endpoints.emplace_back(MakeNonExistantEndpoint());
  }
  EdsResourceArgs args({{"locality0", std::move(endpoints)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // The error shouldn't be DEADLINE_EXCEEDED because timeout is set to 5
  // seconds, and we should disocver in that time that the target backend is
  // down.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "),
                      RpcOptions().set_timeout_ms(kRpcTimeoutMs));
}

// Tests that RPCs fail when the backends are down, and will succeed again
// after the backends are restarted.
TEST_P(EdsTest, BackendsRestart) {
  CreateAndStartBackends(3);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION);
  // Stop backends.  RPCs should fail.
  ShutdownAllBackends();
  // Wait for channel to transition out of READY, so that we know it has
  // noticed that all of the subchannels have failed.  Note that it may
  // be reporting either CONNECTING or TRANSIENT_FAILURE at this point.
  EXPECT_TRUE(channel_->WaitForStateChange(
      GRPC_CHANNEL_READY, grpc_timeout_seconds_to_deadline(5)));
  EXPECT_THAT(channel_->GetState(false),
              ::testing::AnyOf(::testing::Eq(GRPC_CHANNEL_TRANSIENT_FAILURE),
                               ::testing::Eq(GRPC_CHANNEL_CONNECTING)));
  // RPCs should fail.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
  // Restart all backends.  RPCs should start succeeding again.
  StartAllBackends();
  CheckRpcSendOk(DEBUG_LOCATION, 1,
                 RpcOptions().set_timeout_ms(2000).set_wait_for_ready(true));
}

TEST_P(EdsTest, IgnoresDuplicateUpdates) {
  CreateAndStartBackends(1);
  const size_t kNumRpcsPerAddress = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for all backends to come online.
  WaitForAllBackends(DEBUG_LOCATION);
  // Send kNumRpcsPerAddress RPCs per server, but send an EDS update in
  // between.  If the update is not ignored, this will cause the
  // round_robin policy to see an update, which will randomly reset its
  // position in the address list.
  for (size_t i = 0; i < kNumRpcsPerAddress; ++i) {
    CheckRpcSendOk(DEBUG_LOCATION, 2);
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
    CheckRpcSendOk(DEBUG_LOCATION, 2);
  }
  // Each backend should have gotten the right number of requests.
  for (size_t i = 1; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress,
              backends_[i]->backend_service()->request_count());
  }
}

// Testing just one example of an invalid resource here.
// Unit tests for XdsEndpointResourceType have exhaustive tests for all
// of the invalid cases.
TEST_P(EdsTest, NacksInvalidResource) {
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  const auto response_state = WaitForEdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_EQ(response_state->error_message,
            "xDS response validation errors: ["
            "resource index 0: eds_service_name: "
            "INVALID_ARGUMENT: errors parsing EDS resource: ["
            "field:endpoints error:priority 0 empty]]");
}

// Tests that if the balancer is down, the RPCs will still be sent to the
// backends according to the last balancer response, until a new balancer is
// reachable.
TEST_P(EdsTest, KeepUsingLastDataIfBalancerGoesDown) {
  CreateAndStartBackends(2);
  // Set up EDS resource pointing to backend 0.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Start the client and make sure it sees the backend.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Stop the balancer, and verify that RPCs continue to flow to backend 0.
  balancer_->Shutdown();
  auto deadline = grpc_timeout_seconds_to_deadline(5);
  do {
    CheckRpcSendOk(DEBUG_LOCATION);
  } while (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0);
  // Check the EDS resource to point to backend 1 and bring the balancer
  // back up.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  balancer_->Start();
  // Wait for client to see backend 1.
  WaitForBackend(DEBUG_LOCATION, 1);
}

// Tests that the localities in a locality map are picked according to their
// weights.
TEST_P(EdsTest, WeightedRoundRobin) {
  CreateAndStartBackends(2);
  const int kLocalityWeight0 = 2;
  const int kLocalityWeight1 = 8;
  const int kTotalLocalityWeight = kLocalityWeight0 + kLocalityWeight1;
  const double kLocalityWeightRate0 =
      static_cast<double>(kLocalityWeight0) / kTotalLocalityWeight;
  const double kLocalityWeightRate1 =
      static_cast<double>(kLocalityWeight1) / kTotalLocalityWeight;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kLocalityWeightRate0, kErrorTolerance);
  // ADS response contains 2 localities, each of which contains 1 backend.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kLocalityWeight0},
      {"locality1", CreateEndpointsForBackends(1, 2), kLocalityWeight1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for both backends to be ready.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  // The locality picking rates should be roughly equal to the expectation.
  const double locality_picked_rate_0 =
      static_cast<double>(backends_[0]->backend_service()->request_count()) /
      kNumRpcs;
  const double locality_picked_rate_1 =
      static_cast<double>(backends_[1]->backend_service()->request_count()) /
      kNumRpcs;
  EXPECT_THAT(locality_picked_rate_0,
              ::testing::DoubleNear(kLocalityWeightRate0, kErrorTolerance));
  EXPECT_THAT(locality_picked_rate_1,
              ::testing::DoubleNear(kLocalityWeightRate1, kErrorTolerance));
}

// Tests that we don't suffer from integer overflow in locality weights.
TEST_P(EdsTest, NoIntegerOverflowInLocalityWeights) {
  CreateAndStartBackends(2);
  const uint32_t kLocalityWeight1 = std::numeric_limits<uint32_t>::max() / 3;
  const uint32_t kLocalityWeight0 =
      std::numeric_limits<uint32_t>::max() - kLocalityWeight1;
  const uint64_t kTotalLocalityWeight =
      static_cast<uint64_t>(kLocalityWeight0) +
      static_cast<uint64_t>(kLocalityWeight1);
  const double kLocalityWeightRate0 =
      static_cast<double>(kLocalityWeight0) / kTotalLocalityWeight;
  const double kLocalityWeightRate1 =
      static_cast<double>(kLocalityWeight1) / kTotalLocalityWeight;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kLocalityWeightRate0, kErrorTolerance);
  // ADS response contains 2 localities, each of which contains 1 backend.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kLocalityWeight0},
      {"locality1", CreateEndpointsForBackends(1, 2), kLocalityWeight1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for both backends to be ready.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  // The locality picking rates should be roughly equal to the expectation.
  const double locality_picked_rate_0 =
      static_cast<double>(backends_[0]->backend_service()->request_count()) /
      kNumRpcs;
  const double locality_picked_rate_1 =
      static_cast<double>(backends_[1]->backend_service()->request_count()) /
      kNumRpcs;
  EXPECT_THAT(locality_picked_rate_0,
              ::testing::DoubleNear(kLocalityWeightRate0, kErrorTolerance));
  EXPECT_THAT(locality_picked_rate_1,
              ::testing::DoubleNear(kLocalityWeightRate1, kErrorTolerance));
}

// Tests that we correctly handle a locality containing no endpoints.
TEST_P(EdsTest, LocalityContainingNoEndpoints) {
  CreateAndStartBackends(2);
  const size_t kNumRpcs = 5000;
  // EDS response contains 2 localities, one with no endpoints.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends()},
      {"locality1", {}},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for both backends to be ready.
  WaitForAllBackends(DEBUG_LOCATION);
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  // All traffic should go to the reachable locality.
  EXPECT_EQ(backends_[0]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
  EXPECT_EQ(backends_[1]->backend_service()->request_count(),
            kNumRpcs / backends_.size());
}

// Tests that the locality map can work properly even when it contains a large
// number of localities.
TEST_P(EdsTest, ManyLocalitiesStressTest) {
  const size_t kNumLocalities = 50;
  CreateAndStartBackends(kNumLocalities + 1);
  const uint32_t kRpcTimeoutMs = 5000;
  // The first ADS response contains kNumLocalities localities, each of which
  // contains its own backend.
  EdsResourceArgs args;
  for (size_t i = 0; i < kNumLocalities; ++i) {
    std::string name = absl::StrCat("locality", i);
    EdsResourceArgs::Locality locality(name,
                                       CreateEndpointsForBackends(i, i + 1));
    args.locality_list.emplace_back(std::move(locality));
  }
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends are ready.
  WaitForAllBackends(DEBUG_LOCATION, 0, kNumLocalities,
                     /*check_status=*/nullptr,
                     WaitForBackendOptions().set_reset_counters(false),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  // The second ADS response contains 1 locality, which contains backend 50.
  args =
      EdsResourceArgs({{"locality0", CreateEndpointsForBackends(
                                         kNumLocalities, kNumLocalities + 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until backend 50 is ready.
  WaitForBackend(DEBUG_LOCATION, kNumLocalities);
}

// Tests that the localities in a locality map are picked correctly after
// update (addition, modification, deletion).
TEST_P(EdsTest, LocalityMapUpdateChurn) {
  CreateAndStartBackends(4);
  const size_t kNumRpcs = 3000;
  // The locality weight for the first 3 localities.
  const std::vector<int> kLocalityWeights0 = {2, 3, 4};
  const double kTotalLocalityWeight0 =
      std::accumulate(kLocalityWeights0.begin(), kLocalityWeights0.end(), 0);
  std::vector<double> locality_weight_rate_0;
  locality_weight_rate_0.reserve(kLocalityWeights0.size());
  for (int weight : kLocalityWeights0) {
    locality_weight_rate_0.push_back(weight / kTotalLocalityWeight0);
  }
  // Delete the first locality, keep the second locality, change the third
  // locality's weight from 4 to 2, and add a new locality with weight 6.
  const std::vector<int> kLocalityWeights1 = {3, 2, 6};
  const double kTotalLocalityWeight1 =
      std::accumulate(kLocalityWeights1.begin(), kLocalityWeights1.end(), 0);
  std::vector<double> locality_weight_rate_1 = {
      0 /* placeholder for locality 0 */};
  for (int weight : kLocalityWeights1) {
    locality_weight_rate_1.push_back(weight / kTotalLocalityWeight1);
  }
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), 2},
      {"locality1", CreateEndpointsForBackends(1, 2), 3},
      {"locality2", CreateEndpointsForBackends(2, 3), 4},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for the first 3 backends to be ready.
  WaitForAllBackends(DEBUG_LOCATION, 0, 3);
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // The picking rates of the first 3 backends should be roughly equal to the
  // expectation.
  std::vector<double> locality_picked_rates;
  for (size_t i = 0; i < 3; ++i) {
    locality_picked_rates.push_back(
        static_cast<double>(backends_[i]->backend_service()->request_count()) /
        kNumRpcs);
  }
  const double kErrorTolerance = 0.2;
  for (size_t i = 0; i < 3; ++i) {
    gpr_log(GPR_INFO, "Locality %" PRIuPTR " rate %f", i,
            locality_picked_rates[i]);
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_0[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_0[i] * (1 + kErrorTolerance))));
  }
  args = EdsResourceArgs({
      {"locality1", CreateEndpointsForBackends(1, 2), 3},
      {"locality2", CreateEndpointsForBackends(2, 3), 2},
      {"locality3", CreateEndpointsForBackends(3, 4), 6},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Backend 3 hasn't received any request.
  EXPECT_EQ(0U, backends_[3]->backend_service()->request_count());
  // Wait until the locality update has been processed, as signaled by backend
  // 3 receiving a request.
  WaitForAllBackends(DEBUG_LOCATION, 3, 4);
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  // Send kNumRpcs RPCs.
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // Backend 0 no longer receives any request.
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  // The picking rates of the last 3 backends should be roughly equal to the
  // expectation.
  locality_picked_rates = {0 /* placeholder for backend 0 */};
  for (size_t i = 1; i < 4; ++i) {
    locality_picked_rates.push_back(
        static_cast<double>(backends_[i]->backend_service()->request_count()) /
        kNumRpcs);
  }
  for (size_t i = 1; i < 4; ++i) {
    gpr_log(GPR_INFO, "Locality %" PRIuPTR " rate %f", i,
            locality_picked_rates[i]);
    EXPECT_THAT(
        locality_picked_rates[i],
        ::testing::AllOf(
            ::testing::Ge(locality_weight_rate_1[i] * (1 - kErrorTolerance)),
            ::testing::Le(locality_weight_rate_1[i] * (1 + kErrorTolerance))));
  }
}

// Tests that we don't fail RPCs when replacing all of the localities in
// a given priority.
TEST_P(EdsTest, ReplaceAllLocalitiesInPriority) {
  CreateAndStartBackends(2);
  // Initial EDS update has backend 0.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for the first backend to be ready.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Send EDS update that replaces the locality and switches to backend 1.
  args = EdsResourceArgs({{"locality1", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // When the client sees the update, RPCs should start going to backend 1.
  // No RPCs should fail during this change.
  WaitForBackend(DEBUG_LOCATION, 1);
}

TEST_P(EdsTest, ConsistentWeightedTargetUpdates) {
  CreateAndStartBackends(4);
  // Initial update has two localities.
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(1, 2)},
      {"locality1", CreateEndpointsForBackends(2, 3)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForAllBackends(DEBUG_LOCATION, 1, 3);
  // Next update removes locality1.
  // Also add backend 0 to locality0, so that we can tell when the
  // update has been seen.
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Next update re-adds locality1.
  // Also add backend 3 to locality1, so that we can tell when the
  // update has been seen.
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 2)},
      {"locality1", CreateEndpointsForBackends(2, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 3);
}

// Tests that RPCs are dropped according to the drop config.
TEST_P(EdsTest, Drops) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double kDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDropRateForLbAndThrottle, kErrorTolerance);
  // The ADS response contains two drop categories.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcs, StatusCode::UNAVAILABLE,
      kStatusMessageDropPrefix);
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
}

// Tests that drop config is converted correctly from per hundred.
TEST_P(EdsTest, DropPerHundred) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerHundredForLb = 10;
  const double kDropRateForLb = kDropPerHundredForLb / 100.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  // The ADS response contains one drop category.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerHundredForLb}};
  args.drop_denominator = FractionalPercent::HUNDRED;
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcs, StatusCode::UNAVAILABLE,
      kStatusMessageDropPrefix);
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
}

// Tests that drop config is converted correctly from per ten thousand.
TEST_P(EdsTest, DropPerTenThousand) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerTenThousandForLb = 1000;
  const double kDropRateForLb = kDropPerTenThousandForLb / 10000.0;
  const double kErrorTolerance = 0.05;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  // The ADS response contains one drop category.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerTenThousandForLb}};
  args.drop_denominator = FractionalPercent::TEN_THOUSAND;
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcs, StatusCode::UNAVAILABLE,
      kStatusMessageDropPrefix);
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
}

// Tests that drop is working correctly after update.
TEST_P(EdsTest, DropConfigUpdate) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kErrorTolerance = 0.05;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double kDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  const size_t kNumRpcsLbOnly =
      ComputeIdealNumRpcs(kDropRateForLb, kErrorTolerance);
  const size_t kNumRpcsBoth =
      ComputeIdealNumRpcs(kDropRateForLbAndThrottle, kErrorTolerance);
  // The first ADS response contains one drop category.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcsLbOnly RPCs and count the drops.
  gpr_log(GPR_INFO, "========= BEFORE FIRST BATCH ==========");
  size_t num_drops = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcsLbOnly, StatusCode::UNAVAILABLE,
      kStatusMessageDropPrefix);
  gpr_log(GPR_INFO, "========= DONE WITH FIRST BATCH ==========");
  // The drop rate should be roughly equal to the expectation.
  double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcsLbOnly;
  gpr_log(GPR_INFO, "First batch drop rate %f", seen_drop_rate);
  EXPECT_THAT(seen_drop_rate,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
  // The second ADS response contains two drop categories, send an update EDS
  // response.
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until the drop rate increases to the middle of the two configs,
  // which implies that the update has been in effect.
  const double kDropRateThreshold =
      (kDropRateForLb + kDropRateForLbAndThrottle) / 2;
  size_t num_rpcs = kNumRpcsBoth;
  SendRpcsUntil(
      DEBUG_LOCATION,
      [&](const RpcResult& result) {
        ++num_rpcs;
        if (result.status.ok()) {
          EXPECT_EQ(result.response.message(), kRequestMessage);
        } else {
          EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
          EXPECT_THAT(result.status.error_message(),
                      ::testing::StartsWith(kStatusMessageDropPrefix));
          ++num_drops;
        }
        seen_drop_rate = static_cast<double>(num_drops) / num_rpcs;
        return seen_drop_rate < kDropRateThreshold;
      },
      /*timeout_ms=*/40000);
  // Send kNumRpcsBoth RPCs and count the drops.
  gpr_log(GPR_INFO, "========= BEFORE SECOND BATCH ==========");
  num_drops = SendRpcsAndCountFailuresWithMessage(DEBUG_LOCATION, kNumRpcsBoth,
                                                  StatusCode::UNAVAILABLE,
                                                  kStatusMessageDropPrefix);
  gpr_log(GPR_INFO, "========= DONE WITH SECOND BATCH ==========");
  // The new drop rate should be roughly equal to the expectation.
  seen_drop_rate = static_cast<double>(num_drops) / kNumRpcsBoth;
  gpr_log(GPR_INFO, "Second batch drop rate %f", seen_drop_rate);
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
}

// Tests that all the RPCs are dropped if any drop category drops 100%.
TEST_P(EdsTest, DropAll) {
  const size_t kNumRpcs = 1000;
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 1000000;
  // The ADS response contains two drop categories.
  EdsResourceArgs args;
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and all of them are dropped.
  size_t num_drops = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcs, StatusCode::UNAVAILABLE,
      kStatusMessageDropPrefix);
  EXPECT_EQ(num_drops, kNumRpcs);
}

//
// EDS failover tests
//

class FailoverTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    XdsEnd2endTest::SetUp();
    ResetStub(/*failover_timeout_ms=*/500);
  }
};

INSTANTIATE_TEST_SUITE_P(
    XdsTest, FailoverTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

// Localities with the highest priority are used when multiple priority exist.
TEST_P(FailoverTest, ChooseHighestPriority) {
  CreateAndStartBackends(4);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose priority with no endpoints.
TEST_P(FailoverTest, DoesNotUsePriorityWithNoEndpoints) {
  CreateAndStartBackends(3);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", {}, kDefaultLocalityWeight, 0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false));
  for (size_t i = 1; i < 3; ++i) {
    EXPECT_EQ(0U, backends_[i]->backend_service()->request_count());
  }
}

// Does not choose locality with no endpoints.
TEST_P(FailoverTest, DoesNotUseLocalityWithNoEndpoints) {
  CreateAndStartBackends(1);
  EdsResourceArgs args({
      {"locality0", {}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(), kDefaultLocalityWeight, 0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for all backends to be used.
  WaitForAllBackends(DEBUG_LOCATION);
}

// If the higher priority localities are not reachable, failover to the
// highest priority among the rest.
TEST_P(FailoverTest, Failover) {
  CreateAndStartBackends(2);
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
      {"locality1", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       3},
      {"locality3", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false));
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
}

// Reports CONNECTING when failing over to a lower priority.
TEST_P(FailoverTest, ReportsConnectingDuringFailover) {
  CreateAndStartBackends(1);
  // Priority 0 will be unreachable, so we'll use priority 1.
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(), kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  ConnectionAttemptInjector injector;
  auto hold = injector.AddHold(backends_[0]->port());
  // Start an RPC in the background, which should cause the channel to
  // try to connect.
  LongRunningRpc rpc;
  rpc.StartRpc(stub_.get(), RpcOptions());
  // Wait for connection attempt to start to the backend.
  hold->Wait();
  // Channel state should be CONNECTING here, and any RPC should be
  // queued.
  EXPECT_EQ(channel_->GetState(false), GRPC_CHANNEL_CONNECTING);
  // Allow the connection attempt to complete.
  hold->Resume();
  // Now the RPC should complete successfully.
  gpr_log(GPR_INFO, "=== WAITING FOR RPC TO FINISH ===");
  Status status = rpc.GetStatus();
  gpr_log(GPR_INFO, "=== RPC FINISHED ===");
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
}

// If a locality with higher priority than the current one becomes ready,
// switch to it.
TEST_P(FailoverTest, SwitchBackToHigherPriority) {
  CreateAndStartBackends(4);
  const size_t kNumRpcs = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 3);
  backends_[3]->StopListeningAndSendGoaways();
  backends_[0]->StopListeningAndSendGoaways();
  WaitForBackend(DEBUG_LOCATION, 1);
  ShutdownBackend(0);
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0);
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[0]->backend_service()->request_count());
}

// The first update only contains unavailable priorities. The second update
// contains available priorities.
TEST_P(FailoverTest, UpdateInitialUnavailable) {
  CreateAndStartBackends(2);
  EdsResourceArgs args({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
      {"locality1", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0, [&](const RpcResult& result) {
    if (!result.status.ok()) {
      EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
      EXPECT_THAT(result.status.error_message(),
                  ::testing::MatchesRegex(MakeConnectionFailureRegex(
                      "connections to all backends failing; last error: ")));
    }
  });
}

// Tests that after the localities' priorities are updated, we still choose
// the highest READY priority with the updated localities.
TEST_P(FailoverTest, UpdatePriority) {
  CreateAndStartBackends(4);
  const size_t kNumRpcs = 100;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       3},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false));
  EXPECT_EQ(0U, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0U, backends_[1]->backend_service()->request_count());
  EXPECT_EQ(0U, backends_[2]->backend_service()->request_count());
  args = EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       2},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       0},
      {"locality2", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       1},
      {"locality3", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       3},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 1);
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  EXPECT_EQ(kNumRpcs, backends_[1]->backend_service()->request_count());
}

// Moves all localities in the current priority to a higher priority.
TEST_P(FailoverTest, MoveAllLocalitiesInCurrentPriorityToHigherPriority) {
  CreateAndStartBackends(3);
  auto non_existant_endpoint = MakeNonExistantEndpoint();
  // First update:
  // - Priority 0 is locality 0, containing an unreachable backend.
  // - Priority 1 is locality 1, containing backends 0 and 1.
  EdsResourceArgs args({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(0, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // When we get the first update, all backends in priority 0 are down,
  // so we will create priority 1.  Backends 0 and 1 should have traffic,
  // but backend 2 should not.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions().set_reset_counters(false));
  EXPECT_EQ(0UL, backends_[2]->backend_service()->request_count());
  // Second update:
  // - Priority 0 contains both localities 0 and 1.
  // - Priority 1 is not present.
  // - We add backend 2 to locality 1, just so we have a way to know
  //   when the update has been seen by the client.
  args = EdsResourceArgs({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(0, 3), kDefaultLocalityWeight,
       0},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // When backend 2 gets traffic, we know the second update has been seen.
  WaitForBackend(DEBUG_LOCATION, 2);
  // The xDS server got at least 1 response.
  EXPECT_TRUE(balancer_->ads_service()->eds_response_state().has_value());
}

// This tests a bug triggered by the xds_cluster_resolver policy reusing
// a child name for the priority policy when that child name was still
// present but deactivated.
TEST_P(FailoverTest, PriorityChildNameChurn) {
  CreateAndStartBackends(4);
  auto non_existant_endpoint = MakeNonExistantEndpoint();
  // Initial update:
  // - P0:locality0, child number 0 (unreachable)
  // - P1:locality1, child number 1
  // - P2:locality2, child number 2
  EdsResourceArgs args({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality1", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       1},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       2},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Next update:
  // - P0:locality0, child number 0 (still unreachable)
  // - P1:locality2, child number 2 (moved from P2 to P1)
  // - P2:locality3, child number 3 (new child)
  // Child number 1 will be deactivated.
  args = EdsResourceArgs({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
      {"locality3", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       2},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 1);
  // Next update:
  // - P0:locality0, child number 0 (still unreachable)
  // - P1:locality4, child number 4 (new child number -- should not reuse #1)
  // - P2:locality3, child number 3
  // Child number 1 will be deactivated.
  args = EdsResourceArgs({
      {"locality0", {non_existant_endpoint}, kDefaultLocalityWeight, 0},
      {"locality4", CreateEndpointsForBackends(3, 4), kDefaultLocalityWeight,
       1},
      {"locality3", CreateEndpointsForBackends(2, 3), kDefaultLocalityWeight,
       2},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions().set_reset_counters(false));
  // P2 should not have gotten any traffic in this change.
  EXPECT_EQ(0UL, backends_[2]->backend_service()->request_count());
}

//
// EDS client load reporting tests
//

using ClientLoadReportingTest = XdsEnd2endTest;

INSTANTIATE_TEST_SUITE_P(
    XdsTest, ClientLoadReportingTest,
    ::testing::Values(XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

MATCHER_P2(LoadMetricEq, num_requests_finished_with_metric, total_metric_value,
           "equals LoadMetric") {
  bool match = true;
  match &= ::testing::ExplainMatchResult(num_requests_finished_with_metric,
                                         arg.num_requests_finished_with_metric,
                                         result_listener);
  match &=
      ::testing::ExplainMatchResult(::testing::DoubleEq(total_metric_value),
                                    arg.total_metric_value, result_listener);
  return match;
}

// Tests that the load report received at the balancer is correct.
TEST_P(ClientLoadReportingTest, Vanilla) {
  CreateAndStartBackends(4);
  const size_t kNumRpcsPerAddress = 10;
  const size_t kNumFailuresPerAddress = 3;
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
      {"locality1", CreateEndpointsForBackends(2, 4)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends are ready.
  size_t num_warmup_rpcs =
      WaitForAllBackends(DEBUG_LOCATION, 0, 4, /*check_status=*/nullptr,
                         WaitForBackendOptions().set_reset_counters(false));
  // Test with multiple reports to ensure clients reset metrics after reporting.
  // report.
  for (int report = 0; report < 3; ++report) {
    // Send kNumRpcsPerAddress RPCs per server with named metrics.
    xds::data::orca::v3::OrcaLoadReport backend_metrics;
    auto& named_metrics = (*backend_metrics.mutable_named_metrics());
    named_metrics["foo"] = 1.0;
    named_metrics["bar"] = 2.0;
    CheckRpcSendOk(DEBUG_LOCATION, kNumRpcsPerAddress * backends_.size(),
                   RpcOptions().set_backend_metrics(backend_metrics));
    named_metrics["foo"] = 0.3;
    named_metrics["bar"] = 0.4;
    for (size_t i = 0; i < kNumFailuresPerAddress * backends_.size(); ++i) {
      CheckRpcSendFailure(
          DEBUG_LOCATION, StatusCode::FAILED_PRECONDITION, "",
          RpcOptions().set_server_fail(true).set_backend_metrics(
              backend_metrics));
    }
    const size_t total_successful_rpcs_sent =
        (kNumRpcsPerAddress * backends_.size()) + num_warmup_rpcs;
    const size_t total_failed_rpcs_sent =
        kNumFailuresPerAddress * backends_.size();
    // Check that the backends got the right number of requests.
    size_t total_rpcs_sent = 0;
    for (const auto& backend : backends_) {
      total_rpcs_sent += backend->backend_service()->request_count();
      backend->backend_service()->ResetCounters();
    }
    EXPECT_EQ(total_rpcs_sent,
              total_successful_rpcs_sent + total_failed_rpcs_sent);
    // The load report received at the balancer should be correct.
    std::vector<ClientStats> load_report =
        balancer_->lrs_service()->WaitForLoadReport();
    ASSERT_EQ(load_report.size(), 1UL);
    ClientStats& client_stats = load_report.front();
    EXPECT_EQ(client_stats.cluster_name(), kDefaultClusterName);
    EXPECT_EQ(client_stats.eds_service_name(), kDefaultEdsServiceName);
    EXPECT_EQ(total_successful_rpcs_sent,
              client_stats.total_successful_requests());
    EXPECT_EQ(0U, client_stats.total_requests_in_progress());
    EXPECT_EQ(total_rpcs_sent, client_stats.total_issued_requests());
    EXPECT_EQ(total_failed_rpcs_sent, client_stats.total_error_requests());
    EXPECT_EQ(0U, client_stats.total_dropped_requests());
    ASSERT_THAT(
        client_stats.locality_stats(),
        ::testing::ElementsAre(::testing::Pair("locality0", ::testing::_),
                               ::testing::Pair("locality1", ::testing::_)));
    size_t num_successful_rpcs = 0;
    size_t num_failed_rpcs = 0;
    std::map<std::string, ClientStats::LocalityStats::LoadMetric>
        named_metrics_total;
    for (const auto& p : client_stats.locality_stats()) {
      EXPECT_EQ(p.second.total_requests_in_progress, 0U);
      EXPECT_EQ(
          p.second.total_issued_requests,
          p.second.total_successful_requests + p.second.total_error_requests);
      num_successful_rpcs += p.second.total_successful_requests;
      num_failed_rpcs += p.second.total_error_requests;
      for (const auto& s : p.second.load_metrics) {
        named_metrics_total[s.first] += s.second;
      }
    }
    EXPECT_EQ(num_successful_rpcs, total_successful_rpcs_sent);
    EXPECT_EQ(num_failed_rpcs, total_failed_rpcs_sent);
    EXPECT_EQ(num_successful_rpcs + num_failed_rpcs, total_rpcs_sent);
    EXPECT_THAT(
        named_metrics_total,
        ::testing::UnorderedElementsAre(
            ::testing::Pair(
                "foo",
                LoadMetricEq(
                    (kNumRpcsPerAddress + kNumFailuresPerAddress) *
                        backends_.size(),
                    (kNumRpcsPerAddress * backends_.size()) * 1.0 +
                        (kNumFailuresPerAddress * backends_.size()) * 0.3)),
            ::testing::Pair(
                "bar",
                LoadMetricEq(
                    (kNumRpcsPerAddress + kNumFailuresPerAddress) *
                        backends_.size(),
                    (kNumRpcsPerAddress * backends_.size()) * 2.0 +
                        (kNumFailuresPerAddress * backends_.size()) * 0.4))));
    // The LRS service got a single request, and sent a single response.
    EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
    EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
    // Warmup RPCs only count in the first report.
    num_warmup_rpcs = 0;
  }
}

// Tests send_all_clusters.
TEST_P(ClientLoadReportingTest, SendAllClusters) {
  CreateAndStartBackends(2);
  balancer_->lrs_service()->set_send_all_clusters(true);
  const size_t kNumRpcsPerAddress = 10;
  const size_t kNumFailuresPerAddress = 3;
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends are ready.
  size_t num_warmup_rpcs = WaitForAllBackends(DEBUG_LOCATION);
  // Send kNumRpcsPerAddress RPCs per server.
  xds::data::orca::v3::OrcaLoadReport backend_metrics;
  auto& named_metrics = (*backend_metrics.mutable_named_metrics());
  named_metrics["foo"] = 1.0;
  named_metrics["bar"] = 2.0;
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcsPerAddress * backends_.size(),
                 RpcOptions().set_backend_metrics(backend_metrics));
  named_metrics["foo"] = 0.3;
  named_metrics["bar"] = 0.4;
  for (size_t i = 0; i < kNumFailuresPerAddress * backends_.size(); ++i) {
    CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::FAILED_PRECONDITION, "",
                        RpcOptions().set_server_fail(true).set_backend_metrics(
                            backend_metrics));
  }
  // Check that each backend got the right number of requests.
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(kNumRpcsPerAddress + kNumFailuresPerAddress,
              backends_[i]->backend_service()->request_count());
  }
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats& client_stats = load_report.front();
  EXPECT_EQ(kNumRpcsPerAddress * backends_.size() + num_warmup_rpcs,
            client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ((kNumRpcsPerAddress + kNumFailuresPerAddress) * backends_.size() +
                num_warmup_rpcs,
            client_stats.total_issued_requests());
  EXPECT_EQ(kNumFailuresPerAddress * backends_.size(),
            client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  EXPECT_THAT(
      client_stats.locality_stats(),
      ::testing::ElementsAre(::testing::Pair(
          "locality0",
          ::testing::Field(
              &ClientStats::LocalityStats::load_metrics,
              ::testing::UnorderedElementsAre(
                  ::testing::Pair(
                      "foo",
                      LoadMetricEq(
                          (kNumRpcsPerAddress + kNumFailuresPerAddress) *
                              backends_.size(),
                          (kNumRpcsPerAddress * backends_.size()) * 1.0 +
                              (kNumFailuresPerAddress * backends_.size()) *
                                  0.3)),
                  ::testing::Pair(
                      "bar",
                      LoadMetricEq(
                          (kNumRpcsPerAddress + kNumFailuresPerAddress) *
                              backends_.size(),
                          (kNumRpcsPerAddress * backends_.size()) * 2.0 +
                              (kNumFailuresPerAddress * backends_.size()) *
                                  0.4)))))));
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// Tests that we don't include stats for clusters that are not requested
// by the LRS server.
TEST_P(ClientLoadReportingTest, HonorsClustersRequestedByLrsServer) {
  CreateAndStartBackends(1);
  balancer_->lrs_service()->set_cluster_names({"bogus"});
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends are ready.
  WaitForAllBackends(DEBUG_LOCATION);
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 0UL);
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// Tests that if the balancer restarts, the client load report contains the
// stats before and after the restart correctly.
TEST_P(ClientLoadReportingTest, BalancerRestart) {
  CreateAndStartBackends(4);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 2)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait until all backends returned by the balancer are ready.
  size_t num_rpcs = WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  ClientStats client_stats = std::move(load_report.front());
  EXPECT_EQ(num_rpcs, client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  EXPECT_THAT(client_stats.locality_stats(),
              ::testing::ElementsAre(::testing::Pair(
                  "locality0",
                  ::testing::Field(&ClientStats::LocalityStats::load_metrics,
                                   ::testing::IsEmpty()))));
  // Shut down the balancer.
  balancer_->Shutdown();
  // We should continue using the last EDS response we received from the
  // balancer before it was shut down.
  // Note: We need to use WaitForAllBackends() here instead of just
  // CheckRpcSendOk(kNumBackendsFirstPass), because when the balancer
  // shuts down, the XdsClient will generate an error to the
  // ListenerWatcher, which will cause the xds resolver to send a
  // no-op update to the LB policy.  When this update gets down to the
  // round_robin child policy for the locality, it will generate a new
  // subchannel list, which resets the start index randomly.  So we need
  // to be a little more permissive here to avoid spurious failures.
  ResetBackendCounters();
  num_rpcs = WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  // Now restart the balancer, this time pointing to the new backends.
  balancer_->Start();
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends(2, 4)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Wait for queries to start going to one of the new backends.
  // This tells us that we're now using the new serverlist.
  num_rpcs += WaitForAllBackends(DEBUG_LOCATION, 2, 4);
  // Send one RPC per backend.
  xds::data::orca::v3::OrcaLoadReport backend_metrics;
  auto& named_metrics = (*backend_metrics.mutable_named_metrics());
  named_metrics["foo"] = 1.0;
  named_metrics["bar"] = 2.0;
  CheckRpcSendOk(DEBUG_LOCATION, 2,
                 RpcOptions().set_backend_metrics(backend_metrics));
  num_rpcs += 2;
  // Check client stats.
  load_report = balancer_->lrs_service()->WaitForLoadReport();
  ASSERT_EQ(load_report.size(), 1UL);
  client_stats = std::move(load_report.front());
  EXPECT_EQ(num_rpcs, client_stats.total_successful_requests());
  EXPECT_EQ(0U, client_stats.total_requests_in_progress());
  EXPECT_EQ(0U, client_stats.total_error_requests());
  EXPECT_EQ(0U, client_stats.total_dropped_requests());
  EXPECT_THAT(client_stats.locality_stats(),
              ::testing::ElementsAre(::testing::Pair(
                  "locality0",
                  ::testing::Field(
                      &ClientStats::LocalityStats::load_metrics,
                      ::testing::UnorderedElementsAre(
                          ::testing::Pair("foo", LoadMetricEq(2, 2.0)),
                          ::testing::Pair("bar", LoadMetricEq(2, 4.0)))))));
}

// Tests load reporting when switching over from one cluster to another.
TEST_P(ClientLoadReportingTest, ChangeClusters) {
  CreateAndStartBackends(4);
  const char* kNewClusterName = "new_cluster_name";
  const char* kNewEdsServiceName = "new_eds_service_name";
  balancer_->lrs_service()->set_cluster_names(
      {kDefaultClusterName, kNewClusterName});
  // cluster kDefaultClusterName -> locality0 -> backends 0 and 1
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 2)},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // cluster kNewClusterName -> locality1 -> backends 2 and 3
  EdsResourceArgs args2({
      {"locality1", CreateEndpointsForBackends(2, 4)},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsServiceName));
  // CDS resource for kNewClusterName.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(kNewClusterName);
  new_cluster.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName);
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Wait for all backends to come online.
  size_t num_rpcs = WaitForAllBackends(DEBUG_LOCATION, 0, 2);
  // The load report received at the balancer should be correct.
  std::vector<ClientStats> load_report =
      balancer_->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&ClientStats::cluster_name, kDefaultClusterName),
          ::testing::Property(&ClientStats::eds_service_name,
                              kDefaultEdsServiceName),
          ::testing::Property(
              &ClientStats::locality_stats,
              ::testing::ElementsAre(::testing::Pair(
                  "locality0",
                  ::testing::AllOf(
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_successful_requests,
                                       num_rpcs),
                      ::testing::Field(&ClientStats::LocalityStats::
                                           total_requests_in_progress,
                                       0UL),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_error_requests,
                          0UL),
                      ::testing::Field(
                          &ClientStats::LocalityStats::total_issued_requests,
                          num_rpcs),
                      ::testing::Field(
                          &ClientStats::LocalityStats::load_metrics,
                          ::testing::IsEmpty()))))),
          ::testing::Property(&ClientStats::total_dropped_requests, 0UL))));
  // Change RDS resource to point to new cluster.
  RouteConfiguration new_route_config = default_route_config_;
  new_route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->set_cluster(kNewClusterName);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Wait for all new backends to be used.
  num_rpcs = WaitForAllBackends(DEBUG_LOCATION, 2, 4);
  // The load report received at the balancer should be correct.
  load_report = balancer_->lrs_service()->WaitForLoadReport();
  EXPECT_THAT(
      load_report,
      ::testing::ElementsAre(
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name,
                                  kDefaultClusterName),
              ::testing::Property(&ClientStats::eds_service_name,
                                  kDefaultEdsServiceName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality0",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Lt(num_rpcs)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              0UL),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_issued_requests,
                                           ::testing::Le(num_rpcs)),
                          ::testing::Field(
                              &ClientStats::LocalityStats::load_metrics,
                              ::testing::IsEmpty()))))),
              ::testing::Property(&ClientStats::total_dropped_requests, 0UL)),
          ::testing::AllOf(
              ::testing::Property(&ClientStats::cluster_name, kNewClusterName),
              ::testing::Property(&ClientStats::eds_service_name,
                                  kNewEdsServiceName),
              ::testing::Property(
                  &ClientStats::locality_stats,
                  ::testing::ElementsAre(::testing::Pair(
                      "locality1",
                      ::testing::AllOf(
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_successful_requests,
                                           ::testing::Le(num_rpcs)),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_requests_in_progress,
                                           0UL),
                          ::testing::Field(
                              &ClientStats::LocalityStats::total_error_requests,
                              0UL),
                          ::testing::Field(&ClientStats::LocalityStats::
                                               total_issued_requests,
                                           ::testing::Le(num_rpcs)),
                          ::testing::Field(
                              &ClientStats::LocalityStats::load_metrics,
                              ::testing::IsEmpty()))))),
              ::testing::Property(&ClientStats::total_dropped_requests, 0UL))));
  size_t total_ok = 0;
  for (const ClientStats& client_stats : load_report) {
    total_ok += client_stats.total_successful_requests();
  }
  EXPECT_EQ(total_ok, num_rpcs);
  // The LRS service got a single request, and sent a single response.
  EXPECT_EQ(1U, balancer_->lrs_service()->request_count());
  EXPECT_EQ(1U, balancer_->lrs_service()->response_count());
}

// Tests that the drop stats are correctly reported by client load reporting.
TEST_P(ClientLoadReportingTest, DropStats) {
  CreateAndStartBackends(1);
  const uint32_t kDropPerMillionForLb = 100000;
  const uint32_t kDropPerMillionForThrottle = 200000;
  const double kErrorTolerance = 0.05;
  const double kDropRateForLb = kDropPerMillionForLb / 1000000.0;
  const double kDropRateForThrottle = kDropPerMillionForThrottle / 1000000.0;
  const double kDropRateForLbAndThrottle =
      kDropRateForLb + (1 - kDropRateForLb) * kDropRateForThrottle;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDropRateForLbAndThrottle, kErrorTolerance);
  // The ADS response contains two drop categories.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  args.drop_categories = {{kLbDropType, kDropPerMillionForLb},
                          {kThrottleDropType, kDropPerMillionForThrottle}};
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send kNumRpcs RPCs and count the drops.
  size_t num_drops = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcs, StatusCode::UNAVAILABLE,
      kStatusMessageDropPrefix);
  // The drop rate should be roughly equal to the expectation.
  const double seen_drop_rate = static_cast<double>(num_drops) / kNumRpcs;
  EXPECT_THAT(seen_drop_rate, ::testing::DoubleNear(kDropRateForLbAndThrottle,
                                                    kErrorTolerance));
  // Check client stats.
  ClientStats client_stats;
  do {
    std::vector<ClientStats> load_reports =
        balancer_->lrs_service()->WaitForLoadReport();
    for (const auto& load_report : load_reports) {
      client_stats += load_report;
    }
  } while (client_stats.total_issued_requests() +
               client_stats.total_dropped_requests() <
           kNumRpcs);
  EXPECT_EQ(num_drops, client_stats.total_dropped_requests());
  EXPECT_THAT(static_cast<double>(client_stats.dropped_requests(kLbDropType)) /
                  kNumRpcs,
              ::testing::DoubleNear(kDropRateForLb, kErrorTolerance));
  EXPECT_THAT(
      static_cast<double>(client_stats.dropped_requests(kThrottleDropType)) /
          (kNumRpcs * (1 - kDropRateForLb)),
      ::testing::DoubleNear(kDropRateForThrottle, kErrorTolerance));
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
  grpc::testing::ConnectionAttemptInjector::Init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
