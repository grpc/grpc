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

#include <vector>

#include "src/core/resolver/xds/xds_config.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
