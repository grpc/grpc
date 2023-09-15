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

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/event_engine/endpoint_config.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/env.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "test/cpp/end2end/connection_attempt_injector.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::cluster::v3::CustomClusterType;
using ::envoy::config::core::v3::HealthStatus;
using ::envoy::extensions::clusters::aggregate::v3::ClusterConfig;

class RingHashTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    logical_dns_cluster_resolver_response_generator_ =
        grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
    InitClient();
    SetUpChannel();
  }

  void SetUpChannel(ChannelArguments* args = nullptr) {
    ChannelArguments local_args;
    if (args == nullptr) args = &local_args;
    args->SetPointerWithVtable(
        GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR,
        logical_dns_cluster_resolver_response_generator_.get(),
        &grpc_core::FakeResolverResponseGenerator::kChannelArgPointerVtable);
    ResetStub(/*failover_timeout_ms=*/0, args);
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

  std::string CreateMetadataValueThatHashesToBackendPort(int port) {
    return absl::StrCat(ipv6_only_ ? "[::1]" : "127.0.0.1", ":", port, "_0");
  }

  std::string CreateMetadataValueThatHashesToBackend(int index) {
    return CreateMetadataValueThatHashesToBackendPort(backends_[index]->port());
  }

  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      logical_dns_cluster_resolver_response_generator_;
};

// Run both with and without load reporting, just for test coverage.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, RingHashTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_enable_load_reporting()),
    &XdsTestType::Name);

