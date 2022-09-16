//
// Copyright 2022 gRPC authors.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "test/core/client_channel/lb_policy/lb_policy_test_lib.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class PickFirstTest : public LoadBalancingPolicyTest {
 protected:
  PickFirstTest() : lb_policy_(MakeLbPolicy("pick_first")) {}

  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
};

TEST_F(PickFirstTest, Basic) {
  constexpr absl::string_view kAddressUri = "ipv4:127.0.0.1:443";
  const grpc_resolved_address address = MakeAddress(kAddressUri);
  // Send an update containing one address.
  work_serializer_->Run(
      [&]() {
        LoadBalancingPolicy::UpdateArgs update_args;
        update_args.addresses.emplace();
        update_args.addresses->emplace_back(address, ChannelArgs());
        auto update_status = lb_policy_->UpdateLocked(std::move(update_args));
        EXPECT_TRUE(update_status.ok()) << update_status;
      },
      DEBUG_LOCATION);
  // LB policy should have reported CONNECTING state.
  auto picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get());
  // LB policy should have created a subchannel for the address with the
  // GRPC_ARG_INHIBIT_HEALTH_CHECKING channel arg.
  SubchannelKey key(address,
                    ChannelArgs().Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, true));
  auto it = subchannel_pool_.find(key);
  ASSERT_NE(it, subchannel_pool_.end());
  auto& subchannel_state = it->second;
  // LB policy should have requested a connection on this subchannel.
  EXPECT_TRUE(subchannel_state.ConnectionRequested());
  // Tell subchannel to report CONNECTING.
  work_serializer_->Run(
      [&]() {
        subchannel_state.SetConnectivityState(GRPC_CHANNEL_CONNECTING,
                                              absl::OkStatus());
      },
      DEBUG_LOCATION);
  // LB policy should again report CONNECTING.
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get());
  // Tell subchannel to report READY.
  work_serializer_->Run(
      [&]() {
        subchannel_state.SetConnectivityState(GRPC_CHANNEL_READY,
                                              absl::OkStatus());
      },
      DEBUG_LOCATION);
  // LB policy should report READY.
  picker = ExpectState(GRPC_CHANNEL_READY);
  // Picker should return the same subchannel repeatedly.
  for (size_t i = 0; i < 3; ++i) {
    ExecCtx exec_ctx;
    FakeMetadata metadata({});
    FakeCallState call_state;
    auto pick_result =
        picker->Pick({"/service/method", &metadata, &call_state});
    auto* complete = absl::get_if<LoadBalancingPolicy::PickResult::Complete>(
        &pick_result.result);
    ASSERT_NE(complete, nullptr);
    auto* subchannel = static_cast<SubchannelState::FakeSubchannel*>(
        complete->subchannel.get());
    auto address_uri = grpc_sockaddr_to_uri(&subchannel->address());
    ASSERT_TRUE(address_uri.ok()) << address_uri.status();
    EXPECT_EQ(*address_uri, kAddressUri);
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
