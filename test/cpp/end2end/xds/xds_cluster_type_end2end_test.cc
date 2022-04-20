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

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "test/cpp/end2end/connection_delay_injector.h"
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
      addresses.emplace_back(address.addr, address.len, nullptr);
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
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // RPCs should succeed.
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(LogicalDNSClusterTest, MissingLoadAssignment) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "load_assignment not present for LOGICAL_DNS cluster"));
}

TEST_P(LogicalDNSClusterTest, MissingLocalities) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("load_assignment for LOGICAL_DNS cluster must have "
                           "exactly one locality, found 0"));
}

TEST_P(LogicalDNSClusterTest, MultipleLocalities) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* load_assignment = cluster.mutable_load_assignment();
  load_assignment->add_endpoints();
  load_assignment->add_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(
      response_state->error_message,
      ::testing::HasSubstr("load_assignment for LOGICAL_DNS cluster must have "
                           "exactly one locality, found 2"));
}

TEST_P(LogicalDNSClusterTest, MissingEndpoints) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "locality for LOGICAL_DNS cluster must have exactly one "
                  "endpoint, found 0"));
}

TEST_P(LogicalDNSClusterTest, MultipleEndpoints) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  auto* locality = cluster.mutable_load_assignment()->add_endpoints();
  locality->add_lb_endpoints();
  locality->add_lb_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr(
                  "locality for LOGICAL_DNS cluster must have exactly one "
                  "endpoint, found 2"));
}

TEST_P(LogicalDNSClusterTest, EmptyEndpoint) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("LbEndpoint endpoint field not set"));
}

TEST_P(LogicalDNSClusterTest, EndpointMissingAddress) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Endpoint address field not set"));
}

TEST_P(LogicalDNSClusterTest, AddressMissingSocketAddress) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("Address socket_address field not set"));
}

TEST_P(LogicalDNSClusterTest, SocketAddressHasResolverName) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address()
      ->set_resolver_name("foo");
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("LOGICAL_DNS clusters must NOT have a "
                                   "custom resolver name set"));
}

TEST_P(LogicalDNSClusterTest, SocketAddressMissingAddress) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address();
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("SocketAddress address field not set"));
}

TEST_P(LogicalDNSClusterTest, SocketAddressMissingPort) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
  // Create Logical DNS Cluster
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address()
      ->set_address(kServerName);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("SocketAddress port_value field not set"));
}

// Test that CDS client should send a NACK if cluster type is Logical DNS but
// the feature is not yet supported.
TEST_P(LogicalDNSClusterTest, Disabled) {
  auto cluster = default_cluster_;
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("DiscoveryType is not valid."));
}

//
// aggregate cluster tests
//

// TODO(roth): Add tests showing that load reporting is enabled on a
// per-underlying-cluster basis.

using AggregateClusterTest = ClusterTypeTest;

INSTANTIATE_TEST_SUITE_P(XdsTest, AggregateClusterTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(AggregateClusterTest, ) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
  ShutdownBackend(0);
  WaitForBackend(DEBUG_LOCATION, 1,
                 WaitForBackendOptions().set_allow_failures(true));
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0);
}