TEST_P(RingHashTest, AggregateClusterFallBackFromRingHashAtStartup) {
  CreateAndStartBackends(2);
  const char* kNewCluster1Name = "new_cluster_1";
  const char* kNewEdsService1Name = "new_eds_service_name_1";
  const char* kNewCluster2Name = "new_cluster_2";
  const char* kNewEdsService2Name = "new_eds_service_name_2";
  // Populate new EDS resources.
  EdsResourceArgs args1({
      {"locality0", {MakeNonExistantEndpoint(), MakeNonExistantEndpoint()}},
  });
  EdsResourceArgs args2({
      {"locality0", CreateEndpointsForBackends()},
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
  cluster.set_lb_policy(Cluster::RING_HASH);
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kNewCluster1Name);
  cluster_config.add_clusters(kNewCluster2Name);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set up route with channel id hashing
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Verifying that we are using ring hash as only 1 endpoint is receiving all
  // the traffic.
  CheckRpcSendOk(DEBUG_LOCATION, 100, RpcOptions().set_timeout_ms(5000));
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 100)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST_P(RingHashTest,
       AggregateClusterFallBackFromRingHashToLogicalDnsAtStartup) {
  CreateAndStartBackends(1);
  const char* kEdsClusterName = "eds_cluster";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate EDS resource.
  EdsResourceArgs args({
      {"locality0",
       {MakeNonExistantEndpoint(), MakeNonExistantEndpoint()},
       kDefaultLocalityWeight,
       0},
      {"locality1",
       {MakeNonExistantEndpoint(), MakeNonExistantEndpoint()},
       kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Populate new CDS resources.
  Cluster eds_cluster = default_cluster_;
  eds_cluster.set_name(kEdsClusterName);
  balancer_->ads_service()->SetCdsResource(eds_cluster);
  // Populate LOGICAL_DNS cluster.
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
  cluster.set_lb_policy(Cluster::RING_HASH);
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kEdsClusterName);
  cluster_config.add_clusters(kLogicalDNSClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set up route with channel id hashing
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts());
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Inject connection delay to make this act more realistically.
  ConnectionAttemptInjector injector;
  injector.SetDelay(grpc_core::Duration::Milliseconds(500) *
                    grpc_test_slowdown_factor());
  // Send RPC.  Need the timeout to be long enough to account for the
  // subchannel connection delays.
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(5000));
}

TEST_P(RingHashTest,
       AggregateClusterFallBackFromRingHashToLogicalDnsAtStartupNoFailedRpcs) {
  CreateAndStartBackends(1);
  const char* kEdsClusterName = "eds_cluster";
  const char* kLogicalDNSClusterName = "logical_dns_cluster";
  // Populate EDS resource.
  EdsResourceArgs args({
      {"locality0",
       {MakeNonExistantEndpoint(), MakeNonExistantEndpoint()},
       kDefaultLocalityWeight,
       0},
      {"locality1",
       {MakeNonExistantEndpoint(), MakeNonExistantEndpoint()},
       kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Populate new CDS resources.
  Cluster eds_cluster = default_cluster_;
  eds_cluster.set_name(kEdsClusterName);
  balancer_->ads_service()->SetCdsResource(eds_cluster);
  // Populate LOGICAL_DNS cluster.
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
  cluster.set_lb_policy(Cluster::RING_HASH);
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters(kEdsClusterName);
  cluster_config.add_clusters(kLogicalDNSClusterName);
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Set up route with channel id hashing
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Set Logical DNS result
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Resolver::Result result;
    result.addresses = CreateAddressListFromPortList(GetBackendPorts());
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Set up connection attempt injector.
  ConnectionAttemptInjector injector;
  auto hold = injector.AddHold(backends_[0]->port());
  // Increase subchannel backoff time, so that subchannels stay in
  // TRANSIENT_FAILURE for long enough to trigger potential problems.
  ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS,
                      10000 * grpc_test_slowdown_factor());
  SetUpChannel(&channel_args);
  // Start an RPC in the background.
  LongRunningRpc rpc;
  rpc.StartRpc(stub_.get(), RpcOptions().set_timeout_ms(5000));
  // Wait for connection attempt to the backend.
  hold->Wait();
  // Channel should report CONNECTING here, and any RPC should be queued.
  EXPECT_EQ(channel_->GetState(false), GRPC_CHANNEL_CONNECTING);
  // Start a second RPC at this point, which should be queued as well.
  // This will fail if the priority policy fails to update the picker to
  // point to the LOGICAL_DNS child; if it leaves it pointing to the EDS
  // priority 1, then the RPC will fail, because all subchannels are in
  // TRANSIENT_FAILURE.
  // Note that sending only the first RPC does not catch this case,
  // because if the priority policy fails to update the picker, then the
  // pick for the first RPC will not be retried.
  LongRunningRpc rpc2;
  rpc2.StartRpc(stub_.get(), RpcOptions().set_timeout_ms(5000));
  // Allow the connection attempt to complete.
  hold->Resume();
  // Now the RPCs should complete successfully.
  gpr_log(GPR_INFO, "=== WAITING FOR FIRST RPC TO FINISH ===");
  Status status = rpc.GetStatus();
  gpr_log(GPR_INFO, "=== FIRST RPC FINISHED ===");
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
  gpr_log(GPR_INFO, "=== WAITING FOR SECOND RPC TO FINISH ===");
  status = rpc2.GetStatus();
  gpr_log(GPR_INFO, "=== SECOND RPC FINISHED ===");
  EXPECT_TRUE(status.ok()) << "code=" << status.error_code()
                           << " message=" << status.error_message();
}

// Tests that ring hash policy that hashes using channel id ensures all RPCs
// to go 1 particular backend.
TEST_P(RingHashTest, ChannelIdHashing) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 100, RpcOptions().set_timeout_ms(5000));
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 100)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// Tests that ring hash policy that hashes using a header value can spread
// RPCs across all the backends.
TEST_P(RingHashTest, HeaderHashing) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Note each type of RPC will contains a header value that will always be
  // hashed to a specific backend as the header value matches the value used
  // to create the entry in the ring.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  std::vector<std::pair<std::string, std::string>> metadata2 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(2)}};
  std::vector<std::pair<std::string, std::string>> metadata3 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(3)}};
  const auto rpc_options =
      RpcOptions().set_metadata(std::move(metadata)).set_timeout_ms(5000);
  const auto rpc_options1 =
      RpcOptions().set_metadata(std::move(metadata1)).set_timeout_ms(5000);
  const auto rpc_options2 =
      RpcOptions().set_metadata(std::move(metadata2)).set_timeout_ms(5000);
  const auto rpc_options3 =
      RpcOptions().set_metadata(std::move(metadata3)).set_timeout_ms(5000);
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options1);
  WaitForBackend(DEBUG_LOCATION, 2, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options2);
  WaitForBackend(DEBUG_LOCATION, 3, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options3);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options2);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options3);
  for (size_t i = 0; i < backends_.size(); ++i) {
    EXPECT_EQ(100, backends_[i]->backend_service()->request_count());
  }
}

