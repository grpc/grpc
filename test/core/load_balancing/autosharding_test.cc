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
#include <stdint.h>

#include <array>
#include <chrono>
#include <map>
#include <string>

#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace testing {
namespace {

class AutoShardingTest : public LoadBalancingPolicyTest {
 protected:
  AutoShardingTest() : LoadBalancingPolicyTest("autosharding_experimental") {}

  // Builds an autosharding config with the given field values.  An empty
  // initial_assignment_timeout means the field is omitted (default of 60s).
  static RefCountedPtr<LoadBalancingPolicy::Config> MakeAutoShardingConfig(
      const std::string& key_header_name_name = "x-slice-key",
      bool enable_fallback = true,
      const std::string& initial_assignment_timeout = "1s",
      const std::string& channel_factory_key = "sharding_service",
      const std::string& autosharding_target = "my_autosharding_target") {
    Json::Object fields;
    fields["channelFactoryKey"] = Json::FromString(channel_factory_key);
    fields["autoshardingTarget"] = Json::FromString(autosharding_target);
    fields["keyHeaderName"] = Json::FromString(key_header_name_name);
    fields["enableFallback"] = Json::FromBool(enable_fallback);
    if (!initial_assignment_timeout.empty()) {
      fields["initialAssignmentTimeout"] =
          Json::FromString(initial_assignment_timeout);
    }
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"autosharding_experimental", Json::FromObject(fields)}})}));
  }

  // Applies an update with the given addresses and config, and expects the
  // policy to start in IDLE state with a picker that queues picks while it
  // waits for the initial assignment from the sharding service.
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> ApplyUpdateAndExpectIdle(
      absl::Span<const absl::string_view> addresses,
      RefCountedPtr<LoadBalancingPolicy::Config> config) {
    EXPECT_EQ(
        ApplyUpdate(BuildUpdate(addresses, std::move(config)), lb_policy()),
        absl::OkStatus());
    auto picker = ExpectState(GRPC_CHANNEL_IDLE);
    // While waiting for the initial assignment, picks should be queued, and
    // no connections should be attempted (child policies are created
    // lazily).
    ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
    for (absl::string_view address : addresses) {
      EXPECT_EQ(FindSubchannel(address), nullptr);
    }
    return picker;
  }

  // The metadata used for picks.  The value is arbitrary, since the policy
  // will not have received any assignment from the sharding service in these
  // tests, so all keys will fall into the fallback pool.
  static const std::map<std::string, std::string> kSliceKeyMetadata;

  static constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"};
};

const std::map<std::string, std::string> AutoShardingTest::kSliceKeyMetadata = {
    {"x-slice-key", "some_key"}};

TEST_F(AutoShardingTest, StartupFallbackWhenTimerExpires) {
  SetExpectedTimerDuration(std::chrono::seconds(1));
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  // The policy should report a new picker that uses the fallback pool.
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The pick should trigger a connection attempt on exactly one endpoint.
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr) {
      ASSERT_EQ(subchannel, nullptr);
      subchannel = sc;
    }
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
  auto address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  EXPECT_THAT(address, ::testing::AnyOf(kAddresses[0], kAddresses[1]));
}

TEST_F(AutoShardingTest, FallbackDisabledFailsPicksAfterTimeout) {
  auto picker = ApplyUpdateAndExpectIdle(
      kAddresses, MakeAutoShardingConfig(/*key_header_name_name=*/"x-slice-key",
                                         /*enable_fallback=*/false));
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  // Note: Can't use ExpectPickFail() here, because it does not pass the
  // slice key metadata that the picker needs in order to get past the key
  // extraction step.
  auto pick_result = DoPick(picker.get(), {}, kSliceKeyMetadata);
  auto* fail =
      std::get_if<LoadBalancingPolicy::PickResult::Fail>(&pick_result.result);
  ASSERT_NE(fail, nullptr);
  EXPECT_EQ(fail->status, absl::UnavailableError("no endpoint available"));
}

TEST_F(AutoShardingTest, ResolutionNotePropagatedOnFallbackDisabled) {
  auto update = BuildUpdate(
      kAddresses, MakeAutoShardingConfig(/*key_header_name_name=*/"x-slice-key",
                                         /*enable_fallback=*/false));
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()), absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto pick_result = DoPick(picker.get(), {}, kSliceKeyMetadata);
  auto* fail =
      std::get_if<LoadBalancingPolicy::PickResult::Fail>(&pick_result.result);
  ASSERT_NE(fail, nullptr);
  EXPECT_EQ(fail->status, absl::UnavailableError(
                              "no endpoint available (DNS resolution note)"));
}

