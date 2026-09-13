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

#include <grpc/grpc.h>

#include <array>
#include <map>
#include <string>
#include <utility>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

class AutoShardingTest : public LoadBalancingPolicyTest {
 protected:
  static constexpr absl::string_view kKeyHeaderName = "x-slice-key";
  static constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"};

  class ConfigBuilder {
   public:
    ConfigBuilder() {
      SetChannelFactoryKey("sharding_service");
      SetAutoshardingTarget("my_autosharding_target");
      SetKeyHeaderName(std::string(kKeyHeaderName));
      SetEnableFallback(false);
      SetInitialAssignmentTimeout(Duration::Seconds(1));
    }

    ConfigBuilder& SetChannelFactoryKey(std::string channel_factory_key) {
      json_["channelFactoryKey"] =
          Json::FromString(std::move(channel_factory_key));
      return *this;
    }
    ConfigBuilder& SetAutoshardingTarget(std::string autosharding_target) {
      json_["autoshardingTarget"] =
          Json::FromString(std::move(autosharding_target));
      return *this;
    }
    ConfigBuilder& SetKeyHeaderName(std::string key_header_name) {
      json_["keyHeaderName"] = Json::FromString(std::move(key_header_name));
      return *this;
    }
    ConfigBuilder& SetEnableFallback(bool enable_fallback) {
      json_["enableFallback"] = Json::FromBool(enable_fallback);
      return *this;
    }
    ConfigBuilder& SetInitialAssignmentTimeout(Duration duration) {
      json_["initialAssignmentTimeout"] =
          Json::FromString(duration.ToJsonString());
      return *this;
    }

    RefCountedPtr<LoadBalancingPolicy::Config> Build() {
      Json config = Json::FromArray({Json::FromObject(
          {{"autosharding_experimental", Json::FromObject(json_)}})});
      return MakeConfig(config);
    }

   private:
    Json::Object json_;
  };

  static std::map<std::string, std::string> MakeMetadata(
      std::string key = "some_key") {
    return {{std::string(kKeyHeaderName), std::move(key)}};
  }

  AutoShardingTest() : LoadBalancingPolicyTest("autosharding_experimental") {}
};

TEST_F(AutoShardingTest, StartupFallback) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The pick should trigger a connection attempt on exactly one endpoint.
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    subchannel = FindSubchannel(address);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // No other subchannels should have been created.
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr) EXPECT_EQ(sc, subchannel);
  }
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata());
  EXPECT_THAT(address, ::testing::AnyOf(kAddresses[0], kAddresses[1]));
}

TEST_F(AutoShardingTest, FallbackDisabledFailsPicks) {
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, ConfigBuilder().Build()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError("no endpoint available"));
      },
      /*call_attributes=*/{}, MakeMetadata());
}

TEST_F(AutoShardingTest, ResolutionNotePropagatedOnFallbackDisabled) {
  auto update = BuildUpdate(kAddresses, ConfigBuilder().Build());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()), absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(
      picker.get(),
      [](const absl::Status& status) {
        EXPECT_EQ(status, absl::UnavailableError(
                              "no endpoint available (DNS resolution note)"));
      },
      /*call_attributes=*/{}, MakeMetadata());
}

TEST_F(AutoShardingTest, MissingKeyHeaderFailsPick) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(picker.get(), [](const absl::Status& status) {
    EXPECT_EQ(status, absl::InternalError(
                          "slice key header \"x-slice-key\" not present"));
  });
}

TEST_F(AutoShardingTest, EmptyEndpointList) {
  const std::vector<absl::string_view> kNoAddresses;
  auto update = BuildUpdate(kNoAddresses, ConfigBuilder().Build());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()),
            absl::UnavailableError("empty address list: DNS resolution note"));
  ExpectTransientFailureUpdate(
      absl::UnavailableError("empty address list: DNS resolution note"));
}

