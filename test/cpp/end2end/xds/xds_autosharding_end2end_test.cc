//
// Copyright 2026 gRPC authors.
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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/extensions/load_balancing_policies/autosharding/v3/autosharding.pb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.pb.h"
#include "src/core/config/config_vars.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/autosharding_test_server.h"
#include "test/cpp/end2end/connection_attempt_injector.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::core::v3::HealthStatus;
using ::envoy::extensions::grpc_service::channel_credentials::insecure::v3::
    InsecureCredentials;
using ::envoy::extensions::load_balancing_policies::autosharding::v3::
    Autosharding;
using ::envoy::extensions::load_balancing_policies::wrr_locality::v3::
    WrrLocality;

constexpr absl::string_view kKeyHeaderName = "x-shard-key";
constexpr absl::string_view kAutoshardingTarget = "autosharding-target";

// XdsEnd2endTest has its own ServerThread class, so name the fake
// autosharding server's thread here.
using AutoshardingServerThread = ServerThread<AutoshardingServiceImpl>;

class XdsAutoshardingTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {
    autosharding_server_ =
        std::make_unique<AutoshardingServerThread>("autosharding");
    autosharding_server_->Start();
    // With a trusted xDS server, the GrpcService proto gives the
    // credentials for the autosharding server.
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer());
  }

  void TearDown() override {
    autosharding_server_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  // Returns an Autosharding config for the fake autosharding server.
  Autosharding MakeAutosharding(absl::string_view autosharding_target) const {
    Autosharding autosharding;
    auto* google_grpc =
        autosharding.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->set_target_uri(
        grpc_core::LocalIpAndPort(autosharding_server_->port()));
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        InsecureCredentials());
    autosharding.set_autosharding_target(autosharding_target);
    autosharding.set_key_header_name(kKeyHeaderName);
    return autosharding;
  }

  // Sets the LB policy of the default cluster.
  void SetClusterLbPolicy(const google::protobuf::Message& lb_policy) {
    auto cluster = default_cluster_;
    cluster.mutable_load_balancing_policy()
        ->add_policies()
        ->mutable_typed_extension_config()
        ->mutable_typed_config()
        ->PackFrom(lb_policy);
    balancer_->ads_service()->SetCdsResource(cluster);
  }

  // The endpoint name that the autosharding server uses for a backend.
  static std::string BackendName(size_t backend_idx) {
    return absl::StrCat("backend-", backend_idx);
  }

  // Returns an EDS endpoint for a backend, with hostname BackendName().
  EdsResourceArgs::Endpoint CreateNamedEndpoint(size_t backend_idx) {
    return CreateEndpoint(backend_idx, HealthStatus::UNKNOWN, /*lb_weight=*/1,
                          /*additional_backend_indexes=*/{},
                          BackendName(backend_idx));
  }

  // The target that a policy under wrr_locality sends for the target
  // "shard-%s" in a locality of the default region and zone.
  static std::string LocalityTarget(absl::string_view sub_zone) {
    return absl::StrFormat(
        "shard-{region=\"%s\", zone=\"%s\", sub_zone=\"%s\"}",
        kDefaultLocalityRegion, kDefaultLocalityZone, sub_zone);
  }

  static RpcOptions KeyRpcOptions(absl::string_view key) {
    return RpcOptions().set_metadata(
        {{std::string(kKeyHeaderName), std::string(key)}});
  }

  // The env var must be set before InitClient() creates the bootstrap.
  grpc_core::testing::ScopedExperimentalEnvVar env_var_{
      "GRPC_XDS_EXPERIMENTAL_ENABLE_AUTOSHARDING_LB"};
  std::unique_ptr<AutoshardingServerThread> autosharding_server_;
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsAutoshardingTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

// Autosharding picks both the locality and the endpoint.
TEST_P(XdsAutoshardingTest, TopLevel) {
  CreateAndStartBackends(2);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs(
      {{"locality0", {CreateNamedEndpoint(0), CreateNamedEndpoint(1)}}})));
  // Keys before "m" go to backend 0, and the rest go to backend 1.
  autosharding_server_->service().SetAssignment(
      {{"m", {0}}, {"", {1}}}, {BackendName(0), BackendName(1)},
      /*generation=*/1);
  SetClusterLbPolicy(MakeAutosharding(kAutoshardingTarget));
  CheckRpcSendOk(DEBUG_LOCATION, 1, KeyRpcOptions("a"));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1u);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 0u);
  CheckRpcSendOk(DEBUG_LOCATION, 1, KeyRpcOptions("z"));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1u);
  EXPECT_EQ(backends_[1]->backend_service()->request_count(), 1u);
  EXPECT_THAT(autosharding_server_->service().targets(),
              ::testing::ElementsAre(kAutoshardingTarget));
}

