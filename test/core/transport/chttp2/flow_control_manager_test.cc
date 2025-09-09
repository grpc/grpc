//
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
//

#include "src/core/ext/transport/chttp2/transport/flow_control_manager.h"

#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"

namespace grpc_core {
namespace http2 {
namespace {

constexpr uint32_t kTestMaxFrameSize = RFC9113::kMinimumFrameSize + 10;

class FlowControlManagerTest
    : public ::testing::TestWithParam<
          std::tuple<bool, chttp2::FlowControlAction::Urgency>> {};

TEST_P(FlowControlManagerTest, ActOnFlowControlActionSettings) {
  Http2Settings settings;
  chttp2::FlowControlAction action;
  const bool enable_preferred_rx_crypto_frame_advertisement =
      std::get<0>(GetParam());
  const chttp2::FlowControlAction::Urgency urgency = std::get<1>(GetParam());

  const uint32_t initial_window_size = settings.initial_window_size();
  const uint32_t max_frame_size = settings.max_frame_size();
  const uint32_t initial_preferred_receive_crypto_message_size =
      settings.preferred_receive_crypto_message_size();

  action.set_send_initial_window_update(urgency, initial_window_size + 10);
  action.set_send_max_frame_size_update(urgency, max_frame_size + 10);
  action.set_preferred_rx_crypto_frame_size_update(urgency, kTestMaxFrameSize);

  ActOnFlowControlActionSettings(
      action, settings, enable_preferred_rx_crypto_frame_advertisement);

  EXPECT_EQ(settings.initial_window_size(), initial_window_size + 10);
  EXPECT_EQ(settings.max_frame_size(), max_frame_size + 10);
  if (enable_preferred_rx_crypto_frame_advertisement) {
    EXPECT_EQ(settings.preferred_receive_crypto_message_size(),
              kTestMaxFrameSize);
  } else {
    EXPECT_EQ(settings.preferred_receive_crypto_message_size(),
              initial_preferred_receive_crypto_message_size);
  }
}

INSTANTIATE_TEST_SUITE_P(
    FlowControlManagerTest, FlowControlManagerTest,
    ::testing::Combine(
        ::testing::Bool(),
        ::testing::Values(
            chttp2::FlowControlAction::Urgency::UPDATE_IMMEDIATELY,
            chttp2::FlowControlAction::Urgency::QUEUE_UPDATE)));

TEST(FlowControlManagerTest, ActOnFlowControlActionSettingsNoActionNeeded) {
  Http2Settings settings;
  chttp2::FlowControlAction action;

  const uint32_t initial_window_size = settings.initial_window_size();
  const uint32_t max_frame_size = settings.max_frame_size();
  const uint32_t preferred_receive_crypto_message_size =
      settings.preferred_receive_crypto_message_size();

  action.set_send_initial_window_update(kNoActionNeeded,
                                        initial_window_size + 10);
  action.set_send_max_frame_size_update(kNoActionNeeded, max_frame_size + 10);
  action.set_preferred_rx_crypto_frame_size_update(
      kNoActionNeeded, preferred_receive_crypto_message_size + 10);

  ActOnFlowControlActionSettings(
      action, settings,
      /*enable_preferred_rx_crypto_frame_advertisement=*/true);

  EXPECT_EQ(settings.initial_window_size(), initial_window_size);
  EXPECT_EQ(settings.max_frame_size(), max_frame_size);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(),
            preferred_receive_crypto_message_size);
}

TEST(FlowControlManagerTest, ActOnFlowControlActionSettingsNoAction) {
  Http2Settings settings;
  chttp2::FlowControlAction action;

  const uint32_t initial_window_size = settings.initial_window_size();
  const uint32_t max_frame_size = settings.max_frame_size();
  const uint32_t preferred_receive_crypto_message_size =
      settings.preferred_receive_crypto_message_size();

  ActOnFlowControlActionSettings(
      action, settings,
      /*enable_preferred_rx_crypto_frame_advertisement=*/true);

  EXPECT_EQ(settings.initial_window_size(), initial_window_size);
  EXPECT_EQ(settings.max_frame_size(), max_frame_size);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(),
            preferred_receive_crypto_message_size);
}

}  // namespace
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
