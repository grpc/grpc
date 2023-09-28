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

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include <grpc/event_engine/endpoint_config.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "test/cpp/end2end/connection_attempt_injector.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::cluster::v3::CustomClusterType;
using ::envoy::extensions::clusters::aggregate::v3::ClusterConfig;

class ClusterTypeTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    logical_dns_cluster_resolver_response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    InitClient();
    ChannelArguments args;
    args.SetPointerWithVtable(
        GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR,
        logical_dns_cluster_resolver_response_generator_.get(),
        &grpc_core::FakeResolverResponseGenerator::kChannelArgPointerVtable);
    ResetStub(/*failover_timeout_ms=*/0, &args);
  }

  grpc_core::ServerAddressList CreateAddressListFromPortList(
      const std::vector<int>& ports) {
    grpc_core::ServerAddressList addresses;
    for (int port : ports) {
      absl::StatusOr<grpc_core::URI> lb_uri = grpc_core::URI::Parse(
          absl::StrCat(ipv6_only_ ? "ipv6:[::1]:" : "ipv4:127.0.0.1:", port));
      GPR_ASSERT(lb_uri.ok());
      grpc_resolved_address address;
      GPR_ASSERT(grpc_parse_uri(*lb_uri, &address));
      addresses.emplace_back(address, grpc_core::ChannelArgs());
    }
    return addresses;
  }

  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      logical_dns_cluster_resolver_response_generator_;
};

//
// LOGICAL_DNS cluster tests
//

using LogicalDNSClusterTest = ClusterTypeTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, LogicalDNSClusterTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(LogicalDNSClusterTest, Basic) {
  CreateAndStartBackends(1);
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* address = cluster.mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address()
                      ->mutable_socket_address();
  address->set_address(kServerName);
  address->set_port_value(443);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts());
    logical_dns_cluster_resolver_response_generator_->SetResponseSynchronously(
        std::move(result));
  }
  // RPCs should succeed.
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// aggregate cluster tests
//

// TODO(roth): Add tests showing that load reporting is enabled on a
// per-underlying-cluster basis.

using AggregateClusterTest = ClusterTypeTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, AggregateClusterTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(AggregateClusterTest, Basic) {
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  // Populate new EDS resources.
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
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
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Wait for traffic to go to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  backends_[0]->StopListeningAndSendGoaways();
  WaitForBackend(DEBUG_LOCATION, 1);
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic goes back to it.
  ShutdownBackend(0);
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0);
}

// This test covers a bug found in the following scenario:
// 1. P0 reports TRANSIENT_FAILURE, so we start connecting to P1.
// 2. While P1 is still in CONNECTING, P0 goes back to READY, so we
//    switch back to P0, deactivating P1.
// 3. P0 then goes back to TRANSIENT_FAILURE, and we reactivate P1.
// The bug caused us to fail to choose P1 even though it is in state
// CONNECTING (because the failover timer was not running), so we
// incorrectly failed the RPCs.
TEST_P(AggregateClusterTest, FallBackWithConnectivityChurn) {
  CreateAndStartBackends(2);
  const char* kClusterName1 = "cluster1";
  const char* kClusterName2 = "cluster2";
  const char* kEdsServiceName2 = "eds_service_name2";
  // Populate EDS resources.
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  args = EdsResourceArgs({{"locality1", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args, kEdsServiceName2));
  // Populate new CDS resources.
  Cluster cluster1 = default_cluster_;
  cluster1.set_name(kClusterName1);
  balancer_->ads_service()->SetCdsResource(cluster1);
  Cluster cluster2 = default_cluster_;
  cluster2.set_name(kClusterName2);
  cluster2.mutable_eds_cluster_config()->set_service_name(kEdsServiceName2);
  balancer_->ads_service()->SetCdsResource(cluster2);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kClusterName1);
  cluster_config.add_clusters(kClusterName2);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Start connection injector.
  ConnectionAttemptInjector injector;
  auto hold0 = injector.AddHold(backends_[0]->port());
  auto hold1 = injector.AddHold(backends_[1]->port());
  // Start long-running RPC in the background.
  // This will trigger the channel to start connecting.
  // Increase timeout to account for subchannel connection delays.
  LongRunningRpc rpc;
  rpc.StartRpc(stub_.get(), RpcOptions().set_timeout_ms(2000));
  // Tell channel to start connecting.
  channel_->GetState(/*try_to_connect=*/true);
  // Wait for backend 0 connection attempt to start, then fail it.
  hold0->Wait();
  hold0->Fail(GRPC_ERROR_CREATE("injected connection failure"));
  // The channel should trigger a connection attempt for backend 1 now,
  // but we've added a hold for that, so it will not complete yet.
  // Meanwhile, the channel will also start a second attempt for backend
  // 0, which we have NOT held, so it will complete normally, and the
  // RPC will finish on backend 0.
  gpr_log(GPR_INFO, "=== WAITING FOR RPC TO FINISH === ");
  Status status = rpc.GetStatus();
  gpr_log(GPR_INFO, "=== RPC FINISHED === ");
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  EXPECT_EQ(1UL, backends_[0]->backend_service()->request_count());
  // Wait for backend 1 connection attempt to start.
  hold1->Wait();
  // Send GOAWAY from the P0 backend.
  // We don't actually shut it down here to avoid flakiness caused by
  // failing an RPC after the client has already sent it but before the
  // server finished processing it.
  backends_[0]->StopListeningAndSendGoaways();
  // Allow the connection attempt to the P1 backend to resume.
  hold1->Resume();
  // Wait for P1 backend to start getting traffic.
  WaitForBackend(DEBUG_LOCATION, 1);
}