// Autosharding picks the endpoint under wrr_locality.  "%s" in the target
// is replaced with the locality.
TEST_P(XdsAutoshardingTest, UnderWrrLocality) {
  CreateAndStartBackends(1);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0", {CreateNamedEndpoint(0)}}})));
  autosharding_server_->service().SetAssignment({{"", {0}}}, {BackendName(0)},
                                                /*generation=*/1);
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(MakeAutosharding("shard-%s"));
  SetClusterLbPolicy(wrr_locality);
  CheckRpcSendOk(DEBUG_LOCATION, 1, KeyRpcOptions("a"));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1u);
  EXPECT_THAT(autosharding_server_->service().targets(),
              ::testing::ElementsAre(LocalityTarget("locality0")));
}

// While the policy waits for the first assignment, it reports CONNECTING,
// so the priority policy fails over to the next priority when its failover
// timer fires (items 6 and 7).  If the policy reported IDLE, the priority
// policy would cancel the timer and never fail over.
TEST_P(XdsAutoshardingTest, PriorityFailoverWhileWaitingForAssignment) {
  CreateAndStartBackends(2);
  ResetStub(/*failover_timeout_ms=*/500);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0",
       {CreateNamedEndpoint(0)},
       kDefaultLocalityWeight,
       /*priority=*/0},
      {"locality1",
       {CreateNamedEndpoint(1)},
       kDefaultLocalityWeight,
       /*priority=*/1},
  })));
  // The policy in priority 0 never gets an assignment.  The policy in
  // priority 1 does.
  autosharding_server_->service().SetSilentTarget(LocalityTarget("locality0"));
  autosharding_server_->service().SetAssignment({{"", {0}}}, {BackendName(1)},
                                                /*generation=*/1);
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(MakeAutosharding("shard-%s"));
  SetClusterLbPolicy(wrr_locality);
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), KeyRpcOptions("a"));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 0u);
}

// When a locality moves to another priority, a new autosharding policy is
// created.  It reuses the channel to the autosharding server.
TEST_P(XdsAutoshardingTest, RecreatedPolicyReusesChannel) {
  CreateAndStartBackends(2);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(
      EdsResourceArgs({{"locality0", {CreateNamedEndpoint(0)}}})));
  autosharding_server_->service().SetAssignment(
      {{"", {0, 1}}}, {BackendName(0), BackendName(1)}, /*generation=*/1);
  SetClusterLbPolicy(MakeAutosharding(kAutoshardingTarget));
  CheckRpcSendOk(DEBUG_LOCATION, 1, KeyRpcOptions("a"));
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1u);
  // From now on, hold any new connection attempt to the autosharding
  // server.  If the new policy created a new channel, its stream would
  // never start, and RPCs to backend 1 would fail.
  ConnectionAttemptInjector injector;
  auto hold = injector.AddHold(autosharding_server_->port());
  // Put a new locality in priority 0, and move locality0 to priority 1.
  // The CDS policy gives the new locality a new priority child, so a new
  // autosharding policy is created.
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality1",
       {CreateNamedEndpoint(1)},
       kDefaultLocalityWeight,
       /*priority=*/0},
      {"locality0",
       {CreateNamedEndpoint(0)},
       kDefaultLocalityWeight,
       /*priority=*/1},
  })));
  WaitForBackend(DEBUG_LOCATION, 1, /*check_status=*/nullptr,
                 WaitForBackendOptions(), KeyRpcOptions("a"));
  EXPECT_FALSE(hold->IsStarted());
  EXPECT_THAT(autosharding_server_->service().targets(),
              ::testing::ElementsAre(kAutoshardingTarget, kAutoshardingTarget));
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
  grpc_init();
  grpc::testing::ConnectionAttemptInjector::Init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