// Tests that ring hash policy that hashes using a header value and regex
// rewrite to aggregate RPCs to 1 backend.
TEST_P(RingHashTest, HeaderHashingWithRegexRewrite) {
  CreateAndStartBackends(4);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  hash_policy->mutable_header()
      ->mutable_regex_rewrite()
      ->mutable_pattern()
      ->set_regex("[0-9]+");
  hash_policy->mutable_header()->mutable_regex_rewrite()->set_substitution(
      "foo");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  std::vector<std::pair<std::string, std::string>> metadata1 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(1)}};
  std::vector<std::pair<std::string, std::string>> metadata2 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(2)}};
  std::vector<std::pair<std::string, std::string>> metadata3 = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(3)}};
  const auto rpc_options =
      RpcOptions().set_metadata(std::move(metadata)).set_timeout_ms(5000);
  const auto rpc_options1 =
      RpcOptions().set_metadata(std::move(metadata1)).set_timeout_ms(5000);
  const auto rpc_options2 =
      RpcOptions().set_metadata(std::move(metadata2)).set_timeout_ms(5000);
  const auto rpc_options3 =
      RpcOptions().set_metadata(std::move(metadata3)).set_timeout_ms(5000);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options1);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options2);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options3);
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 400)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// Tests that ring hash policy that hashes using a random value.
TEST_P(RingHashTest, NoHashPolicy) {
  CreateAndStartBackends(2);
  const double kDistribution50Percent = 0.5;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDistribution50Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions().set_timeout_ms(kRpcTimeoutMs),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  const int request_count_1 = backends_[0]->backend_service()->request_count();
  const int request_count_2 = backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(request_count_1) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_2) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
}

// Tests that we observe endpoint weights.
TEST_P(RingHashTest, EndpointWeights) {
  CreateAndStartBackends(3);
  const double kDistribution50Percent = 0.5;
  const double kDistribution25Percent = 0.25;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDistribution50Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Endpoint 0 has weight 0, will be treated as weight 1.
  // Endpoint 1 has weight 1.
  // Endpoint 2 has weight 2.
  EdsResourceArgs args(
      {{"locality0",
        {CreateEndpoint(0, ::envoy::config::core::v3::HealthStatus::UNKNOWN, 0),
         CreateEndpoint(1, ::envoy::config::core::v3::HealthStatus::UNKNOWN, 1),
         CreateEndpoint(2, ::envoy::config::core::v3::HealthStatus::UNKNOWN,
                        2)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(DEBUG_LOCATION, 0, 3, /*check_status=*/nullptr,
                     WaitForBackendOptions().set_timeout_ms(kRpcTimeoutMs),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  // Endpoint 2 should see 50% of traffic, and endpoints 0 and 1 should
  // each see 25% of traffic.
  const int request_count_0 = backends_[0]->backend_service()->request_count();
  const int request_count_1 = backends_[1]->backend_service()->request_count();
  const int request_count_2 = backends_[2]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(request_count_0) / kNumRpcs,
              ::testing::DoubleNear(kDistribution25Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_1) / kNumRpcs,
              ::testing::DoubleNear(kDistribution25Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_2) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
}

// Test that ring hash policy evaluation will continue past the terminal
// policy if no results are produced yet.
TEST_P(RingHashTest, ContinuesPastTerminalPolicyThatDoesNotProduceResult) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("header_not_present");
  hash_policy->set_terminal(true);
  auto* hash_policy2 = route->mutable_route()->add_hash_policy();
  hash_policy2->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  const auto rpc_options =
      RpcOptions().set_metadata(std::move(metadata)).set_timeout_ms(5000);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 100);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 0);
}

// Test random hash is used when header hashing specified a header field that
// the RPC did not have.
TEST_P(RingHashTest, HashOnHeaderThatIsNotPresent) {
  CreateAndStartBackends(2);
  const double kDistribution50Percent = 0.5;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDistribution50Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("header_not_present");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"unmatched_header", absl::StrFormat("%" PRIu32, rand())},
  };
  const auto rpc_options = RpcOptions().set_metadata(std::move(metadata));
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions().set_timeout_ms(kRpcTimeoutMs),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs, rpc_options);
  const int request_count_1 = backends_[0]->backend_service()->request_count();
  const int request_count_2 = backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(request_count_1) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_2) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
}

