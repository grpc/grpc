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

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

  action.test_only_set_send_initial_window_update(urgency,
                                                  initial_window_size + 10);
  action.test_only_set_send_max_frame_size_update(urgency, max_frame_size + 10);
  action.test_only_set_preferred_rx_crypto_frame_size_update(urgency,
                                                             kTestMaxFrameSize);

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

  action.test_only_set_send_initial_window_update(kNoActionNeeded,
                                                  initial_window_size + 10);
  action.test_only_set_send_max_frame_size_update(kNoActionNeeded,
                                                  max_frame_size + 10);
  action.test_only_set_preferred_rx_crypto_frame_size_update(
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

TEST(FlowControlManagerTest, GetMaxPermittedDequeue) {
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  chttp2::StreamFlowControl stream_flow_control(&transport_flow_control);
  Http2Settings peer_settings;  // The defaults are fine for our tests.

  // Initial windows: transport=65535, stream_delta=0, initial_window=65535
  // flow_control_tokens = min(65535, 0+65535) = 65535
  EXPECT_EQ(chttp2::kDefaultWindow,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/100000u, peer_settings));
  EXPECT_EQ(100,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/100u, peer_settings));
  EXPECT_EQ(0,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/0u, peer_settings));
  EXPECT_EQ(1000,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/1000u, peer_settings));

  // If stream flow control has negative delta, reduces effective stream window.
  // Send 1000 bytes, transport window reduces by 1000, stream delta becomes
  // -1000.
  {
    chttp2::StreamFlowControl::OutgoingUpdateContext sfc_upd(
        &stream_flow_control);
    sfc_upd.SentData(1000);
  }
  // transport window = 65535-1000=64535, stream_delta=-1000
  // flow_control_tokens = min(64535, -1000+65535) = min(64535, 64535) = 64535
  EXPECT_EQ(chttp2::kDefaultWindow - 1000,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/100000u, peer_settings));

  // If transport window is limiting
  {
    chttp2::StreamFlowControl::OutgoingUpdateContext sfc_upd(
        &stream_flow_control);
    sfc_upd.SentData(60000);
    // This restores the stream tokens, but NOT the transport tokens.
    sfc_upd.RecvUpdate(60000);
  }
  // transport window = 64535-60000=4535, stream_delta=-1000-60000+60000 = -1000
  // flow_control_tokens = min(4535, -1000+65535) = min(4535, 64535) = 4535
  EXPECT_EQ(4535,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/100000u, peer_settings));

  // If stream window + initial window is limiting
  // increase transport window
  {
    chttp2::TransportFlowControl::OutgoingUpdateContext tfc_upd(
        &transport_flow_control);
    tfc_upd.RecvUpdate(60000);
  }
  // transport window = 4535+60000=64535, stream_delta=-1000
  // Decrease peer_settings initial window size
  peer_settings.SetInitialWindowSize(1000);
  // stream_delta=-1000, initial_window=1000
  // flow_control_tokens = min(64535, -1000+1000) = min(64535, 0) = 0
  EXPECT_EQ(0,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/100000u, peer_settings));

  // If flow_control_tokens is 0, max_dequeue is 0.
  peer_settings.SetInitialWindowSize(1000);
  // transport window = 64535, stream_delta = -1000, initial_window=1000
  // stream_fc.remote_window_delta() + peer_settings.initial_window_size() = 0
  // flow_control_tokens = min(64535, 0) = 0
  EXPECT_EQ(0,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/1000u, peer_settings));

  // If flow_control_tokens is negative, max_dequeue is 0.
  peer_settings.SetInitialWindowSize(500);
  // transport window = 64535, stream_delta = -1000
  // initial_window=500
  // stream_fc.remote_window_delta() + peer_settings.initial_window_size()= -500
  // flow_control_tokens = min(64535, -500) = -500
  EXPECT_EQ(0,
            GetMaxPermittedDequeue(transport_flow_control, stream_flow_control,
                                   /*upper_limit=*/1000u, peer_settings));
}

}  // namespace
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
