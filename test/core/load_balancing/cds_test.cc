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

#include "src/core/load_balancing/xds/cds.h"

#include <grpc/channel_factory.h>
#include <grpc/impl/channel_arg_names.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/resolver/xds/xds_config.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/grpc/xds_cluster.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/grpc/xds_transport_factory_grpc.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

class CdsTest : public ::testing::Test {
 protected:
  static RefCountedPtr<XdsLocalityName> MakeLocality(std::string sub_zone) {
    return MakeRefCounted<XdsLocalityName>("foo", "bar", sub_zone);
  }
};

class CdsChildNameStateTest : public CdsTest {
 protected:
  static std::shared_ptr<const XdsEndpointResource> MakeEndpointResource(
      const std::vector<std::vector<std::string /*sub_zone*/>>& priorities) {
    auto endpoint_resource = std::make_shared<XdsEndpointResource>();
    for (size_t i = 0; i < priorities.size(); ++i) {
      XdsEndpointResource::Priority priority;
      const auto& localities = priorities[i];
      for (const auto& sub_zone : localities) {
        auto locality_name = MakeLocality(sub_zone);
        auto& locality = priority.localities[locality_name.get()];
        locality.name = std::move(locality_name);
      }
      endpoint_resource->priorities.push_back(std::move(priority));
    }
    return endpoint_resource;
  }

  static XdsConfig::ClusterConfig::EndpointConfig MakeEndpointConfig(
      const std::shared_ptr<const XdsEndpointResource>& endpoint_resource) {
    return XdsConfig::ClusterConfig::EndpointConfig(endpoint_resource,
                                                    /*resolution_note=*/"");
  }

  static XdsConfig::ClusterConfig::EndpointConfig MakeEndpointConfig(
      const std::vector<std::vector<std::string /*sub_zone*/>>& priorities) {
    return MakeEndpointConfig(MakeEndpointResource(priorities));
  }

  static XdsConfig::ClusterConfig MakeClusterConfig(
      const std::shared_ptr<const XdsEndpointResource>& endpoint_resource) {
    return XdsConfig::ClusterConfig(
        /*cluster=*/nullptr, endpoint_resource, /*resolution_note=*/"");
  }
};

TEST_F(CdsChildNameStateTest, InitialUpdate) {
  CdsChildNameState state;
  state.Update(nullptr, MakeEndpointConfig({{"aa", "bb"}, {"cc", "dd"}}));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 1));
  EXPECT_THAT(state.next_available_child_number(), 2);
}

TEST_F(CdsChildNameStateTest, UpdateNewChildNumber) {
  CdsChildNameState state;
  auto endpoint_resource = MakeEndpointResource({{"aa", "bb"}, {"cc", "dd"}});
  state.Update(nullptr, MakeEndpointConfig(endpoint_resource));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 1));
  EXPECT_THAT(state.next_available_child_number(), 2);
  auto old_cluster = MakeClusterConfig(endpoint_resource);
  state.Update(&old_cluster, MakeEndpointConfig({{"aa", "bb"}, {"ee"}}));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 2));
  EXPECT_THAT(state.next_available_child_number(), 3);
}

TEST_F(CdsChildNameStateTest, UpdateReusesChildNumbers) {
  CdsChildNameState state;
  auto endpoint_resource = MakeEndpointResource({{"aa", "bb"}, {"cc", "dd"}});
  state.Update(nullptr, MakeEndpointConfig(endpoint_resource));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 1));
  EXPECT_THAT(state.next_available_child_number(), 2);
  auto old_cluster = MakeClusterConfig(endpoint_resource);
  state.Update(&old_cluster, MakeEndpointConfig({{"cc"}, {"aa", "bb"}}));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(1, 0));
  EXPECT_THAT(state.next_available_child_number(), 2);
}

TEST_F(CdsChildNameStateTest, LocalityMovingBetweenPriorities) {
  CdsChildNameState state;
  // Initial update.
  auto endpoint_resource = MakeEndpointResource({{"aa", "bb"}, {"cc", "dd"}});
  state.Update(nullptr, MakeEndpointConfig(endpoint_resource));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 1));
  EXPECT_THAT(state.next_available_child_number(), 2);
  // Second update: move locality "cc" from p1 to p0.
  auto endpoint_resource2 = MakeEndpointResource({{"aa", "bb", "cc"}, {"dd"}});
  auto old_cluster = MakeClusterConfig(endpoint_resource);
  state.Update(&old_cluster, MakeEndpointConfig(endpoint_resource2));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 1));
  EXPECT_THAT(state.next_available_child_number(), 2);
  // Third update: move locality "cc" back to p1.
  old_cluster = MakeClusterConfig(endpoint_resource2);
  state.Update(&old_cluster, MakeEndpointConfig(endpoint_resource));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 1));
  EXPECT_THAT(state.next_available_child_number(), 2);
}