// Test random hash is used when only unsupported hash policies are
// configured.
TEST_P(RingHashTest, UnsupportedHashPolicyDefaultToRandomHashing) {
  CreateAndStartBackends(2);
  const double kDistribution50Percent = 0.5;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kDistribution50Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy_unsupported_1 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_1->mutable_cookie()->set_name("cookie");
  auto* hash_policy_unsupported_2 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_2->mutable_connection_properties()->set_source_ip(
      true);
  auto* hash_policy_unsupported_3 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_3->mutable_query_parameter()->set_name(
      "query_parameter");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions().set_timeout_ms(kRpcTimeoutMs),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  const int request_count_1 = backends_[0]->backend_service()->request_count();
  const int request_count_2 = backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(request_count_1) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(request_count_2) / kNumRpcs,
              ::testing::DoubleNear(kDistribution50Percent, kErrorTolerance));
}

// Tests that ring hash policy that hashes using a random value can spread
// RPCs across all the backends according to locality weight.
TEST_P(RingHashTest, RandomHashingDistributionAccordingToEndpointWeight) {
  CreateAndStartBackends(2);
  const size_t kWeight1 = 1;
  const size_t kWeight2 = 2;
  const size_t kWeightTotal = kWeight1 + kWeight2;
  const double kWeight33Percent = static_cast<double>(kWeight1) / kWeightTotal;
  const double kWeight66Percent = static_cast<double>(kWeight2) / kWeightTotal;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kWeight33Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args({{"locality0",
                         {CreateEndpoint(0, HealthStatus::UNKNOWN, 1),
                          CreateEndpoint(1, HealthStatus::UNKNOWN, 2)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions().set_timeout_ms(kRpcTimeoutMs),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  const int weight_33_request_count =
      backends_[0]->backend_service()->request_count();
  const int weight_66_request_count =
      backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(weight_33_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight33Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_66_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight66Percent, kErrorTolerance));
}

// Tests that ring hash policy that hashes using a random value can spread
// RPCs across all the backends according to locality weight.
TEST_P(RingHashTest,
       RandomHashingDistributionAccordingToLocalityAndEndpointWeight) {
  CreateAndStartBackends(2);
  const size_t kWeight1 = 1 * 1;
  const size_t kWeight2 = 2 * 2;
  const size_t kWeightTotal = kWeight1 + kWeight2;
  const double kWeight20Percent = static_cast<double>(kWeight1) / kWeightTotal;
  const double kWeight80Percent = static_cast<double>(kWeight2) / kWeightTotal;
  const double kErrorTolerance = 0.05;
  const uint32_t kRpcTimeoutMs = 10000;
  const size_t kNumRpcs =
      ComputeIdealNumRpcs(kWeight20Percent, kErrorTolerance);
  auto cluster = default_cluster_;
  // Increasing min ring size for random distribution.
  cluster.mutable_ring_hash_lb_config()->mutable_minimum_ring_size()->set_value(
      100000);
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  EdsResourceArgs args(
      {{"locality0", {CreateEndpoint(0, HealthStatus::UNKNOWN, 1)}, 1},
       {"locality1", {CreateEndpoint(1, HealthStatus::UNKNOWN, 2)}, 2}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // TODO(donnadionne): remove extended timeout after ring creation
  // optimization.
  WaitForAllBackends(DEBUG_LOCATION, 0, 2, /*check_status=*/nullptr,
                     WaitForBackendOptions().set_timeout_ms(kRpcTimeoutMs),
                     RpcOptions().set_timeout_ms(kRpcTimeoutMs));
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  const int weight_20_request_count =
      backends_[0]->backend_service()->request_count();
  const int weight_80_request_count =
      backends_[1]->backend_service()->request_count();
  EXPECT_THAT(static_cast<double>(weight_20_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight20Percent, kErrorTolerance));
  EXPECT_THAT(static_cast<double>(weight_80_request_count) / kNumRpcs,
              ::testing::DoubleNear(kWeight80Percent, kErrorTolerance));
}

// Tests that ring hash policy that hashes using a fixed string ensures all
// RPCs to go 1 particular backend; and that subsequent hashing policies are
// ignored due to the setting of terminal.
TEST_P(RingHashTest, FixedHashingTerminalPolicy) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("fixed_string");
  hash_policy->set_terminal(true);
  auto* hash_policy_to_be_ignored = route->mutable_route()->add_hash_policy();
  hash_policy_to_be_ignored->mutable_header()->set_header_name("random_string");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"fixed_string", "fixed_value"},
      {"random_string", absl::StrFormat("%" PRIu32, rand())},
  };
  const auto rpc_options =
      RpcOptions().set_metadata(std::move(metadata)).set_timeout_ms(5000);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 100)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// Test that the channel will go from idle to ready via connecting;
// (tho it is not possible to catch the connecting state before moving to
// ready)
TEST_P(RingHashTest, IdleToReady) {
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_timeout_ms(5000));
  EXPECT_EQ(GRPC_CHANNEL_READY, channel_->GetState(false));
}

// Test that the channel will transition to READY once it starts
// connecting even if there are no RPCs being sent to the picker.
TEST_P(RingHashTest, ContinuesConnectingWithoutPicks) {
  // Create EDS resource.
  CreateAndStartBackends(1);
  auto non_existant_endpoint = MakeNonExistantEndpoint();
  EdsResourceArgs args(
      {{"locality0", {non_existant_endpoint, CreateEndpoint(0)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Change CDS resource to use RING_HASH.
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Add hash policy to RDS resource.
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Start connection attempt injector and add a hold for the P0
  // connection attempt.
  ConnectionAttemptInjector injector;
  auto hold = injector.AddHold(non_existant_endpoint.port);
  // A long-running RPC, just used to send the RPC in another thread.
  LongRunningRpc rpc;
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash",
       CreateMetadataValueThatHashesToBackendPort(non_existant_endpoint.port)}};
  rpc.StartRpc(stub_.get(), RpcOptions().set_timeout_ms(0).set_metadata(
                                std::move(metadata)));
  // Wait for the RPC to trigger the P0 connection attempt, then cancel it,
  // and then allow the connection attempt to complete.
  hold->Wait();
  rpc.CancelRpc();
  EXPECT_EQ(StatusCode::CANCELLED, rpc.GetStatus().error_code());
  hold->Resume();
  // Wait for channel to become connected without any pending RPC.
  EXPECT_TRUE(channel_->WaitForConnected(grpc_timeout_seconds_to_deadline(5)));
  // Make sure the backend did not get any requests.
  EXPECT_EQ(0UL, backends_[0]->backend_service()->request_count());
}

// Tests that when we trigger internal connection attempts without
// picks, we do so for only one subchannel at a time.
TEST_P(RingHashTest, ContinuesConnectingWithoutPicksOneSubchannelAtATime) {
  // Create EDS resource.
  CreateAndStartBackends(1);
  auto non_existant_endpoint0 = MakeNonExistantEndpoint();
  auto non_existant_endpoint1 = MakeNonExistantEndpoint();
  auto non_existant_endpoint2 = MakeNonExistantEndpoint();
  EdsResourceArgs args({{"locality0",
                         {non_existant_endpoint0, non_existant_endpoint1,
                          non_existant_endpoint2, CreateEndpoint(0)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Change CDS resource to use RING_HASH.
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Add hash policy to RDS resource.
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Start connection attempt injector.
  ConnectionAttemptInjector injector;
  auto hold_non_existant0 = injector.AddHold(non_existant_endpoint0.port);
  auto hold_non_existant1 = injector.AddHold(non_existant_endpoint1.port);
  auto hold_non_existant2 = injector.AddHold(non_existant_endpoint2.port);
  auto hold_good = injector.AddHold(backends_[0]->port());
  // A long-running RPC, just used to send the RPC in another thread.
  LongRunningRpc rpc;
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackendPort(
                           non_existant_endpoint0.port)}};
  rpc.StartRpc(stub_.get(), RpcOptions().set_timeout_ms(0).set_metadata(
                                std::move(metadata)));
  // Wait for the RPC to trigger a connection attempt to the first address,
  // then cancel the RPC.  No other connection attempts should be started yet.
  hold_non_existant0->Wait();
  rpc.CancelRpc();
  EXPECT_FALSE(hold_non_existant1->IsStarted());
  EXPECT_FALSE(hold_non_existant2->IsStarted());
  EXPECT_FALSE(hold_good->IsStarted());
  // Allow the connection attempt to the first address to resume and wait
  // for the attempt for the second address.  No other connection
  // attempts should be started yet.
  auto hold_non_existant0_again = injector.AddHold(non_existant_endpoint0.port);
  hold_non_existant0->Resume();
  hold_non_existant1->Wait();
  EXPECT_FALSE(hold_non_existant0_again->IsStarted());
  EXPECT_FALSE(hold_non_existant2->IsStarted());
  EXPECT_FALSE(hold_good->IsStarted());
  // Allow the connection attempt to the second address to resume and wait
  // for the attempt for the third address.  No other connection
  // attempts should be started yet.
  auto hold_non_existant1_again = injector.AddHold(non_existant_endpoint1.port);
  hold_non_existant1->Resume();
  hold_non_existant2->Wait();
  EXPECT_FALSE(hold_non_existant0_again->IsStarted());
  EXPECT_FALSE(hold_non_existant1_again->IsStarted());
  EXPECT_FALSE(hold_good->IsStarted());
  // Allow the connection attempt to the third address to resume and wait
  // for the attempt for the final address.  No other connection
  // attempts should be started yet.
  auto hold_non_existant2_again = injector.AddHold(non_existant_endpoint2.port);
  hold_non_existant2->Resume();
  hold_good->Wait();
  EXPECT_FALSE(hold_non_existant0_again->IsStarted());
  EXPECT_FALSE(hold_non_existant1_again->IsStarted());
  EXPECT_FALSE(hold_non_existant2_again->IsStarted());
  // Allow the final attempt to resume.
  hold_good->Resume();
  // Wait for channel to become connected without any pending RPC.
  EXPECT_TRUE(channel_->WaitForConnected(grpc_timeout_seconds_to_deadline(10)));
  // No other connection attempts should have been started.
  EXPECT_FALSE(hold_non_existant0_again->IsStarted());
  EXPECT_FALSE(hold_non_existant1_again->IsStarted());
  EXPECT_FALSE(hold_non_existant2_again->IsStarted());
  // RPC should have been cancelled.
  EXPECT_EQ(StatusCode::CANCELLED, rpc.GetStatus().error_code());
  // Make sure the backend did not get any requests.
  EXPECT_EQ(0UL, backends_[0]->backend_service()->request_count());
}

// Test that when the first pick is down leading to a transient failure, we
// will move on to the next ring hash entry.
TEST_P(RingHashTest, TransientFailureCheckNextOne) {
  CreateAndStartBackends(1);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  std::vector<EdsResourceArgs::Endpoint> endpoints;
  const int unused_port = grpc_pick_unused_port_or_die();
  endpoints.emplace_back(unused_port);
  endpoints.emplace_back(backends_[0]->port());
  EdsResourceArgs args({{"locality0", std::move(endpoints)}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash",
       CreateMetadataValueThatHashesToBackendPort(unused_port)}};
  const auto rpc_options =
      RpcOptions().set_metadata(std::move(metadata)).set_timeout_ms(5000);
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
}

// Test that when a backend goes down, we will move on to the next subchannel
// (with a lower priority).  When the backend comes back up, traffic will move
// back.
TEST_P(RingHashTest, SwitchToLowerPrioirtyAndThenBack) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({
      {"locality0", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality1", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  const auto rpc_options =
      RpcOptions().set_metadata(std::move(metadata)).set_timeout_ms(5000);
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  backends_[0]->StopListeningAndSendGoaways();
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  ShutdownBackend(0);
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  CheckRpcSendOk(DEBUG_LOCATION, 100, rpc_options);
  EXPECT_EQ(100, backends_[0]->backend_service()->request_count());
  EXPECT_EQ(0, backends_[1]->backend_service()->request_count());
}

// Test that when all backends are down, we will keep reattempting.
TEST_P(RingHashTest, ReattemptWhenAllEndpointsUnreachable) {
  CreateAndStartBackends(1);
  const uint32_t kConnectionTimeoutMilliseconds = 5000;
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args(
      {{"locality0", {MakeNonExistantEndpoint(), CreateEndpoint(0)}}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  ShutdownBackend(0);
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      MakeConnectionFailureRegex(
          "ring hash cannot find a connected endpoint; first failure: "),
      RpcOptions().set_metadata(std::move(metadata)));
  StartBackend(0);
  // Ensure we are actively connecting without any traffic.
  EXPECT_TRUE(channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
}

// Test that when all backends are down and then up, we may pick a TF backend
// and we will then jump to ready backend.
TEST_P(RingHashTest, TransientFailureSkipToAvailableReady) {
  CreateBackends(2);
  const uint32_t kConnectionTimeoutMilliseconds = 5000;
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_header()->set_header_name("address_hash");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Make sure we include some unused ports to fill the ring.
  EdsResourceArgs args({
      {"locality0",
       {CreateEndpoint(0), CreateEndpoint(1), MakeNonExistantEndpoint(),
        MakeNonExistantEndpoint()}},
  });
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"address_hash", CreateMetadataValueThatHashesToBackend(0)}};
  const auto rpc_options = RpcOptions()
                               .set_metadata(std::move(metadata))
                               .set_timeout_ms(kConnectionTimeoutMilliseconds);
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  gpr_log(GPR_INFO, "=== SENDING FIRST RPC ===");
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      MakeConnectionFailureRegex(
          "ring hash cannot find a connected endpoint; first failure: "),
      rpc_options);
  gpr_log(GPR_INFO, "=== DONE WITH FIRST RPC ===");
  EXPECT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel_->GetState(false));
  // Bring up backend 0.  The channel should become connected without
  // any picks, because in TF, we are always trying to connect to at
  // least one backend at all times.
  gpr_log(GPR_INFO, "=== STARTING BACKEND 0 ===");
  StartBackend(0);
  gpr_log(GPR_INFO, "=== WAITING FOR CHANNEL TO BECOME READY ===");
  EXPECT_TRUE(channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
  // RPCs should go to backend 0.
  gpr_log(GPR_INFO, "=== WAITING FOR BACKEND 0 ===");
  WaitForBackend(DEBUG_LOCATION, 0, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  EXPECT_EQ(GRPC_CHANNEL_READY, channel_->GetState(false));
  // Bring down backend 0 and bring up backend 1.
  // Note the RPC contains a header value that will always be hashed to
  // backend 0. So by purposely bringing down backend 0 and bringing up another
  // backend, this will ensure Picker's first choice of backend 0 will fail
  // and it will go through the remaining subchannels to find one in READY.
  // Since the the entries in the ring are pretty distributed and we have
  // unused ports to fill the ring, it is almost guaranteed that the Picker
  // will go through some non-READY entries and skip them as per design.
  gpr_log(GPR_INFO, "=== SHUTTING DOWN BACKEND 0 ===");
  ShutdownBackend(0);
  gpr_log(GPR_INFO, "=== WAITING FOR STATE CHANGE ===");
  EXPECT_TRUE(channel_->WaitForStateChange(
      GRPC_CHANNEL_READY,
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
  EXPECT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel_->GetState(false));
  gpr_log(GPR_INFO, "=== SENDING SECOND RPC ===");
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      MakeConnectionFailureRegex(
          "ring hash cannot find a connected endpoint; first failure: "),
      rpc_options);
  gpr_log(GPR_INFO, "=== STARTING BACKEND 1 ===");
  StartBackend(1);
  gpr_log(GPR_INFO, "=== WAITING FOR CHANNEL TO BECOME READY ===");
  EXPECT_TRUE(channel_->WaitForConnected(
      grpc_timeout_milliseconds_to_deadline(kConnectionTimeoutMilliseconds)));
  gpr_log(GPR_INFO, "=== WAITING FOR BACKEND 1 ===");
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), rpc_options);
  gpr_log(GPR_INFO, "=== DONE ===");
}

// This tests a bug seen in the wild where ring_hash started with no
// endpoints and reported TRANSIENT_FAILURE, then got an update with
// endpoints and reported IDLE, but the picker update was squelched, so
// it failed to ever get reconnected.
TEST_P(RingHashTest, ReattemptWhenGoingFromTransientFailureToIdle) {
  CreateAndStartBackends(1);
  const uint32_t kConnectionTimeoutMilliseconds = 5000;
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  // Send empty EDS update.
  EdsResourceArgs args(
      {{"locality0", std::vector<EdsResourceArgs::Endpoint>()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  EXPECT_EQ(GRPC_CHANNEL_IDLE, channel_->GetState(false));
  // Channel should fail RPCs and go into TRANSIENT_FAILURE.
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      "empty address list: EDS resource eds_service_name contains empty "
      "localities: \\[\\{region=\"xds_default_locality_region\", "
      "zone=\"xds_default_locality_zone\", sub_zone=\"locality0\"\\}\\]",
      RpcOptions().set_timeout_ms(kConnectionTimeoutMilliseconds));
  EXPECT_EQ(GRPC_CHANNEL_TRANSIENT_FAILURE, channel_->GetState(false));
  // Send EDS update with 1 backend.
  args = EdsResourceArgs({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // A wait_for_ready RPC should succeed, and the channel should report READY.
  CheckRpcSendOk(DEBUG_LOCATION, 1,
                 RpcOptions()
                     .set_timeout_ms(kConnectionTimeoutMilliseconds)
                     .set_wait_for_ready(true));
  EXPECT_EQ(GRPC_CHANNEL_READY, channel_->GetState(false));
}

// Test unspported hash policy types are all ignored before a supported
// policy.
TEST_P(RingHashTest, UnsupportedHashPolicyUntilChannelIdHashing) {
  CreateAndStartBackends(2);
  auto cluster = default_cluster_;
  cluster.set_lb_policy(Cluster::RING_HASH);
  balancer_->ads_service()->SetCdsResource(cluster);
  auto new_route_config = default_route_config_;
  auto* route = new_route_config.mutable_virtual_hosts(0)->mutable_routes(0);
  auto* hash_policy_unsupported_1 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_1->mutable_cookie()->set_name("cookie");
  auto* hash_policy_unsupported_2 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_2->mutable_connection_properties()->set_source_ip(
      true);
  auto* hash_policy_unsupported_3 = route->mutable_route()->add_hash_policy();
  hash_policy_unsupported_3->mutable_query_parameter()->set_name(
      "query_parameter");
  auto* hash_policy = route->mutable_route()->add_hash_policy();
  hash_policy->mutable_filter_state()->set_key("io.grpc.channel_id");
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_,
                                   new_route_config);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  CheckRpcSendOk(DEBUG_LOCATION, 100, RpcOptions().set_timeout_ms(5000));
  bool found = false;
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (backends_[i]->backend_service()->request_count() > 0) {
      EXPECT_EQ(backends_[i]->backend_service()->request_count(), 100)
          << "backend " << i;
      EXPECT_FALSE(found) << "backend " << i;
      found = true;
    }
  }
  EXPECT_TRUE(found);
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