TEST_P(AggregateClusterTest, DiamondDependency) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
  ShutdownBackend(0);
  WaitForBackend(DEBUG_LOCATION, 1,
                 WaitForBackendOptions().set_allow_failures(true));
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
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
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
  // This class injects itself into all TCP connection attempts made
  // against iomgr.  It intercepts the attempts for the P0 and P1
  // backends and allows them to proceed as desired to simulate the case
  // being tested.
  class ConnectionInjector : public ConnectionAttemptInjector {
   public:
    ConnectionInjector(int p0_port, int p1_port)
        : p0_port_(p0_port), p1_port_(p1_port) {}

    void HandleConnection(grpc_closure* closure, grpc_endpoint** ep,
                          grpc_pollset_set* interested_parties,
                          const grpc_channel_args* channel_args,
                          const grpc_resolved_address* addr,
                          grpc_core::Timestamp deadline) override {
      {
        grpc_core::MutexLock lock(&mu_);
        const int port = grpc_sockaddr_get_port(addr);
        gpr_log(GPR_INFO, "==> HandleConnection(): state_=%d, port=%d", state_,
                port);
        switch (state_) {
          case kInit:
            // Make P0 report TF, which should trigger us to try to connect to
            // P1.
            if (port == p0_port_) {
              gpr_log(GPR_INFO, "*** INJECTING FAILURE FOR P0 ENDPOINT");
              grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure,
                                      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                          "injected connection failure"));
              state_ = kP0Failed;
              return;
            }
            break;
          case kP0Failed:
            // Hold connection attempt to P1 so that it stays in CONNECTING.
            if (port == p1_port_) {
              gpr_log(GPR_INFO,
                      "*** DELAYING CONNECTION ATTEMPT FOR P1 ENDPOINT");
              queued_p1_attempt_ = absl::make_unique<QueuedAttempt>(
                  closure, ep, interested_parties, channel_args, addr,
                  deadline);
              state_ = kDone;
              return;
            }
            break;
          case kDone:
            // P0 should attempt reconnection.  Log it to make the test
            // easier to debug, but allow it to complete, so that the
            // priority policy deactivates P1.
            if (port == p0_port_) {
              gpr_log(GPR_INFO,
                      "*** INTERCEPTING CONNECTION ATTEMPT FOR P0 ENDPOINT");
            }
            break;
        }
      }
      AttemptConnection(closure, ep, interested_parties, channel_args, addr,
                        deadline);
    }

    // Invoked by the test when the RPC to the P0 backend has succeeded
    // and it's ready to allow the P1 connection attempt to proceed.
    void CompletePriority1Connection() {
      grpc_core::ExecCtx exec_ctx;
      std::unique_ptr<QueuedAttempt> attempt;
      {
        grpc_core::MutexLock lock(&mu_);
        GPR_ASSERT(state_ == kDone);
        attempt = std::move(queued_p1_attempt_);
      }
      attempt->Resume();
    }

   private:
    const int p0_port_;
    const int p1_port_;

    grpc_core::Mutex mu_;
    enum {
      kInit,
      kP0Failed,
      kDone,
    } state_ ABSL_GUARDED_BY(mu_) = kInit;
    std::unique_ptr<QueuedAttempt> queued_p1_attempt_ ABSL_GUARDED_BY(mu_);
  };
  ConnectionInjector connection_attempt_injector(backends_[0]->port(),
                                                 backends_[1]->port());
  connection_attempt_injector.Start();
  // Wait for P0 backend.
  // Increase timeout to account for subchannel connection delays.
  WaitForBackend(DEBUG_LOCATION, 0, WaitForBackendOptions(),
                 RpcOptions().set_timeout_ms(2000));
  // Bring down the P0 backend.
  ShutdownBackend(0);
  // Allow the connection attempt to the P1 backend to resume.
  connection_attempt_injector.CompletePriority1Connection();
  // Wait for P1 backend to start getting traffic.
  WaitForBackend(DEBUG_LOCATION, 1);
}

TEST_P(AggregateClusterTest, EdsToLogicalDns) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Wait for traffic to go to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  ShutdownBackend(0);
  WaitForBackend(DEBUG_LOCATION, 1,
                 WaitForBackendOptions().set_allow_failures(true));
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
  StartBackend(0);
  WaitForBackend(DEBUG_LOCATION, 0);
}

TEST_P(AggregateClusterTest, LogicalDnsToEds) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // Wait for traffic to go to backend 0.
  WaitForBackend(DEBUG_LOCATION, 0);
  // Shutdown backend 0 and wait for all traffic to go to backend 1.
  ShutdownBackend(0);
  WaitForBackend(DEBUG_LOCATION, 1,
                 WaitForBackendOptions().set_allow_failures(true));
  auto response_state = balancer_->ads_service()->cds_response_state();
  ASSERT_TRUE(response_state.has_value());
  EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  // Bring backend 0 back and ensure all traffic go back to it.
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
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
    logical_dns_cluster_resolver_response_generator_->SetResponse(
        std::move(result));
  }
  // When an RPC fails, we know the channel has seen the update.
  CheckRpcSendFailure(DEBUG_LOCATION);
  // Send an EDS update that moves locality1 to priority 0.
  args1 = EdsResourceArgs({
      {"locality1", CreateEndpointsForBackends(0, 1), kDefaultLocalityWeight,
       0},
      {"locality2", CreateEndpointsForBackends(1, 2), kDefaultLocalityWeight,
       1},
  });
  balancer_->ads_service()->SetEdsResource(
      BuildEdsResource(args1, kNewEdsService1Name));
  WaitForBackend(DEBUG_LOCATION, 0,
                 WaitForBackendOptions().set_allow_failures(true));
}

TEST_P(AggregateClusterTest, MultipleClustersWithSameLocalities) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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

TEST_P(AggregateClusterTest, RecursionDepthJustBelowMax) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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
  ScopedExperimentalEnvVar env_var(
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AGGREGATE_AND_LOGICAL_DNS_CLUSTER");
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

// Test that CDS client should send a NACK if cluster type is AGGREGATE but
// the feature is not yet supported.
TEST_P(AggregateClusterTest, Disabled) {
  auto cluster = default_cluster_;
  CustomClusterType* custom_cluster = cluster.mutable_cluster_type();
  custom_cluster->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters("cluster1");
  cluster_config.add_clusters("cluster2");
  custom_cluster->mutable_typed_config()->PackFrom(cluster_config);
  cluster.set_type(Cluster::LOGICAL_DNS);
  balancer_->ads_service()->SetCdsResource(cluster);
  const auto response_state = WaitForCdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::HasSubstr("DiscoveryType is not valid."));
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
  grpc::testing::ConnectionAttemptInjector::Init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