TEST_F(CdsChildNameStateTest, LocalityMovingBetweenPrioritiesSuboptimal) {
  CdsChildNameState state;
  // Initial update.
  auto endpoint_resource = MakeEndpointResource({{"bb", "cc"}, {"aa", "dd"}});
  state.Update(nullptr, MakeEndpointConfig(endpoint_resource));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(0, 1));
  EXPECT_THAT(state.next_available_child_number(), 2);
  // Second update: move locality "aa" from p1 to p0.
  auto endpoint_resource2 = MakeEndpointResource({{"aa", "bb", "cc"}, {"dd"}});
  auto old_cluster = MakeClusterConfig(endpoint_resource);
  state.Update(&old_cluster, MakeEndpointConfig(endpoint_resource2));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(1, 2));
  EXPECT_THAT(state.next_available_child_number(), 3);
  // Third update: move locality "aa" back to p1.
  old_cluster = MakeClusterConfig(endpoint_resource2);
  state.Update(&old_cluster, MakeEndpointConfig(endpoint_resource));
  EXPECT_THAT(state.priority_child_numbers(), ::testing::ElementsAre(1, 2));
  EXPECT_THAT(state.next_available_child_number(), 3);
}

using CdsPriorityEndpointIteratorTest = CdsTest;

TEST_F(CdsPriorityEndpointIteratorTest, NormalizedWeights) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_PF_WEIGHTED_SHUFFLING");
  constexpr std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445"};
  auto endpoint_resource = std::make_shared<XdsEndpointResource>();
  auto& priority = endpoint_resource->priorities.emplace_back();
  auto locality_name1 = MakeLocality("subzone1");
  auto& locality1 = priority.localities[locality_name1.get()];
  locality1.name = locality_name1;
  locality1.lb_weight = 10;
  locality1.endpoints.push_back(LoadBalancingPolicyTest::MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_WEIGHT, 1)));
  locality1.endpoints.push_back(LoadBalancingPolicyTest::MakeEndpointAddresses(
      {kAddresses[1]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_WEIGHT, 2)));
  auto locality_name2 = MakeLocality("subzone2");
  auto& locality2 = priority.localities[locality_name2.get()];
  locality2.name = locality_name2;
  locality2.lb_weight = 10;
  locality2.endpoints.push_back(LoadBalancingPolicyTest::MakeEndpointAddresses(
      {kAddresses[2]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_WEIGHT, 3)));
  std::vector<size_t> priority_child_numbers = {0};
  CdsPriorityEndpointIterator iterator(
      RefCountedStringValue("cluster_foo"),
      /*use_http_connect=*/true, endpoint_resource, priority_child_numbers);
  std::vector<std::pair<std::string /*address*/, uint32_t /*weight*/>>
      resolved_endpoints;
  iterator.ForEach([&](const EndpointAddresses& endpoint) {
    resolved_endpoints.emplace_back(
        grpc_sockaddr_to_uri(&endpoint.address()).value(),
        static_cast<uint32_t>(
            endpoint.args().GetInt(GRPC_ARG_ADDRESS_WEIGHT).value_or(0)));
  });
  EXPECT_THAT(
      resolved_endpoints,
      ::testing::ElementsAre(::testing::Pair(kAddresses[0], 357913941),
                             ::testing::Pair(kAddresses[1], 715827882),
                             ::testing::Pair(kAddresses[2], 1073741824)));
}

TEST_F(CdsPriorityEndpointIteratorTest, OldWeightsWhenDisabled) {
  auto endpoint_resource = std::make_shared<XdsEndpointResource>();
  auto& priority = endpoint_resource->priorities.emplace_back();
  auto locality_name1 = MakeLocality("subzone1");
  auto& locality1 = priority.localities[locality_name1.get()];
  locality1.name = locality_name1;
  locality1.lb_weight = 10;
  locality1.endpoints.push_back(LoadBalancingPolicyTest::MakeEndpointAddresses(
      {"ipv4:127.0.0.1:443"}, ChannelArgs().Set(GRPC_ARG_ADDRESS_WEIGHT, 100)));
  std::vector<size_t> priority_child_numbers = {0};
  CdsPriorityEndpointIterator iterator(
      RefCountedStringValue("cluster_foo"),
      /*use_http_connect=*/true, endpoint_resource, priority_child_numbers);
  std::vector<std::pair<std::string, uint32_t>> resolved_endpoints;
  iterator.ForEach([&](const EndpointAddresses& endpoint) {
    resolved_endpoints.emplace_back(
        grpc_sockaddr_to_uri(&endpoint.address()).value(),
        static_cast<uint32_t>(
            endpoint.args().GetInt(GRPC_ARG_ADDRESS_WEIGHT).value_or(0)));
  });
  EXPECT_THAT(resolved_endpoints, ::testing::ElementsAre(::testing::Pair(
                                      "ipv4:127.0.0.1:443", 1000)));
}