TEST_F(AutoShardingTest, RetainsEndpointForHostnameAcrossUpdates) {
  // First update: a single endpoint with hostname "host1". Drive it to READY.
  const std::vector<EndpointAddresses> first_update = {MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(first_update,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  // Second update: retains endpoint for "host1".
  const std::vector<EndpointAddresses> second_update = {MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(second_update,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  // Reconfiguring the reused endpoint's pick_first child makes it re-register
  // its subchannel watchers, which triggers a second READY notification on top
  // of the policy's own unconditional post-update picker refresh; both report
  // READY without an intervening CONNECTING, confirming the endpoint was never
  // actually torn down and reconnected.
  ExpectState(GRPC_CHANNEL_READY);
  ExpectState(GRPC_CHANNEL_READY);
}

TEST_F(AutoShardingTest, EmptyKeyHeaderValueIsValidKey) {
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata(""));
}

TEST_F(AutoShardingTest, SameAddressListedMultipleTimes) {
  const std::array<absl::string_view, 3> kAddressesWithDup = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:441"};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddressesWithDup,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  const auto metadata = MakeMetadata("ipv4:127.0.0.1:441");
  ExpectPickQueued(picker.get(), {}, metadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address :
       {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}) {
    subchannel = FindSubchannel(address);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, metadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest, MultipleAddressesPerEndpoint) {
  constexpr std::array<absl::string_view, 2> kEndpoint1Addresses = {
      "ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444"};
  constexpr std::array<absl::string_view, 2> kEndpoint2Addresses = {
      "ipv4:127.0.0.1:445", "ipv4:127.0.0.1:446"};
  const std::array<EndpointAddresses, 2> kEndpoints = {
      MakeEndpointAddresses(kEndpoint1Addresses),
      MakeEndpointAddresses(kEndpoint2Addresses)};
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kEndpoints,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  const auto metadata = MakeMetadata("ipv4:127.0.0.1:443");
  ExpectPickQueued(picker.get(), {}, metadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address :
       {"ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
        "ipv4:127.0.0.1:446"}) {
    subchannel = FindSubchannel(address);
    if (subchannel != nullptr) break;
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {}, metadata);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, metadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest,
       TriggersConnectionAttemptsInConnectingAndTransientFailureWithoutPicks) {
  const std::array<absl::string_view, 4> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443",
      "ipv4:127.0.0.1:444"};
  std::array<SubchannelState*, 4> subchannels;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    subchannels[i] = CreateSubchannel(kAddresses[i]);
  }
  absl::flat_hash_set<SubchannelState*> failed_subchannels;
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              ConfigBuilder().SetEnableFallback(true).Build()),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, MakeMetadata());
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel0 = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(subchannel0, nullptr) << "index " << i;
      subchannel0 = subchannels[i];
    }
  }
  ASSERT_NE(subchannel0, nullptr);
  subchannel0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i] != subchannel0) {
      EXPECT_FALSE(subchannels[i]->ConnectionRequested()) << "index " << i;
    }
  }
  subchannel0->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(subchannel0);
  ExpectReresolutionRequest();
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  SubchannelState* connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  ASSERT_FALSE(failed_subchannels.contains(connecting_subchannel));
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  connecting_subchannel = nullptr;
  for (size_t i = 0; i < subchannels.size(); ++i) {
    if (subchannels[i]->ConnectionRequested()) {
      ASSERT_EQ(connecting_subchannel, nullptr) << "index " << i;
      connecting_subchannel = subchannels[i];
    }
  }
  ASSERT_NE(connecting_subchannel, nullptr);
  ASSERT_FALSE(failed_subchannels.contains(connecting_subchannel));
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  for (size_t i = 0; i < subchannels.size(); ++i) {
    EXPECT_FALSE(subchannels[i]->ConnectionRequested());
  }
  connecting_subchannel->SetConnectivityState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("connection attempt failed"));
  failed_subchannels.insert(connecting_subchannel);
  ExpectReresolutionRequest();
  picker = ExpectState(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      absl::UnavailableError("no reachable endpoints; last error: "
                             "UNAVAILABLE: connection attempt failed"));
  subchannel0->SetConnectivityState(GRPC_CHANNEL_IDLE);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  EXPECT_TRUE(subchannel0->ConnectionRequested());
  subchannel0->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  subchannel0->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, MakeMetadata());
  ASSERT_TRUE(address.has_value());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
