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

#include "src/core/ext/transport/chttp2/transport/http2_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/grpc.h>

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/time.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/test_util/postmortem.h"
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {
namespace http2 {
namespace testing {

///////////////////////////////////////////////////////////////////////////////
// Settings and ChannelArgs helpers tests

TEST(Http2CommonTransportTest, TestReadChannelArgs) {
  // Test to validate that ReadChannelArgs reads all the channel args
  // correctly.
  Http2Settings settings;
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  ChannelArgs channel_args =
      ChannelArgs()
          .Set(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER, 2048)
          .Set(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES, 1024)
          .Set(GRPC_ARG_HTTP2_MAX_FRAME_SIZE, 16384)
          .Set(GRPC_ARG_EXPERIMENTAL_HTTP2_PREFERRED_CRYPTO_FRAME_SIZE, true)
          .Set(GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY, 1)
          .Set(GRPC_ARG_SECURITY_FRAME_ALLOWED, true);
  ReadSettingsFromChannelArgs(channel_args, settings, transport_flow_control,
                              /*is_client=*/true);
  // Settings read from ChannelArgs.
  EXPECT_EQ(settings.header_table_size(), 2048u);
  EXPECT_EQ(settings.initial_window_size(), 1024u);
  EXPECT_EQ(settings.max_frame_size(), 16384u);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), INT_MAX);
  EXPECT_EQ(settings.allow_true_binary_metadata(), true);
  EXPECT_EQ(settings.allow_security_frame(), true);
  // Default settings
  EXPECT_EQ(settings.max_concurrent_streams(), 4294967295u);
  EXPECT_EQ(settings.max_header_list_size(), 16384u);
  EXPECT_EQ(settings.enable_push(), true);

  // If ChannelArgs don't have a value for the setting, the default must be
  // loaded into the Settings object
  Http2Settings settings2;
  EXPECT_EQ(settings2.header_table_size(), 4096u);
  EXPECT_EQ(settings2.max_concurrent_streams(), 4294967295u);
  EXPECT_EQ(settings2.initial_window_size(), 65535u);
  EXPECT_EQ(settings2.max_frame_size(), 16384u);
  // TODO(tjagtap) : [PH2][P4] : Investigate why we change it in
  // ReadSettingsFromChannelArgs . Right now ReadSettingsFromChannelArgs is
  // functinally similar to the legacy read_channel_args.
  EXPECT_EQ(settings2.max_header_list_size(), 16777216u);
  EXPECT_EQ(settings2.preferred_receive_crypto_message_size(), 0u);
  EXPECT_EQ(settings2.enable_push(), true);
  EXPECT_EQ(settings2.allow_true_binary_metadata(), false);
  EXPECT_EQ(settings2.allow_security_frame(), false);

  ReadSettingsFromChannelArgs(ChannelArgs(), settings2, transport_flow_control,
                              /*is_client=*/true);
  EXPECT_EQ(settings2.header_table_size(), 4096u);
  EXPECT_EQ(settings2.max_concurrent_streams(), 4294967295u);
  EXPECT_EQ(settings2.initial_window_size(), 65535u);
  EXPECT_EQ(settings2.max_frame_size(), 16384u);
  // TODO(tjagtap) : [PH2][P4] : Investigate why we change it in
  // ReadSettingsFromChannelArgs . Right now ReadSettingsFromChannelArgs is
  // functinally similar to the legacy read_channel_args.
  EXPECT_EQ(settings2.max_header_list_size(), 16384u);
  EXPECT_EQ(settings2.preferred_receive_crypto_message_size(), 0u);
  EXPECT_EQ(settings2.enable_push(), true);
  EXPECT_EQ(settings2.allow_true_binary_metadata(), false);
  EXPECT_EQ(settings2.allow_security_frame(), false);
}

///////////////////////////////////////////////////////////////////////////////
// Flow control helpers tests

TEST(Http2CommonTransportTest, ProcessOutgoingDataFrameFlowControlTest) {
  chttp2::TransportFlowControl transport_flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  chttp2::StreamFlowControl stream_flow_control(&transport_flow_control);
  EXPECT_EQ(transport_flow_control.remote_window(), chttp2::kDefaultWindow);
  EXPECT_EQ(stream_flow_control.remote_window_delta(), 0);

  ProcessOutgoingDataFrameFlowControl(stream_flow_control, 1000);
  EXPECT_EQ(transport_flow_control.remote_window(),
            chttp2::kDefaultWindow - 1000);
  EXPECT_EQ(stream_flow_control.remote_window_delta(), -1000);

  // Test with 0 tokens consumed
  for (int i = 0; i < 3; ++i) {
    ProcessOutgoingDataFrameFlowControl(stream_flow_control, 0);
    EXPECT_EQ(transport_flow_control.remote_window(),
              chttp2::kDefaultWindow - 1000);
    EXPECT_EQ(stream_flow_control.remote_window_delta(), -1000);
  }
}

TEST(Http2CommonTransportTest, ProcessIncomingDataFrameFlowControlNullStream) {
  const uint32_t frame_payload_size = 20000;
  chttp2::TransportFlowControl flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  Http2FrameHeader frame_header;
  frame_header.length = frame_payload_size;
  frame_header.type = 0;  // DATA Frame
  frame_header.flags = 0;
  frame_header.stream_id = 1;

  EXPECT_EQ(flow_control.announced_window(), chttp2::kDefaultWindow);

  // First DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control, nullptr);
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(flow_control.announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);

  // 2nd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control, nullptr);
  EXPECT_TRUE(action2.IsOk());
  EXPECT_EQ(flow_control.announced_window(),
            chttp2::kDefaultWindow - 2 * frame_payload_size);

  // 3rd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action3 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control, nullptr);
  EXPECT_TRUE(action3.IsOk());
  EXPECT_EQ(flow_control.announced_window(),
            chttp2::kDefaultWindow - 3 * frame_payload_size);

  // 4th DATA frame of size frame_payload_size.
  // This will fail because the flow control window is exhausted.
  ValueOrHttp2Status<chttp2::FlowControlAction> action4 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control, nullptr);
  // Invalid operation because flow control window was exceeded.
  EXPECT_FALSE(action4.IsOk());
  EXPECT_EQ(action4.GetErrorType(),
            Http2Status::Http2ErrorType::kConnectionError);
  EXPECT_EQ(action4.GetConnectionErrorCode(),
            Http2ErrorCode::kFlowControlError);
  EXPECT_EQ(action4.DebugString(),
            "Connection Error: {Error Code:FLOW_CONTROL_ERROR, Message:frame "
            "of size 20000 overflows local window of 5535}");
}