TEST_F(CdsPriorityEndpointIteratorTest, InvalidWeightsClamped) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_PF_WEIGHTED_SHUFFLING");
  auto endpoint_resource = std::make_shared<XdsEndpointResource>();
  auto& priority = endpoint_resource->priorities.emplace_back();
  auto locality_name1 = MakeLocality("subzone1");
  auto& locality1 = priority.localities[locality_name1.get()];
  locality1.name = locality_name1;
  locality1.lb_weight = 10;
  locality1.endpoints.push_back(LoadBalancingPolicyTest::MakeEndpointAddresses(
      {"ipv4:127.0.0.1:443"}, ChannelArgs().Set(GRPC_ARG_ADDRESS_WEIGHT, 0)));
  locality1.endpoints.push_back(LoadBalancingPolicyTest::MakeEndpointAddresses(
      {"ipv4:127.0.0.1:444"}, ChannelArgs().Set(GRPC_ARG_ADDRESS_WEIGHT, -5)));
  auto locality_name2 = MakeLocality("subzone2");
  auto& locality2 = priority.localities[locality_name2.get()];
  locality2.name = locality_name2;
  locality2.lb_weight = 10;
  locality2.endpoints.push_back(LoadBalancingPolicyTest::MakeEndpointAddresses(
      {"ipv4:127.0.0.1:445"}, ChannelArgs().Set(GRPC_ARG_ADDRESS_WEIGHT, 1)));
  std::vector<size_t> priority_child_numbers = {0};
  CdsPriorityEndpointIterator iterator(
      RefCountedStringValue("cluster_foo"),
      /*use_http_connect=*/true, endpoint_resource, priority_child_numbers);
  std::vector<std::pair<std::string, uint32_t>> resolved_endpoints;
  iterator.ForEach([&](const EndpointAddresses& endpoint) {
    resolved_endpoints.emplace_back(
        grpc_sockaddr_to_uri(&endpoint.address()).value(),
        static_cast<uint32_t>(
            endpoint.args().GetInt(GRPC_ARG_ADDRESS_WEIGHT).value_or(0)));
  });
  EXPECT_THAT(resolved_endpoints,
              ::testing::ElementsAre(
                  ::testing::Pair("ipv4:127.0.0.1:443", 536870912),
                  ::testing::Pair("ipv4:127.0.0.1:444", 536870912),
                  ::testing::Pair("ipv4:127.0.0.1:445", 1073741824)));
}

//
// Channel factory injection (gRFC A119)
//

// The channel factory that the recorder LB policy saw in its last update.
// The recorder runs in the WorkSerializer, which runs the test's updates
// synchronously, so no lock is needed.
std::shared_ptr<experimental::ChannelFactory>* g_recorded_channel_factory =
    new std::shared_ptr<experimental::ChannelFactory>();

constexpr absl::string_view kRecorderLbPolicyName =
    "channel_factory_recorder_test";

// An endpoint picking policy that records the channel factory in the
// channel args of each update.
class ChannelFactoryRecorderLb final : public LoadBalancingPolicy {
 public:
  explicit ChannelFactoryRecorderLb(Args args)
      : LoadBalancingPolicy(std::move(args)) {}

  absl::string_view name() const override { return kRecorderLbPolicyName; }

  absl::Status UpdateLocked(UpdateArgs args) override {
    *g_recorded_channel_factory =
        args.args.GetObjectRef<experimental::ChannelFactory>();
    return absl::OkStatus();
  }

  void ResetBackoffLocked() override {}

 private:
  void ShutdownLocked() override {}
};

class ChannelFactoryRecorderLbConfig final
    : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kRecorderLbPolicyName; }
};

class ChannelFactoryRecorderLbFactory final
    : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<ChannelFactoryRecorderLb>(std::move(args));
  }

  absl::string_view name() const override { return kRecorderLbPolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<ChannelFactoryRecorderLbConfig>();
  }
};

