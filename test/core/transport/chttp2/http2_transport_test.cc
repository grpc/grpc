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
#include <vector>

#include "src/core/call/call_spine.h"
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
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/time.h"
#include "test/core/promise/poll_matcher.h"
#include "test/core/test_util/postmortem.h"
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "test/core/transport/util/transport_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {
namespace testing {

class TestsNeedingStreamObjects : public ::testing::Test {
 protected:
  TestsNeedingStreamObjects()
      : transport_flow_control_(
            /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
            /*memory_owner=*/nullptr) {}

  void SetUp() override {}

  RefCountedPtr<Stream> CreateMinimalTestStream(uint32_t stream_id) {
    RefCountedPtr<Arena> arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        grpc_event_engine::experimental::GetDefaultEventEngine().get());
    auto client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString("/foo/bar"));
    std::unique_ptr<CallInitiatorAndHandler> call_pair =
        std::make_unique<CallInitiatorAndHandler>(
            MakeCallPair(std::move(client_initial_metadata), std::move(arena)));
    RefCountedPtr<Stream> stream = MakeRefCounted<Stream>(
        call_pair->handler.StartCall(),
        /*allow_true_binary_metadata_peer=*/true,
        /*allow_true_binary_metadata_acked=*/true, transport_flow_control_);
    stream->SetStreamId(stream_id);
    GRPC_CHECK_EQ(stream->stream_id, stream_id);
    stream_set_.push_back(std::move(stream));
    return stream_set_.back();
  }
  chttp2::TransportFlowControl transport_flow_control_;

 private:
  std::vector<RefCountedPtr<Stream>> stream_set_;
};

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

  EXPECT_EQ(flow_control.test_only_announced_window(), chttp2::kDefaultWindow);

  // First DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                          /*stream=*/nullptr);
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(flow_control.test_only_announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);

  // 2nd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                          /*stream=*/nullptr);
  EXPECT_TRUE(action2.IsOk());
  EXPECT_EQ(flow_control.test_only_announced_window(),
            chttp2::kDefaultWindow - 2 * frame_payload_size);

  // 3rd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action3 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                          /*stream=*/nullptr);
  EXPECT_TRUE(action3.IsOk());
  EXPECT_EQ(flow_control.test_only_announced_window(),
            chttp2::kDefaultWindow - 3 * frame_payload_size);

  // 4th DATA frame of size frame_payload_size.
  // This will fail because the flow control window is exhausted.
  ValueOrHttp2Status<chttp2::FlowControlAction> action4 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                          /*stream=*/nullptr);
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

  EXPECT_EQ(flow_control.test_only_announced_window(), chttp2::kDefaultWindow);

  // Receive first large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                          /*stream=*/nullptr);
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(flow_control.test_only_announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);

  // Send the flow control update to peer
  uint32_t increment = flow_control.MaybeSendUpdate(/*writing_anyway=*/true);

  // Receive 2nd large DATA frame.
  // This should be accepted because we sent fresh flow control tokens.
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                          /*stream=*/nullptr);
  EXPECT_TRUE(action2.IsOk());
  EXPECT_EQ(flow_control.test_only_announced_window(),
            (chttp2::kDefaultWindow + increment) - 2 * frame_payload_size);

  // For an empty DATA frame the flow control window must not change.
  // All empty DATA frames should be accepted by flow control.
  frame_header.length = 0;
  for (int i = 0; i < 3; ++i) {
    ValueOrHttp2Status<chttp2::FlowControlAction> action3 =
        ProcessIncomingDataFrameFlowControl(frame_header, flow_control,
                                            /*stream=*/nullptr);
    EXPECT_TRUE(action3.IsOk());
    EXPECT_EQ(flow_control.test_only_announced_window(),
              (chttp2::kDefaultWindow + increment) - 2 * frame_payload_size);
  }
}

TEST_F(TestsNeedingStreamObjects,
       ProcessIncomingDataFrameFlowControlWithStream) {
  const uint32_t frame_payload_size = 20000;
  RefCountedPtr<Stream> stream = CreateMinimalTestStream(1);
  Http2FrameHeader frame_header;
  frame_header.length = frame_payload_size;
  frame_header.type = 0;  // DATA Frame
  frame_header.flags = 0;
  frame_header.stream_id = 1;

  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(), 0);

  // First DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            -static_cast<int64_t>(frame_payload_size));

  // 2nd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
  EXPECT_TRUE(action2.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - 2 * frame_payload_size);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            -2 * static_cast<int64_t>(frame_payload_size));

  // 3rd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action3 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
  EXPECT_TRUE(action3.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - 3 * frame_payload_size);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            -3 * static_cast<int64_t>(frame_payload_size));

  // 4th DATA frame of size frame_payload_size.
  // This will fail because the flow control window is exhausted.
  ValueOrHttp2Status<chttp2::FlowControlAction> action4 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
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

TEST_F(TestsNeedingStreamObjects,
       ProcessIncomingDataFrameTransportWindowUpdate) {
  const uint32_t frame_payload_size = 60000;
  RefCountedPtr<Stream> stream = CreateMinimalTestStream(1);
  Http2FrameHeader frame_header;
  frame_header.length = frame_payload_size;
  frame_header.type = 0;  // DATA Frame
  frame_header.flags = 0;
  frame_header.stream_id = 1;

  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(), 0);

  // Receive first large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            -static_cast<int64_t>(frame_payload_size));

  // Send the flow control update to peer for transport
  uint32_t increment =
      transport_flow_control_.MaybeSendUpdate(/*writing_anyway=*/true);
  EXPECT_GT(increment, 0);
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - frame_payload_size + increment);

  // Receive 2nd large DATA frame.
  // This should be fail because stream window is not updated.
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
  EXPECT_FALSE(action2.IsOk());
  EXPECT_EQ(action2.GetErrorType(),
            Http2Status::Http2ErrorType::kConnectionError);
  EXPECT_EQ(action2.GetConnectionErrorCode(),
            Http2ErrorCode::kFlowControlError);
  EXPECT_EQ(
      action2.DebugString(),
      "Connection Error: {Error Code:FLOW_CONTROL_ERROR, Message:frame of "
      "size 60000 overflows local window of 5535}");
}

