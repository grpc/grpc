//
// Copyright 2025 gRPC authors.
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

#include <grpc/grpc.h>

#include <array>
#include <string>

#include "src/core/client_channel/client_channel_service_config.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

using internal::ClientChannelGlobalParsedConfig;
using internal::ClientChannelServiceConfigParser;

class RandomSubsettingTest : public LoadBalancingPolicyTest {
 protected:
  RandomSubsettingTest() : LoadBalancingPolicyTest("random_subsetting") {}

  static std::string MakeRandomSubsettingServiceConfig(
      uint32_t subset_size = 3,
      absl::string_view child_policy = "round_robin") {
    return absl::StrFormat(
        "{\n"
        "  \"loadBalancingConfig\":[{\n"
        "    \"random_subsetting\":{\n"
        "      \"subset_size\": %d,\n"
        "      \"childPolicy\": [{\"%s\": {}}]\n"
        "    }\n"
        "  }]\n"
        "}\n",
        subset_size, child_policy);
  }
};

TEST_F(RandomSubsettingTest, BasicConfig) {
  auto service_config = ServiceConfigImpl::Create(
      ChannelArgs(), MakeRandomSubsettingServiceConfig());
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  ASSERT_NE(*service_config, nullptr);
}

TEST_F(RandomSubsettingTest, SubsetSizeLargerThanEndpoints) {
  auto service_config = ServiceConfigImpl::Create(
      ChannelArgs(), MakeRandomSubsettingServiceConfig(42));
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  ASSERT_NE(*service_config, nullptr);

  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);
}

TEST_F(RandomSubsettingTest, ZeroSubsetSize) {
  auto service_config = ServiceConfigImpl::Create(
      ChannelArgs(), MakeRandomSubsettingServiceConfig(0));
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(service_config.status().message(),
              ::testing::HasSubstr("must be greater than 0"));
}

TEST_F(RandomSubsettingTest, MissingSubsetSize) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"random_subsetting\":{\n"
      "      \"childPolicy\": [{\"round_robin\": {}}]\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr("field:subset_size error:field not present"));
}

TEST_F(RandomSubsettingTest, MissingChildPolicy) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"random_subsetting\":{\n"
      "      \"subset_size\": 3\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      service_config.status().message(),
      ::testing::HasSubstr("field:childPolicy error:field not present"));
}

TEST_F(RandomSubsettingTest, ChildPolicyNotArray) {
  const char* service_config_json =
      "{\n"
      "  \"loadBalancingConfig\":[{\n"
      "    \"random_subsetting\":{\n"
      "      \"subset_size\": 3,\n"
      "      \"childPolicy\": {\"round_robin\": {}}\n"
      "    }\n"
      "  }]\n"
      "}\n";
  auto service_config =
      ServiceConfigImpl::Create(ChannelArgs(), service_config_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(service_config.status().message(),
              ::testing::HasSubstr("is not an array"));
}

TEST_F(RandomSubsettingTest, EmptyAddressList) {
  auto service_config = ServiceConfigImpl::Create(
      ChannelArgs(), MakeRandomSubsettingServiceConfig());
  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  auto lb_config = global_config->parsed_lb_config();

  const std::array<absl::string_view, 0> kEmptyAddresses = {};
  auto status =
      ApplyUpdate(BuildUpdate(kEmptyAddresses, lb_config), lb_policy());
  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
  EXPECT_THAT(status.message(), ::testing::HasSubstr("empty address list"));

  // Should report TRANSIENT_FAILURE with the error status
  auto picker = ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                            absl::UnavailableError("empty address list"));
  ASSERT_NE(picker, nullptr);
}

TEST_F(RandomSubsettingTest, FiltersEndpointsCorrectly) {
  constexpr uint32_t kSubsetSize = 3;
  const std::array<absl::string_view, 5> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445"};

  auto service_config = ServiceConfigImpl::Create(
      ChannelArgs(), MakeRandomSubsettingServiceConfig(kSubsetSize));
  ASSERT_TRUE(service_config.ok()) << service_config.status();

  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);

  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, lb_config), lb_policy()),
            absl::OkStatus());

  // Verify only 3 subchannels are created (subset_size)
  int subchannel_count = 0;
  for (const auto& address : kAddresses) {
    if (FindSubchannel(address) != nullptr) {
      subchannel_count++;
    }
  }
  EXPECT_EQ(subchannel_count, kSubsetSize);
}

TEST_F(RandomSubsettingTest, ConnectivityStateTransitions) {
  constexpr uint32_t kSubsetSize = 3;
  const std::array<absl::string_view, 5> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445"};

  auto service_config = ServiceConfigImpl::Create(
      ChannelArgs(),
      MakeRandomSubsettingServiceConfig(kSubsetSize, "pick_first"));
  ASSERT_TRUE(service_config.ok()) << service_config.status();

  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  ASSERT_NE(global_config, nullptr);
  auto lb_config = global_config->parsed_lb_config();
  ASSERT_NE(lb_config, nullptr);

  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, lb_config), lb_policy()),
            absl::OkStatus());

  // Find which subchannel the child policy is trying to connect to
  SubchannelState* connecting_subchannel = nullptr;
  for (const auto& address : kAddresses) {
    auto* subchannel = FindSubchannel(address);
    if (subchannel != nullptr && subchannel->ConnectionRequested()) {
      connecting_subchannel = subchannel;
      break;
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);

  // Subchannel reports CONNECTING
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ASSERT_NE(picker, nullptr);

  // Subchannel reports READY
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = WaitForConnected();
  ASSERT_NE(picker, nullptr);
}

TEST_F(RandomSubsettingTest, MinimizesChurnOnAddressUpdate) {
  constexpr uint32_t kInitialServers = 5;
  constexpr uint32_t kSubsetSize = 3;
  constexpr uint32_t kMinUnchanged = 2;
  const std::array<absl::string_view, kInitialServers> kInitialAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445"};

  auto service_config = ServiceConfigImpl::Create(
      ChannelArgs(), MakeRandomSubsettingServiceConfig(kSubsetSize));
  auto global_config = DownCast<ClientChannelGlobalParsedConfig*>(
      (*service_config)
          ->GetGlobalParsedConfig(
              ClientChannelServiceConfigParser::ParserIndex()));
  auto lb_config = global_config->parsed_lb_config();

  EXPECT_EQ(ApplyUpdate(BuildUpdate(kInitialAddresses, lb_config), lb_policy()),
            absl::OkStatus());

  // Track which subchannels were selected initially
  std::set<std::string> initial_subchannels;
  for (const auto& address : kInitialAddresses) {
    if (FindSubchannel(address) != nullptr) {
      initial_subchannels.insert(std::string(address));
    }
  }
  EXPECT_EQ(initial_subchannels.size(), kSubsetSize);

  // Add one new endpoint
  const std::array<absl::string_view, 6> kUpdatedAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};

  EXPECT_EQ(ApplyUpdate(BuildUpdate(kUpdatedAddresses, lb_config), lb_policy()),
            absl::OkStatus());

  // Count how many subchannels remained the same
  int unchanged_count = 0;
  for (const auto& address : kUpdatedAddresses) {
    if (FindSubchannel(address) != nullptr &&
        initial_subchannels.count(std::string(address)) > 0) {
      unchanged_count++;
    }
  }
  // With rendezvous hashing, most subchannels should remain unchanged
  EXPECT_GE(unchanged_count, kMinUnchanged)
      << "Rendezvous hashing should minimize churn";
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