constexpr absl::string_view kAutoshardingServiceUri =
    "dns:///autosharding.example.com";
constexpr absl::string_view kChannelFactoryKey = "channel_factory_key";

class CdsChannelFactoryTest : public LoadBalancingPolicyTest {
 protected:
  CdsChannelFactoryTest() : LoadBalancingPolicyTest("cds_experimental") {}

  void SetUp() override {
    // The outlier_detection child policy needs a collection scope.
    stats_plugin_group_.Finish();
    LoadBalancingPolicyTest::SetUp();
    auto xds_client = GrpcXdsClient::GetOrCreate(
        "test",
        ChannelArgs().Set(
            GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG,
            "{\"xds_servers\": [{\"server_uri\": \"xds.example.com\", "
            "\"channel_creds\": [{\"type\": \"insecure\"}]}]}"),
        "test");
    ASSERT_TRUE(xds_client.ok()) << xds_client.status();
    xds_client_ = std::move(*xds_client);
  }

  void TearDown() override {
    g_recorded_channel_factory->reset();
    xds_client_.reset();
    LoadBalancingPolicyTest::TearDown();
  }

  // Returns a GrpcService map with one entry for kChannelFactoryKey.
  XdsGrpcServiceMap MakeGrpcServices(absl::string_view server_uri) {
    ValidationErrors errors;
    auto channel_creds_config = ParseXdsBootstrapChannelCreds(
        Json::FromObject({{"channel_creds",
                           Json::FromArray({Json::FromObject(
                               {{"type", Json::FromString("insecure")}})})}}),
        JsonArgs(), &errors);
    EXPECT_TRUE(errors.ok()) << errors.message("channel creds");
    XdsGrpcServiceMap grpc_services;
    grpc_services[std::string(kChannelFactoryKey)] =
        std::make_shared<GrpcXdsServerTarget>(
            std::string(server_uri), std::move(channel_creds_config),
            std::vector<RefCountedPtr<const CallCredsConfig>>());
    return grpc_services;
  }

  // Returns an EDS cluster that uses the recorder policy for endpoint
  // picking.  max_concurrent_requests lets a test change the cluster
  // without changing the GrpcServices.
  static std::shared_ptr<XdsClusterResource> MakeLeafCluster(
      XdsGrpcServiceMap grpc_services,
      uint32_t max_concurrent_requests = 1024) {
    auto cluster = std::make_shared<XdsClusterResource>();
    cluster->type = XdsClusterResource::Eds();
    cluster->lb_policy_config = {Json::FromObject(
        {{std::string(kRecorderLbPolicyName), Json::FromObject({})}})};
    cluster->lb_policy_grpc_services = std::move(grpc_services);
    cluster->max_concurrent_requests = max_concurrent_requests;
    return cluster;
  }

  // Returns an endpoint resource with one priority, one locality and one
  // endpoint.
  static std::shared_ptr<const XdsEndpointResource> MakeEndpoints() {
    auto endpoint_resource = std::make_shared<XdsEndpointResource>();
    auto& priority = endpoint_resource->priorities.emplace_back();
    auto locality_name = MakeRefCounted<XdsLocalityName>("foo", "bar", "baz");
    auto& locality = priority.localities[locality_name.get()];
    locality.name = std::move(locality_name);
    locality.lb_weight = 1;
    locality.endpoints.push_back(
        MakeEndpointAddresses({"ipv4:127.0.0.1:443"}, ChannelArgs()));
    return endpoint_resource;
  }

  // Sends an update for cluster_name with xds_config to the CDS policy.
  absl::Status ApplyCdsUpdate(absl::string_view cluster_name,
                              RefCountedPtr<const XdsConfig> xds_config) {
    LoadBalancingPolicy::UpdateArgs update;
    update.config = MakeConfig(Json::FromArray({Json::FromObject(
        {{"cds_experimental",
          Json::FromObject(
              {{"cluster", Json::FromString(std::string(cluster_name))}})}})}));
    update.args =
        ChannelArgs().SetObject(xds_client_).SetObject(std::move(xds_config));
    return ApplyUpdate(std::move(update), lb_policy());
  }

  // Sends an update with a leaf cluster "foo" to the CDS policy.
  absl::Status ApplyLeafClusterUpdate(
      std::shared_ptr<XdsClusterResource> cluster) {
    auto xds_config = MakeRefCounted<XdsConfig>();
    xds_config->clusters["foo"].emplace(std::move(cluster), MakeEndpoints(),
                                        "");
    return ApplyCdsUpdate("foo", std::move(xds_config));
  }