TEST_F(AutoShardingTest, MissingKeyHeaderFailsPick) {
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(picker.get(), [&](const absl::Status& status) {
    EXPECT_EQ(status, absl::InternalError(
                          "slice key header \"x-slice-key\" not present"));
  });
}

TEST_F(AutoShardingTest, EmptyEndpointList) {
  const std::vector<absl::string_view> kNoAddresses;
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kNoAddresses, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::UnavailableError("empty address list: "));
  ExpectTransientFailureUpdate(absl::UnavailableError("empty address list: "));
}

TEST_F(AutoShardingTest, ResolutionNotePropagatedOnEmptyEndpointList) {
  const std::vector<absl::string_view> kNoAddresses;
  auto update = BuildUpdate(kNoAddresses, MakeAutoShardingConfig());
  update.resolution_note = "DNS resolution note";
  EXPECT_EQ(ApplyUpdate(std::move(update), lb_policy()),
            absl::UnavailableError("empty address list: DNS resolution note"));
  ExpectTransientFailureUpdate(
      absl::UnavailableError("empty address list: DNS resolution note"));
}

TEST_F(AutoShardingTest, EndpointsWithDuplicateHostnamesAreCollapsed) {
  // Two endpoints with the same hostname attribute.  The last one should win.
  const std::vector<EndpointAddresses> endpoints = {
      MakeEndpointAddresses({kAddresses[0]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1")),
      MakeEndpointAddresses({kAddresses[1]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(endpoints, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The pick should trigger a connection attempt on the endpoint that won
  // (the one with index 1).
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  EXPECT_EQ(FindSubchannel(kAddresses[0]), nullptr);
  auto* subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  EXPECT_EQ(address, kAddresses[1]);
}

TEST_F(AutoShardingTest, DuplicateHostnameReusesWinningEndpointAcrossUpdates) {
  // First update: a single endpoint with hostname "host1".  Drive it to
  // READY.
  const std::vector<EndpointAddresses> first_update = {MakeEndpointAddresses(
      {kAddresses[0]}, ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(first_update, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddresses[0]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);

  // Second update: hostname "host1" appears twice -- once for a throwaway
  // endpoint (which loses, since the last occurrence in a resolver update
  // wins) and once more for the exact same address it was already
  // connected to (which wins).  The winning occurrence must be the one
  // that gets to reuse the already-READY endpoint from the previous
  // update; if an earlier, losing occurrence grabs and discards it first
  // (the bug this test guards against), the policy ends up with a
  // brand-new, unconnected endpoint instead, and reports IDLE here rather
  // than staying READY.
  const std::vector<EndpointAddresses> second_update = {
      MakeEndpointAddresses({kAddresses[1]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1")),
      MakeEndpointAddresses({kAddresses[0]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(second_update, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::OkStatus());
  // Reconfiguring the reused endpoint's pick_first child (even with an
  // unchanged address) makes it re-register its subchannel watchers, which
  // triggers a second READY notification on top of the policy's own
  // unconditional post-update picker refresh; both report READY without an
  // intervening CONNECTING, confirming the endpoint was never actually
  // torn down and reconnected.
  ExpectState(GRPC_CHANNEL_READY);
  ExpectState(GRPC_CHANNEL_READY);
}

TEST_F(AutoShardingTest,
       AutoshardingTargetChangeResetsAssignmentAndRestartsTimer) {
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr && sc->ConnectionRequested()) {
      subchannel = sc;
      break;
    }
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  ASSERT_TRUE(address.has_value());

  // Second update with a different autosharding target.
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate(kAddresses,
                              MakeAutoShardingConfig(
                                  /*key_header_name_name=*/"x-slice-key",
                                  /*enable_fallback=*/true,
                                  /*initial_assignment_timeout=*/"1s",
                                  /*channel_factory_key=*/"sharding_service",
                                  /*autosharding_target=*/"new_target")),
                  lb_policy()),
      absl::OkStatus());
  // The reused endpoint re-registers subchannel watchers, triggering an
  // update; along with the policy's post-update refresh, both report READY,
  // but picks are queued because the initial assignment timer has restarted.
  ExpectState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);

  // Expire the restarted timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_READY);
  address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest, ChannelFactoryKeyChangeRestartsTimerAndQueuesPicks) {
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr && sc->ConnectionRequested()) {
      subchannel = sc;
      break;
    }
  }
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  ASSERT_TRUE(address.has_value());

  // Second update with a different channel factory key.
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses,
                                    MakeAutoShardingConfig(
                                        /*key_header_name_name=*/"x-slice-key",
                                        /*enable_fallback=*/true,
                                        /*initial_assignment_timeout=*/"1s",
                                        /*channel_factory_key=*/
                                        "new_channel_factory_key",
                                        /*autosharding_target=*/
                                        "my_autosharding_target")),
                        lb_policy()),
            absl::OkStatus());
  ExpectState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);

  // Expire the restarted timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_READY);
  address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest, EmptyKeyHeaderValueIsValidKey) {
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  const std::map<std::string, std::string> empty_key_metadata = {
      {"x-slice-key", ""}};
  ExpectPickQueued(picker.get(), {}, empty_key_metadata);
}

TEST_F(AutoShardingTest, SameAddressListedMultipleTimes) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:441"};
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  const std::map<std::string, std::string> metadata = {
      {"x-slice-key", "ipv4:127.0.0.1:441"}};
  ExpectPickQueued(picker.get(), {}, metadata);
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, metadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address :
       {"ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"}) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr && sc->ConnectionRequested()) {
      subchannel = sc;
      break;
    }
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
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kEndpoints, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  const std::map<std::string, std::string> metadata = {
      {"x-slice-key", "ipv4:127.0.0.1:443"}};
  ExpectPickQueued(picker.get(), {}, metadata);
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, metadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address :
       {"ipv4:127.0.0.1:443", "ipv4:127.0.0.1:444", "ipv4:127.0.0.1:445",
        "ipv4:127.0.0.1:446"}) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr && sc->ConnectionRequested()) {
      subchannel = sc;
      break;
    }
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
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
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
  auto address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  ASSERT_TRUE(address.has_value());
}