TEST_P(AggregateClusterTest, EdsToLogicalDns) {
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate new EDS resources.
  EdsResourceArgs args1({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  // Create Logical DNS Cluster
  auto logical_dns_cluster = default_cluster_;
  logical_dns_cluster.set_name(kLogicalDNSClusterName);
  logical_dns_cluster.set_type(Cluster::LOGICAL_DNS);
  auto* address = logical_dns_cluster.mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address()
                      ->mutable_socket_address();
  address->set_address(kServerName);
  address->set_port_value(443);
  balancer_->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kLogicalDNSClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts(1, 2));
    logical_dns_cluster_resolver_response_generator_->SetResponseSynchronously(
        std::move(result));
  }
  // Wait for traffic to go to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  backends_[0]->StopListeningAndSendGoaways();
  WaitForBackend(DEBUG_LOCATION, 1);
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  ShutdownBackend(0);
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0);
}

TEST_P(AggregateClusterTest, LogicalDnsToEds) {
  CreateAndStartBackends(2);
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate new EDS resources.
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsService2Name));
  // Populate new CDS resources.
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewCluster2Name);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Create Logical DNS Cluster
  auto logical_dns_cluster = default_cluster_;
  logical_dns_cluster.set_name(kLogicalDNSClusterName);
  logical_dns_cluster.set_type(Cluster::LOGICAL_DNS);
  auto* address = logical_dns_cluster.mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address()
                      ->mutable_socket_address();
  address->set_address(kServerName);
  address->set_port_value(443);
  balancer_->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kLogicalDNSClusterName);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts(0, 1));
    logical_dns_cluster_resolver_response_generator_->SetResponseSynchronously(
        std::move(result));
  }
  // Wait for traffic to go to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  backends_[0]->StopListeningAndSendGoaways();
  WaitForBackend(DEBUG_LOCATION, 1);
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  ShutdownBackend(0);
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0);
}