TEST_F(TestsNeedingStreamObjects,
       ProcessIncomingDataFrameTransportAndStreamWindowUpdate) {
  const uint32_t frame_payload_size = 60000;
  RefCountedPtr<Stream> stream = CreateMinimalTestStream(1);
  Http2FrameHeader frame_header;
  frame_header.length = frame_payload_size;
  frame_header.type = 0;  // DATA Frame
  frame_header.flags = 0;
  frame_header.stream_id = 1;
  int64_t expected_announced_window = chttp2::kDefaultWindow;
  int64_t expected_announced_window_delta = 0;

  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            expected_announced_window);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            expected_announced_window_delta);

  // Receive first large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
  expected_announced_window -= frame_payload_size;
  expected_announced_window_delta -= frame_payload_size;
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            expected_announced_window);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            expected_announced_window_delta);

  chttp2::StreamFlowControl::IncomingUpdateContext stream_flow_control_context(
      &stream->flow_control);
  stream_flow_control_context.SetMinProgressSize(frame_payload_size);
  chttp2::FlowControlAction action =
      stream_flow_control_context.::testing::internal::MakeAction();
  EXPECT_EQ(action.send_stream_update(),
            chttp2::FlowControlAction::Urgency::UPDATE_IMMEDIATELY);

  // Send the flow control update to peer for stream
  uint32_t transport_increment =
      transport_flow_control_.MaybeSendUpdate(/*writing_anyway=*/true);
  uint32_t stream_increment = stream->flow_control.MaybeSendUpdate();
  EXPECT_GT(transport_increment, 0);
  EXPECT_GT(stream_increment, 0);
  expected_announced_window += transport_increment;
  expected_announced_window_delta += stream_increment;
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            expected_announced_window);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            expected_announced_window_delta);

  // Receive 2nd large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream);
  EXPECT_TRUE(action2.IsOk());
  expected_announced_window -= frame_payload_size;
  expected_announced_window_delta -= frame_payload_size;
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            expected_announced_window);
  EXPECT_EQ(stream->flow_control.test_only_announced_window_delta(),
            expected_announced_window_delta);
}

TEST(Http2CommonTransportTest,
     ProcessIncomingWindowUpdateFrameFlowControlNullStream) {
  chttp2::TransportFlowControl flow_control(
      /*name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);
  EXPECT_EQ(flow_control.remote_window(), chttp2::kDefaultWindow);

  Http2WindowUpdateFrame frame;
  frame.increment = 1000;

  // If stream_id != 0 and stream is null, no change in flow control window.
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, flow_control,
                                              /*stream=*/nullptr);
  EXPECT_EQ(flow_control.remote_window(), chttp2::kDefaultWindow);

  // If stream_id == 0, transport flow control window should increase.
  frame.stream_id = 0;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, flow_control,
                                              /*stream=*/nullptr);
  EXPECT_EQ(flow_control.remote_window(), chttp2::kDefaultWindow + 1000);

  // If increment is 0, no change in flow control window.
  // Although 0 increment would be a connection layer at the frame parsing
  // layer, we should be graceful with it at this layer.
  frame.increment = 0;
  frame.stream_id = 0;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, flow_control,
                                              /*stream=*/nullptr);
  EXPECT_EQ(flow_control.remote_window(), chttp2::kDefaultWindow + 1000);
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, flow_control,
                                              /*stream=*/nullptr);
  EXPECT_EQ(flow_control.remote_window(), chttp2::kDefaultWindow + 1000);

  // Large increment
  frame.increment = 10000;
  frame.stream_id = 0;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, flow_control,
                                              /*stream=*/nullptr);
  EXPECT_EQ(flow_control.remote_window(),
            chttp2::kDefaultWindow + 1000 + 10000);
}

TEST_F(TestsNeedingStreamObjects,
       ProcessIncomingWindowUpdateFrameFlowControlWithStream) {
  RefCountedPtr<Stream> stream = CreateMinimalTestStream(1);
  EXPECT_EQ(transport_flow_control_.remote_window(), chttp2::kDefaultWindow);
  EXPECT_EQ(stream->flow_control.remote_window_delta(), 0);

  Http2WindowUpdateFrame frame;
  frame.increment = 1000;

  // If stream_id != 0 and stream is not null, stream flow control window should
  // increase.
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream);
  EXPECT_EQ(transport_flow_control_.remote_window(), chttp2::kDefaultWindow);
  EXPECT_EQ(stream->flow_control.remote_window_delta(), 1000);

  // If stream_id == 0, transport flow control window should increase.
  frame.stream_id = 0;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream);
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->flow_control.remote_window_delta(), 1000);

  // If increment is 0, no change in flow control window.
  // Although 0 increment would be a connection layer at the frame parsing
  // layer, we should be graceful with it at this layer.
  frame.increment = 0;
  frame.stream_id = 0;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream);
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->flow_control.remote_window_delta(), 1000);
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream);
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->flow_control.remote_window_delta(), 1000);

  // Large increment
  frame.increment = 10000;
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream);
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->flow_control.remote_window_delta(), 1000 + 10000);
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