TEST_F(AutoShardingTest, StartupFallbackWithMultipleEndpoints) {
  const std::array<absl::string_view, 3> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442", "ipv4:127.0.0.1:443"};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  const std::map<std::string, std::string> metadata = {{"x-slice-key", "bar"}};
  ExpectPickQueued(picker.get(), {}, metadata);
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, metadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr && sc->ConnectionRequested()) {
      subchannel = sc;
      break;
    }
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

TEST_F(AutoShardingTest, ConfigFailsWithoutChannelFactoryKey) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"autoshardingTarget",
                      Json::FromString("my_autosharding_target")},
                     {"keyHeaderName", Json::FromString("x-slice-key")}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithEmptyChannelFactoryKey) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"channelFactoryKey", Json::FromString("")},
                     {"autoshardingTarget",
                      Json::FromString("my_autosharding_target")},
                     {"keyHeaderName", Json::FromString("x-slice-key")}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithoutAutoshardingTarget) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"channelFactoryKey",
                      Json::FromString("sharding_service")},
                     {"keyHeaderName", Json::FromString("x-slice-key")}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithEmptyAutoshardingTarget) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"channelFactoryKey",
                      Json::FromString("sharding_service")},
                     {"autoshardingTarget", Json::FromString("")},
                     {"keyHeaderName", Json::FromString("x-slice-key")}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithoutKeyHeaderName) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"channelFactoryKey",
                      Json::FromString("sharding_service")},
                     {"autoshardingTarget",
                      Json::FromString("my_autosharding_target")}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithEmptyKeyHeaderName) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"channelFactoryKey",
                      Json::FromString("sharding_service")},
                     {"autoshardingTarget",
                      Json::FromString("my_autosharding_target")},
                     {"keyHeaderName", Json::FromString("")}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithZeroInitialAssignmentTimeout) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"channelFactoryKey",
                      Json::FromString("sharding_service")},
                     {"autoshardingTarget",
                      Json::FromString("my_autosharding_target")},
                     {"keyHeaderName", Json::FromString("x-slice-key")},
                     {"initialAssignmentTimeout",
                      Json::FromString("0s")}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithNegativeInitialAssignmentTimeout) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"channelFactoryKey",
                      Json::FromString("sharding_service")},
                     {"autoshardingTarget",
                      Json::FromString("my_autosharding_target")},
                     {"keyHeaderName", Json::FromString("x-slice-key")},
                     {"initialAssignmentTimeout",
                      Json::FromString("-1s")}})}})}));
  EXPECT_FALSE(config.ok());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