// This test covers a bug seen in the wild where the
// xds_cluster_resolver policy's code to reuse child policy names did
// not correctly handle the case where the LOGICAL_DNS priority failed,
// thus returning a priority with no localities.  This caused the child
// name to be reused incorrectly, which triggered an assertion failure
// in the xds_cluster_impl policy caused by changing its cluster name.
TEST_P(AggregateClusterTest, ReconfigEdsWhileLogicalDnsChildFails) {
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate EDS resource with all unreachable endpoints.
  // - Priority 0: locality0
  // - Priority 1: locality1, locality2
  EdsResourceArgs args1({
      {"locality0", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 0},
      {"locality1", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
      {"locality2", {MakeNonExistantEndpoint()}, kDefaultLocalityWeight, 1},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewCluster1Name);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService1Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  // Create Logical DNS Cluster
  auto logical_dns_cluster = default_cluster_;
  logical_dns_cluster.set_name(kLogicalDNSClusterName);
  logical_dns_cluster.set_type(Cluster::LOGICAL_DNS);
  auto* address = logical_dns_cluster.mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address()
                      ->mutable_socket_address();
  address->set_address(kServerName);
  address->set_port_value(443);
  balancer_->ads_service()->SetCdsResource(logical_dns_cluster);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kLogicalDNSClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = absl::UnavailableError("injected error");
    logical_dns_cluster_resolver_response_generator_->SetResponseSynchronously(
        std::move(result));
  }
  // When an RPC fails, we know the channel has seen the update.
  constexpr char kErrorMessage[] =
      "empty address list: DNS resolution failed for server.example.com:443 "
      "\\(UNAVAILABLE: injected error\\)";
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE, kErrorMessage);
  // Send an EDS update that moves locality1 to priority 0.
  args1 = EdsResourceArgs({
      {"locality1", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  WaitForBackend(DEBUG_LOCATION, 0, [&](const RpcResult& result) {
    if (!result.status.ok()) {
      EXPECT_EQ(result.status.error_code(), StatusCode::UNAVAILABLE);
      EXPECT_THAT(result.status.error_message(),
                  ::testing::MatchesRegex(kErrorMessage));
    }
  });
}

TEST_P(AggregateClusterTest, MultipleClustersWithSameLocalities) {
  CreateAndStartBackends(2);
  const char* kNewClusterName1 = "new_cluster_1";
  const char* kNewEdsServiceName1 = "new_eds_service_name_1";
  const char* kNewClusterName2 = "new_cluster_2";
  const char* kNewEdsServiceName2 = "new_eds_service_name_2";
  // Populate EDS resource for cluster 1 with unreachable endpoint.
  EdsResourceArgs args1({{"locality0", {MakeNonExistantEndpoint()}}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName1));
  // Populate CDS resource for cluster 1.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewClusterName1);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName1);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  // Populate EDS resource for cluster 2.
  args1 = EdsResourceArgs({{"locality1", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName2));
  // Populate CDS resource for cluster 2.
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewClusterName2);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName2);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewClusterName1);
  cluster_config.add_clusters(kNewClusterName2);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Wait for channel to get the resources and get connected.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Send an EDS update for cluster 1 that reuses the locality name from
  // cluster 1 and points traffic to backend 1.
  args1 = EdsResourceArgs({{"locality1", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName1));
  WaitForBackend(DEBUG_LOCATION, 1);
}

// This tests a bug seen in the wild where the cds LB policy was
// incorrectly modifying its copy of the XdsClusterResource for the root
// cluster when generating the child policy config, so when we later
// received an update for one of the underlying clusters, we were no
// longer able to generate a valid child policy config.
TEST_P(AggregateClusterTest, UpdateOfChildCluster) {
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  // Populate new EDS resources.
  EdsResourceArgs args1({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends(1, 2)},
  });
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
  // Create Aggregate Cluster
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Wait for traffic to go to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Now reconfigure the underlying cluster to point to a different EDS
  // resource containing backend 1.
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsService2Name);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  // Wait for traffic to go to backend 1.
  WaitForBackend(DEBUG_LOCATION, 1);
  response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
}