  // Expects that each state update that the CDS policy reported is
  // CONNECTING.  The recorder child never reports a state.
  void ExpectOnlyConnectingUpdates() {
    while (!helper_->QueueEmpty()) ExpectState(GRPC_CHANNEL_CONNECTING);
  }

  RefCountedPtr<GrpcXdsClient> xds_client_;
};

// The factory's channels are tested in xds_transport_grpc_test and
// xds_client_grpc_test.  This test does not create channels, because real
// channels do not work with the FuzzingEventEngine.
TEST_F(CdsChannelFactoryTest, ChildGetsChannelFactory) {
  ASSERT_TRUE(ApplyLeafClusterUpdate(
                  MakeLeafCluster(MakeGrpcServices(kAutoshardingServiceUri)))
                  .ok());
  ExpectOnlyConnectingUpdates();
  ASSERT_NE(*g_recorded_channel_factory, nullptr);
  EXPECT_NE(dynamic_cast<XdsTransportFactoryWrapper*>(
                g_recorded_channel_factory->get()),
            nullptr);
}

TEST_F(CdsChannelFactoryTest, SameGrpcServicesReuseChannelFactory) {
  ASSERT_TRUE(ApplyLeafClusterUpdate(
                  MakeLeafCluster(MakeGrpcServices(kAutoshardingServiceUri)))
                  .ok());
  ExpectOnlyConnectingUpdates();
  std::shared_ptr<experimental::ChannelFactory> channel_factory =
      *g_recorded_channel_factory;
  ASSERT_NE(channel_factory, nullptr);
  // New cluster resource with new but equal GrpcService objects, and a
  // different max_concurrent_requests so that CDS processes the update.
  ASSERT_TRUE(ApplyLeafClusterUpdate(
                  MakeLeafCluster(MakeGrpcServices(kAutoshardingServiceUri),
                                  /*max_concurrent_requests=*/10))
                  .ok());
  ExpectOnlyConnectingUpdates();
  EXPECT_EQ(*g_recorded_channel_factory, channel_factory);
}

TEST_F(CdsChannelFactoryTest, ChangedGrpcServicesCreateNewChannelFactory) {
  ASSERT_TRUE(ApplyLeafClusterUpdate(
                  MakeLeafCluster(MakeGrpcServices(kAutoshardingServiceUri)))
                  .ok());
  ExpectOnlyConnectingUpdates();
  std::shared_ptr<experimental::ChannelFactory> channel_factory =
      *g_recorded_channel_factory;
  ASSERT_NE(channel_factory, nullptr);
  ASSERT_TRUE(
      ApplyLeafClusterUpdate(MakeLeafCluster(MakeGrpcServices(
                                 "dns:///other-autosharding.example.com")))
          .ok());
  ExpectOnlyConnectingUpdates();
  ASSERT_NE(*g_recorded_channel_factory, nullptr);
  EXPECT_NE(*g_recorded_channel_factory, channel_factory);
}

TEST_F(CdsChannelFactoryTest, NoGrpcServicesNoChannelFactory) {
  ASSERT_TRUE(ApplyLeafClusterUpdate(MakeLeafCluster({})).ok());
  ExpectOnlyConnectingUpdates();
  EXPECT_EQ(*g_recorded_channel_factory, nullptr);
}

TEST_F(CdsChannelFactoryTest, AggregateClusterDoesNotCreateChannelFactory) {
  // The aggregate cluster has GrpcServices, but the leaf cluster does not.
  // Only a leaf cluster's CDS policy may inject a channel factory.
  auto aggregate_cluster = std::make_shared<XdsClusterResource>();
  aggregate_cluster->type = XdsClusterResource::Aggregate{{"leaf"}};
  aggregate_cluster->lb_policy_grpc_services =
      MakeGrpcServices(kAutoshardingServiceUri);
  auto xds_config = MakeRefCounted<XdsConfig>();
  xds_config->clusters["aggregate"].emplace(
      std::move(aggregate_cluster), std::vector<absl::string_view>{"leaf"}, "");
  xds_config->clusters["leaf"].emplace(MakeLeafCluster({}), MakeEndpoints(),
                                       "");
  ASSERT_TRUE(ApplyCdsUpdate("aggregate", std::move(xds_config)).ok());
  ExpectOnlyConnectingUpdates();
  EXPECT_EQ(*g_recorded_channel_factory, nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::CoreConfiguration::RegisterEphemeralBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
            std::make_unique<
                grpc_core::testing::ChannelFactoryRecorderLbFactory>());
      });
  return RUN_ALL_TESTS();
}