TEST(Http2CommonTransportTest, ProcessIncomingDataFrameFlowControlNullStream1) {
  const uint32_t frame_payload_size = 60000;
  chttp2::TransportFlowControl flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  Http2FrameHeader frame_header;
  frame_header.length = frame_payload_size;
  frame_header.type = 0;  // DATA Frame
  frame_header.flags = 0;
  frame_header.stream_id = 1;

  EXPECT_EQ(flow_control.announced_window(), chttp2::kDefaultWindow);

  // Receive first large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control, nullptr);
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(flow_control.announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);

  // Send the flow control update to peer
  uint32_t increment = flow_control.MaybeSendUpdate(true);

  // Receive 2nd large DATA frame.
  // This should be accepted because we sent fresh flow control tokens.
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control, nullptr);
  EXPECT_TRUE(action2.IsOk());
  EXPECT_EQ(flow_control.announced_window(),
            (chttp2::kDefaultWindow + increment) - 2 * frame_payload_size);

  // For an empty DATA frame the flow control window must not change.
  // All empty DATA frames should be accepted by flow control.
  frame_header.length = 0;
  for (int i = 0; i < 3; ++i) {
    ValueOrHttp2Status<chttp2::FlowControlAction> action3 =
        ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                            nullptr);
    EXPECT_TRUE(action3.IsOk());
    EXPECT_EQ(flow_control.announced_window(),
              (chttp2::kDefaultWindow + increment) - 2 * frame_payload_size);
  }
}

}  // namespace testing

}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