TEST_P(AggregateClusterTest, DiamondDependency) {
  const char* kNewClusterName1 = "new_cluster_1";
  const char* kNewEdsServiceName1 = "new_eds_service_name_1";
  const char* kNewClusterName2 = "new_cluster_2";
  const char* kNewEdsServiceName2 = "new_eds_service_name_2";
  const char* kNewAggregateClusterName = "new_aggregate_cluster";
  // Populate new EDS resources.
  CreateAndStartBackends(2);
  EdsResourceArgs args1({{"locality0", CreateEndpointsForBackends(0, 1)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsServiceName1));
  EdsResourceArgs args2({{"locality0", CreateEndpointsForBackends(1, 2)}});
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args2, kNewEdsServiceName2));
  // Populate new CDS resources.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewClusterName1);
  new_cluster1.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName1);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  Cluster new_cluster2 = default_cluster_;
  new_cluster2.set_name(kNewClusterName2);
  new_cluster2.mutable_eds_cluster_config()->set_service_name(
      kNewEdsServiceName2);
  balancer_->ads_service()->SetCdsResource(new_cluster2);
  // Populate top-level aggregate cluster pointing to kNewClusterName1
  // and kNewAggregateClusterName.
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewClusterName1);
  cluster_config.add_clusters(kNewAggregateClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Populate kNewAggregateClusterName aggregate cluster pointing to
  // kNewClusterName1 and kNewClusterName2.
  auto aggregate_cluster2 = default_cluster_;
  aggregate_cluster2.set_name(kNewAggregateClusterName);
  custom_cluster = aggregate_cluster2.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  cluster_config.Clear();
  cluster_config.add_clusters(kNewClusterName1);
  cluster_config.add_clusters(kNewClusterName2);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(aggregate_cluster2);
  // Wait for traffic to go to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  backends_[0]->StopListeningAndSendGoaways();
  WaitForBackend(DEBUG_LOCATION, 1);
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  ShutdownBackend(0);
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0);
}

TEST_P(AggregateClusterTest, DependencyLoopWithNoLeafClusters) {
  const char* kNewClusterName1 = "new_cluster_1";
  // Default cluster is an aggregate cluster pointing to kNewClusterName1.
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewClusterName1);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // kDefaultClusterName points to the default cluster.
  cluster.set_name(kNewClusterName1);
  cluster_config.Clear();
  cluster_config.add_clusters(kDefaultClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // RPCs should fail.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "new_cluster_1: FAILED_PRECONDITION: "
                      "aggregate cluster graph has no leaf clusters");
}

TEST_P(AggregateClusterTest, DependencyLoopWithLeafClusters) {
  const char* kNewClusterName1 = "new_cluster_1";
  // Populate new EDS resource.
  CreateAndStartBackends(1);
  EdsResourceArgs args1({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args1));
  // Populate new CDS resource.
  Cluster new_cluster1 = default_cluster_;
  new_cluster1.set_name(kNewClusterName1);
  balancer_->ads_service()->SetCdsResource(new_cluster1);
  // Populate top-level aggregate cluster pointing to itself and the new
  // CDS cluster.
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewClusterName1);
  cluster_config.add_clusters(kDefaultClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // RPCs should work.
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(AggregateClusterTest, RecursionDepthJustBelowMax) {
  // Populate EDS resource.
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(absl::StrCat(kDefaultClusterName, 15));
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populate aggregate cluster chain.
  for (int i = 14; i >= 0; --i) {
    auto cluster = default_cluster_;
    if (i > 0) cluster.set_name(absl::StrCat(kDefaultClusterName, i));
    CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
    custom_cluster->set_name("envoy.clusters.aggregate");
    ClusterConfig cluster_config;
    cluster_config.add_clusters(absl::StrCat(kDefaultClusterName, i + 1));
    custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
    balancer_->ads_service()->SetCdsResource(cluster);
  }
  // RPCs should fail with the right status.
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(AggregateClusterTest, RecursionMaxDepth) {
  // Populate EDS resource.
  CreateAndStartBackends(1);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Populate new CDS resource.
  Cluster new_cluster = default_cluster_;
  new_cluster.set_name(absl::StrCat(kDefaultClusterName, 16));
  balancer_->ads_service()->SetCdsResource(new_cluster);
  // Populate aggregate cluster chain.
  for (int i = 15; i >= 0; --i) {
    auto cluster = default_cluster_;
    if (i > 0) cluster.set_name(absl::StrCat(kDefaultClusterName, i));
    CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
    custom_cluster->set_name("envoy.clusters.aggregate");
    ClusterConfig cluster_config;
    cluster_config.add_clusters(absl::StrCat(kDefaultClusterName, i + 1));
    custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
    balancer_->ads_service()->SetCdsResource(cluster);
  }
  // RPCs should fail with the right status.
  const Status status = SendRpc();
  EXPECT_EQ(StatusCode::UNAVAILABLE, status.error_code());
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr("aggregate cluster graph exceeds max depth"));
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
