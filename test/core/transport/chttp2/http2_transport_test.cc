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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_promises.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/internal_channel_arg_names.h"
#include "src/core/ext/transport/chttp2/transport/read_context.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {
namespace testing {

class TestsNeedingStreamObjects : public ::testing::TestWithParam<bool> {
 protected:
  TestsNeedingStreamObjects()
      : transport_flow_control_(
            /*peer_name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
            /*memory_owner=*/nullptr),
        is_client_(GetParam()) {}

  void SetUp() override {}

  bool is_client() const { return is_client_; }

  RefCountedPtr<Stream> CreateMinimalTestStream(const uint32_t stream_id) {
    RefCountedPtr<Arena> arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        grpc_event_engine::experimental::GetDefaultEventEngine().get());
    Arena::PoolPtr<ClientMetadata> client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString("/foo/bar"));
    const std::unique_ptr<CallInitiatorAndHandler> call_pair =
        std::make_unique<CallInitiatorAndHandler>(
            MakeCallPair(std::move(client_initial_metadata), std::move(arena)));
    RefCountedPtr<Stream> stream =
        is_client_ ? MakeRefCounted<Stream>(call_pair->handler.StartCall(),
                                            transport_flow_control_)
                   : MakeRefCounted<Stream>(
                         call_pair->initiator, transport_flow_control_,
                         stream_id, /*allow_true_binary_metadata_peer=*/true);
    if (is_client_) {
      stream->InitializeClientStream(stream_id,
                                     /*allow_true_binary_metadata_peer=*/true);
    }
    GRPC_CHECK_EQ(stream->GetStreamId(), stream_id);
    stream_set_.push_back(std::move(stream));
    return stream_set_.back();
  }
  chttp2::TransportFlowControl transport_flow_control_;

 private:
  std::vector<RefCountedPtr<Stream>> stream_set_;
  const bool is_client_;
};

INSTANTIATE_TEST_SUITE_P(TestsNeedingStreamObjects, TestsNeedingStreamObjects,
                         ::testing::Bool());

///////////////////////////////////////////////////////////////////////////////
// Connection Preface Validation Tests

class ConnectionPrefaceValidationTest : public ::testing::Test {
 protected:
  void VerifyProtocolError(absl::StatusOr<Slice> input) {
    Http2Status result = ValidateIncomingConnectionPreface(input);
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.GetType(), Http2Status::Http2ErrorType::kConnectionError);
    EXPECT_EQ(result.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
    EXPECT_EQ(result.GetAbslConnectionError().message(),
              RFC9113::kFirstSettingsFrameServer);
  }
};

TEST_F(ConnectionPrefaceValidationTest,
       ValidateIncomingConnectionPrefaceSuccess) {
  absl::StatusOr<Slice> status =
      Slice::FromStaticString(GRPC_CHTTP2_CLIENT_CONNECT_STRING);
  Http2Status result = ValidateIncomingConnectionPreface(status);
  EXPECT_TRUE(result.IsOk());
}

TEST_F(ConnectionPrefaceValidationTest,
       ValidateIncomingConnectionPrefaceErrorStatus) {
  absl::Status error = absl::InternalError("some error");
  absl::StatusOr<Slice> status = error;
  Http2Status result = ValidateIncomingConnectionPreface(status);
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.GetType(), Http2Status::Http2ErrorType::kConnectionError);
  EXPECT_EQ(result.GetAbslConnectionError().code(),
            absl::StatusCode::kInternal);
  EXPECT_EQ(result.GetAbslConnectionError().message(), "some error");
}

TEST_F(ConnectionPrefaceValidationTest,
       ValidateIncomingConnectionPrefaceWrongString) {
  // Case 1: Random wrong string
  VerifyProtocolError(Slice::FromStaticString("WRONG STRING"));

  std::string correct_preface = GRPC_CHTTP2_CLIENT_CONNECT_STRING;

  // Case 2: One character different
  std::string wrong_preface = correct_preface;
  wrong_preface.back() = 'a';
  VerifyProtocolError(Slice::FromCopiedString(wrong_preface));

  // Case 3: One character less
  std::string short_preface =
      correct_preface.substr(0, correct_preface.length() - 1);
  VerifyProtocolError(Slice::FromCopiedString(short_preface));
}

///////////////////////////////////////////////////////////////////////////////
// Settings and ChannelArgs helpers tests

TEST(Http2CommonTransportTest, TestReadChannelArgs) {
  // Test to validate that ReadChannelArgs reads all the channel args
  // correctly.
  Http2Settings settings;
  chttp2::TransportFlowControl transport_flow_control(
      /*peer_name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
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

TEST(Http2CommonTransportTest, TestReadTransportChannelArgs) {
  // Test to validate that ReadChannelArgs reads all the channel args
  // correctly into TransportChannelArgs.
  Http2Settings settings;
  chttp2::TransportFlowControl transport_flow_control(
      /*peer_name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
      /*memory_owner=*/nullptr);

  {
    TransportChannelArgs args;
    // 1. Test Client Defaults
    ReadChannelArgs(ChannelArgs(), args, settings, transport_flow_control,
                    /*is_client=*/true);

    EXPECT_EQ(args.keepalive_time, Duration::Infinity());
    EXPECT_EQ(args.keepalive_timeout, Duration::Infinity());
    EXPECT_EQ(args.ping_timeout, Duration::Infinity());
    EXPECT_EQ(args.settings_timeout, Duration::Infinity());
    EXPECT_EQ(args.keepalive_permit_without_calls, false);
    EXPECT_EQ(transport_flow_control.ph2_enable_rx_crypto(), false);
    EXPECT_EQ(args.max_usable_hpack_table_size, -1);
    EXPECT_GE(args.max_header_list_size_soft_limit, 8192u);
  }

  {
    // 2. Test Server Defaults
    TransportChannelArgs args;
    ReadChannelArgs(ChannelArgs(), args, settings, transport_flow_control,
                    /*is_client=*/false);

    EXPECT_EQ(args.keepalive_time, Duration::Hours(2));
    EXPECT_EQ(args.keepalive_timeout, Duration::Seconds(20));
    EXPECT_EQ(args.ping_timeout, Duration::Minutes(1));
    EXPECT_EQ(args.settings_timeout, Duration::Minutes(1));
    EXPECT_EQ(args.keepalive_permit_without_calls, false);
    EXPECT_EQ(transport_flow_control.ph2_enable_rx_crypto(), false);
    EXPECT_EQ(args.max_usable_hpack_table_size, -1);
    EXPECT_GE(args.max_header_list_size_soft_limit, 8192u);
  }

  {
    // 3. Test Overrides
    TransportChannelArgs args;
    ChannelArgs channel_args =
        ChannelArgs()
            .Set(GRPC_ARG_KEEPALIVE_TIME_MS, 10000)    // 10s
            .Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000)  // 5s
            .Set(GRPC_ARG_PING_TIMEOUT_MS, 3000)       // 3s
            .Set(GRPC_ARG_SETTINGS_TIMEOUT, 15000)     // 15s
            .Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true)
            .Set(GRPC_ARG_EXPERIMENTAL_HTTP2_PREFERRED_CRYPTO_FRAME_SIZE, true)
            .Set(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER, 1024)
            .Set(GRPC_ARG_MAX_METADATA_SIZE, 12345);

    ReadChannelArgs(channel_args, args, settings, transport_flow_control,
                    /*is_client=*/true);

    EXPECT_EQ(args.keepalive_time, Duration::Seconds(10));
    EXPECT_EQ(args.keepalive_timeout, Duration::Seconds(5));
    EXPECT_EQ(args.ping_timeout, Duration::Seconds(3));
    EXPECT_EQ(args.settings_timeout, Duration::Seconds(15));
    EXPECT_EQ(args.keepalive_permit_without_calls, true);
    EXPECT_EQ(transport_flow_control.ph2_enable_rx_crypto(), true);
    EXPECT_EQ(args.max_usable_hpack_table_size, 1024);
    EXPECT_EQ(args.max_header_list_size_soft_limit, 12345u);
  }

  {
    // 4. Test Settings Timeout logic derived from keepalive_timeout
    TransportChannelArgs args;
    ChannelArgs channel_args_2 =
        ChannelArgs()
            .Set(GRPC_ARG_KEEPALIVE_TIME_MS, 100000)  // 100s, just to be finite
            .Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 40000);  // 40s

    ReadChannelArgs(channel_args_2, args, settings, transport_flow_control,
                    /*is_client=*/true);

    EXPECT_EQ(args.settings_timeout, Duration::Seconds(80));
  }
}

///////////////////////////////////////////////////////////////////////////////
// Flow control helpers tests

TEST(Http2CommonTransportTest, ProcessOutgoingDataFrameFlowControlTest) {
  chttp2::TransportFlowControl transport_flow_control(
      /*peer_name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
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
      /*peer_name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
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
      /*peer_name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
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

TEST_P(TestsNeedingStreamObjects,
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
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            0);

  // First DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream.get());
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            -static_cast<int64_t>(frame_payload_size));

  // 2nd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream.get());
  EXPECT_TRUE(action2.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - 2 * frame_payload_size);
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            -2 * static_cast<int64_t>(frame_payload_size));

  // 3rd DATA frame of size frame_payload_size
  ValueOrHttp2Status<chttp2::FlowControlAction> action3 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream.get());
  EXPECT_TRUE(action3.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - 3 * frame_payload_size);
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            -3 * static_cast<int64_t>(frame_payload_size));

  // 4th DATA frame of size frame_payload_size.
  // This will fail because the flow control window is exhausted.
  ValueOrHttp2Status<chttp2::FlowControlAction> action4 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream.get());
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

TEST_P(TestsNeedingStreamObjects,
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
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            0);

  // Receive first large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream.get());
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            chttp2::kDefaultWindow - frame_payload_size);
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
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
                                          stream.get());
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

TEST_P(TestsNeedingStreamObjects,
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
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            expected_announced_window_delta);

  // Receive first large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action1 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream.get());
  expected_announced_window -= frame_payload_size;
  expected_announced_window_delta -= frame_payload_size;
  EXPECT_TRUE(action1.IsOk());
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            expected_announced_window);
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            expected_announced_window_delta);

  chttp2::StreamFlowControl::IncomingUpdateContext stream_flow_control_context(
      &stream->GetStreamFlowControl());
  stream_flow_control_context.SetMinProgressSize(frame_payload_size);
  chttp2::FlowControlAction action = stream_flow_control_context.MakeAction();
  EXPECT_EQ(action.send_stream_update(),
            chttp2::FlowControlAction::Urgency::UPDATE_IMMEDIATELY);

  // Send the flow control update to peer for stream
  uint32_t transport_increment =
      transport_flow_control_.MaybeSendUpdate(/*writing_anyway=*/true);
  uint32_t stream_increment = stream->GetStreamFlowControl().MaybeSendUpdate();
  EXPECT_GT(transport_increment, 0);
  EXPECT_GT(stream_increment, 0);
  expected_announced_window += transport_increment;
  expected_announced_window_delta += stream_increment;
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            expected_announced_window);
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            expected_announced_window_delta);

  // Receive 2nd large DATA frame.
  ValueOrHttp2Status<chttp2::FlowControlAction> action2 =
      ProcessIncomingDataFrameFlowControl(frame_header, transport_flow_control_,
                                          stream.get());
  EXPECT_TRUE(action2.IsOk());
  expected_announced_window -= frame_payload_size;
  expected_announced_window_delta -= frame_payload_size;
  EXPECT_EQ(transport_flow_control_.test_only_announced_window(),
            expected_announced_window);
  EXPECT_EQ(stream->GetStreamFlowControl().test_only_announced_window_delta(),
            expected_announced_window_delta);
}

TEST(Http2CommonTransportTest,
     ProcessIncomingWindowUpdateFrameFlowControlNullStream) {
  chttp2::TransportFlowControl flow_control(
      /*peer_name=*/"TestFlowControl", /*enable_bdp_probe=*/false,
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

TEST_P(TestsNeedingStreamObjects,
       ProcessIncomingWindowUpdateFrameFlowControlWithStream) {
  RefCountedPtr<Stream> stream = CreateMinimalTestStream(1);
  EXPECT_EQ(transport_flow_control_.remote_window(), chttp2::kDefaultWindow);
  EXPECT_EQ(stream->GetStreamFlowControl().remote_window_delta(), 0);

  Http2WindowUpdateFrame frame;
  frame.increment = 1000;

  // If stream_id != 0 and stream is not null, stream flow control window
  // should increase.
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream.get());
  EXPECT_EQ(transport_flow_control_.remote_window(), chttp2::kDefaultWindow);
  EXPECT_EQ(stream->GetStreamFlowControl().remote_window_delta(), 1000);

  // If stream_id == 0, transport flow control window should increase.
  frame.stream_id = 0;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream.get());
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->GetStreamFlowControl().remote_window_delta(), 1000);

  // If increment is 0, no change in flow control window.
  // Although 0 increment would be a connection layer at the frame parsing
  // layer, we should be graceful with it at this layer.
  frame.increment = 0;
  frame.stream_id = 0;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream.get());
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->GetStreamFlowControl().remote_window_delta(), 1000);
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream.get());
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->GetStreamFlowControl().remote_window_delta(), 1000);

  // Large increment
  frame.increment = 10000;
  frame.stream_id = 1;
  ProcessIncomingWindowUpdateFrameFlowControl(frame, transport_flow_control_,
                                              stream.get());
  EXPECT_EQ(transport_flow_control_.remote_window(),
            chttp2::kDefaultWindow + 1000);
  EXPECT_EQ(stream->GetStreamFlowControl().remote_window_delta(), 1000 + 10000);
}

///////////////////////////////////////////////////////////////////////////////
// Read and Write helper tests

class Http2ReadContextTest : public ::testing::Test {
 protected:
  RefCountedPtr<Party> MakeParty() {
    RefCountedPtr<Arena> arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return Party::Make(std::move(arena));
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

TEST_F(Http2ReadContextTest, WakeWithoutPause) {
  // Test that calling ResumeReadLoopIfPaused before MaybePauseReadLoop has
  // no effect and does not crash.
  ReadLoopPauseRestart read_loop_manager;
  read_loop_manager.ResumeReadLoopIfPaused();
  read_loop_manager.ResumeReadLoopIfPaused();
  read_loop_manager.ResumeReadLoopIfPaused();
  read_loop_manager.MaybePauseReadLoop();
  read_loop_manager.SetPauseReadLoop();
}

class SimulatedTransport : public RefCounted<SimulatedTransport> {
 public:
  auto SimulatedReadAndProcessOneFrame() {
    return [self = this->Ref()]() -> Poll<absl::Status> {
      ++(self->i);
      if (self->i % 2 == 0) {
        // Doing this alternate times to make sure that SetPauseReadLoop is
        // idempotent
        absl::StrAppend(&self->execution_order, "Pause ");
        return self->read_loop_manager.MaybePauseReadLoop();
      }
      absl::StrAppend(&self->execution_order, ". ");
      return absl::OkStatus();
    };
  }
  auto SimulatedReadLoop() {
    return AssertResultType<absl::Status>(Loop([self = this->Ref()]() {
      return TrySeq(self->SimulatedReadAndProcessOneFrame(),
                    [self]() -> LoopCtl<absl::Status> {
                      if (self->i < 10) {
                        absl::StrAppend(&self->execution_order, "SetPause ");
                        self->read_loop_manager.SetPauseReadLoop();
                        return Continue();
                      }
                      absl::StrAppend(&self->execution_order, "EndRead ");
                      self->did_end_read = true;
                      return absl::OkStatus();
                    });
    }));
  }
  auto SimulatedOneWrite() {
    return [self = this->Ref()]() -> Poll<absl::Status> {
      absl::StrAppend(&self->execution_order, "Wake ");
      self->read_loop_manager.ResumeReadLoopIfPaused();
      return absl::OkStatus();
    };
  }

  auto SimulatedWriteLoop() {
    return AssertResultType<absl::Status>(Loop([self = this->Ref()]() {
      return TrySeq(Sleep(Duration::Milliseconds(100)),
                    self->SimulatedOneWrite(),
                    [self]() -> LoopCtl<absl::Status> {
                      if (self->did_end_read) {
                        absl::StrAppend(&self->execution_order, "EndWrite ");
                        return absl::OkStatus();
                      }
                      absl::StrAppend(&self->execution_order, "_ ");
                      return Continue();
                    });
    }));
  }

  std::string execution_order;

 private:
  ReadLoopPauseRestart read_loop_manager;
  int i = 0;
  bool did_end_read = false;
};

TEST_F(Http2ReadContextTest, PauseAndWake) {
  RefCountedPtr<SimulatedTransport> transport =
      MakeRefCounted<SimulatedTransport>();
  ExecCtx ctx;
  RefCountedPtr<Party> party = MakeParty();
  Notification n1;
  Notification n2;
  party->Spawn("Read", transport->SimulatedReadLoop(),
               [&n1](absl::Status status) { n1.Notify(); });
  party->Spawn("Write", transport->SimulatedWriteLoop(),
               [&n2](absl::Status status) { n2.Notify(); });
  n1.WaitForNotification();
  n2.WaitForNotification();
  EXPECT_STREQ(transport->execution_order.c_str(),
               ". SetPause Pause Wake _ . SetPause Pause Wake _ . "
               "SetPause Pause Wake _ . SetPause Pause Wake _ . "
               "SetPause Pause Wake _ . EndRead Wake EndWrite ");
}

TEST_F(Http2ReadContextTest, SetAndGetFrameHeader) {
  // Purpose: Verify that SetCurrentFrameHeader stores header attributes
  // correctly. Assertions: GetCurrentFrameHeader returns the exact frame header
  // that was set.
  util::testing::MockPromiseEndpoint mock_endpoint(1234);
  ReadContext context(/*max_new_streams_per_read_cycle=*/32u,
                      mock_endpoint.promise_endpoint, true,
                      GrpcErrors::kMaxSecurityFrameSize,
                      /*ping_on_rst_stream_percent=*/1u);
  Http2FrameHeader header;
  header.length = 100u;
  header.type = 1u;
  header.flags = 2u;
  header.stream_id = 3u;

  context.SetCurrentFrameHeader(header);
  const Http2FrameHeader& retrieved_header = context.GetCurrentFrameHeader();
  EXPECT_EQ(retrieved_header.length, 100u);
  EXPECT_EQ(retrieved_header.type, 1u);
  EXPECT_EQ(retrieved_header.flags, 2u);
  EXPECT_EQ(retrieved_header.stream_id, 3u);
}

TEST_F(Http2ReadContextTest, ReadCycleFramesLimits) {
  // Verify that MaybePauseReadLoop only pauses when frame limit is reached.
  // Assertions: MaybePauseReadLoop does not pause under limit, but pauses at
  // limit.
  ExecCtx ctx;
  const RefCountedPtr<Party> party = MakeParty();
  bool was_pending_under_limit = false;
  bool was_pending_at_limit = false;
  Notification notification;

  party->Spawn(
      "TestFramesLimits",
      [&was_pending_under_limit,
       &was_pending_at_limit]() -> Poll<absl::Status> {
        util::testing::MockPromiseEndpoint mock_endpoint(1234);
        ReadContext read_context(/*max_new_streams_per_read_cycle=*/32u,
                                 mock_endpoint.promise_endpoint, true,
                                 GrpcErrors::kMaxSecurityFrameSize,
                                 /*ping_on_rst_stream_percent=*/1u);
        const Http2FrameHeader header = {
            0u,  // length
            0u,  // type
            0u,  // flags
            1u   // stream_id
        };

        // Step 1: Set frames strictly under the limit.
        for (uint32_t i = 0u; i < kMaxFramesReadPerReadCycle - 1u; ++i) {
          read_context.SetCurrentFrameHeader(header);
          // Step 2: Verify read loop does not pause under the limit.
          const Poll<absl::Status> poll_under =
              read_context.MaybePauseReadLoop();
          if (poll_under.pending()) {
            was_pending_under_limit = true;
            read_context.ResumeReadLoopIfPaused();
          }
        }

        // Step 3: Set one more frame to hit the limit.
        read_context.SetCurrentFrameHeader(header);
        // Step 4: Verify read loop pauses at the limit.
        const Poll<absl::Status> poll_at = read_context.MaybePauseReadLoop();
        if (poll_at.pending()) {
          was_pending_at_limit = true;
          read_context.ResumeReadLoopIfPaused();
          EXPECT_TRUE(read_context.TestOnlyCheckCounters(0u, 0u, false));
        }
        return absl::OkStatus();
      },
      [&notification](absl::Status status) { notification.Notify(); });

  notification.WaitForNotification();
  EXPECT_FALSE(was_pending_under_limit);
  EXPECT_TRUE(was_pending_at_limit);
}

TEST(Http2CommonTransportTest, TestTarpitDuration) {
  // Verify that TarpitDuration generates random values within bounds across
  // many runs.
  const int min_duration_ms = 10;
  const int max_duration_ms = 30;
  for (int i = 0; i < 1000; ++i) {
    const Duration current_duration =
        TarpitDuration(min_duration_ms, max_duration_ms);
    EXPECT_GE(current_duration, Duration::Milliseconds(min_duration_ms));
    EXPECT_LE(current_duration, Duration::Milliseconds(max_duration_ms));
  }

  // Verify that TarpitDuration returns exactly min when bounds are equal.
  const int exact_duration_ms = 15;
  for (int i = 0; i < 50; ++i) {
    const Duration duration =
        TarpitDuration(exact_duration_ms, exact_duration_ms);
    EXPECT_EQ(duration, Duration::Milliseconds(exact_duration_ms));
  }
}

///////////////////////////////////////////////////////////////////////////////
// TarpitManager Unit Tests

class TarpitManagerTest : public TestsNeedingStreamObjects {
 protected:
  TarpitManagerTest()
      : event_engine_(
            grpc_event_engine::experimental::GetDefaultEventEngine()) {}

  void SetUp() override {
    if (is_client()) {
      GTEST_SKIP() << "Tarpitting is only supported on server-side streams.";
    }
    Init();
  }

  // Initializes the TarpitManager with the provided channel arguments and
  // instantiates the test Party. This should be called at the beginning of each
  // test that interacts with TarpitManager.
  void Init(const ChannelArgs& channel_args = ChannelArgs{}) {
    party_ = MakeParty(event_engine_);
    tarpit_manager_ = std::make_unique<TarpitManager>(channel_args);
  }

  TarpitManager& GetTarpitManager() {
    GRPC_CHECK(tarpit_manager_ != nullptr);
    return *tarpit_manager_;
  }

  const TarpitManager& GetTarpitManager() const {
    GRPC_CHECK(tarpit_manager_ != nullptr);
    return *tarpit_manager_;
  }

  const RefCountedPtr<Party>& GetParty() const {
    GRPC_CHECK(party_ != nullptr);
    return party_;
  }

  // This helper function does the following:
  // 1. Spawns a promise to receive the next message from the TarpitManager.
  // 2. Marks the stream as tarpitted.
  // 3. Verifies the stream's state transitions are as expected.
  // 4. Executes the provided callback to verify the TarpitEntry payload.
  // 5. Marks the stream as tarpit completed.
  // Note: This function mimics transport-level handling of a TarpitEntry.
  void ReceiveAndProcessTarpitEntry(
      const RefCountedPtr<Stream>& stream,
      const absl::FunctionRef<void(TarpitEntry&)> verify) {
    ExecCtx exec_ctx;
    Notification done;
    party_->Spawn("receive_and_process_tarpit_entry",
                  GetTarpitManager().NextMessage(),
                  [this, &stream, &verify, &done](auto result) {
                    EXPECT_TRUE(result.ok());
                    if (result.ok()) {
                      TarpitEntry entry = std::move(*result.value());
                      EXPECT_EQ(entry.GetStreamId(), stream->GetStreamId());

                      const StreamStateChange change =
                          GetTarpitManager().OnTarpit(*stream);
                      ExpectTarpitActive(stream);
                      EXPECT_TRUE(change.reads_became_closed);
                      EXPECT_TRUE(stream->IsClosedForReads());

                      verify(entry);

                      stream->SetTarpitCompleted();
                      ExpectTarpitCompleted(stream);
                    }
                    done.Notify();
                  });
    done.WaitForNotification();
  }

  void AddToQueueOnParty(TarpitEntry entry) {
    ExecCtx exec_ctx;
    Notification done;
    party_->Spawn(
        "add_to_queue",
        [this, entry = std::move(entry)]() mutable {
          GetTarpitManager().AddToQueue(std::move(entry));
          return Empty{};
        },
        [&done](Empty) { done.Notify(); });
    done.WaitForNotification();
  }

  void ShutdownOnParty() {
    ExecCtx exec_ctx;
    Notification done;
    party_->Spawn(
        "shutdown",
        [this]() {
          GetTarpitManager().Shutdown();
          return Empty{};
        },
        [&done](Empty) { done.Notify(); });
    done.WaitForNotification();
  }

  void ExpectNeverTarpitted(const RefCountedPtr<Stream>& stream) const {
    EXPECT_EQ(stream->GetTarpitState(), TarpitState::kNone);
    EXPECT_TRUE(stream->WasNeverTarpitted());
    EXPECT_FALSE(stream->IsTarpitted());
    EXPECT_FALSE(stream->IsTarpitTimerActive());
    EXPECT_FALSE(stream->IsTarpitCompleted());
  }

  void ExpectTarpitActive(const RefCountedPtr<Stream>& stream) const {
    EXPECT_EQ(stream->GetTarpitState(), TarpitState::kActive);
    EXPECT_FALSE(stream->WasNeverTarpitted());
    EXPECT_TRUE(stream->IsTarpitted());
    EXPECT_TRUE(stream->IsTarpitTimerActive());
    EXPECT_FALSE(stream->IsTarpitCompleted());
  }

  void ExpectTarpitCompleted(const RefCountedPtr<Stream>& stream) const {
    EXPECT_EQ(stream->GetTarpitState(), TarpitState::kCompleted);
    EXPECT_FALSE(stream->WasNeverTarpitted());
    EXPECT_TRUE(stream->IsTarpitted());
    EXPECT_FALSE(stream->IsTarpitTimerActive());
    EXPECT_TRUE(stream->IsTarpitCompleted());
  }

  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;
  RefCountedPtr<Party> party_;
  std::unique_ptr<TarpitManager> tarpit_manager_;

 private:
  static RefCountedPtr<Party> MakeParty(
      const std::shared_ptr<grpc_event_engine::experimental::EventEngine>&
          event_engine) {
    RefCountedPtr<Arena> arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine.get());
    return Party::Make(std::move(arena));
  }
};

// Verifies that tarpit is enabled by default and can be disabled via
// GRPC_ARG_HTTP_ALLOW_TARPIT channel argument.
TEST_P(TarpitManagerTest, TarpitManagerEnabledDisabledTest) {
  {
    const ChannelArgs default_args;
    const TarpitManager enabled_manager(default_args);
    EXPECT_TRUE(enabled_manager.allow_tarpit());
  }

  {
    const ChannelArgs disabled_args =
        ChannelArgs().Set(GRPC_ARG_HTTP_ALLOW_TARPIT, false);
    const TarpitManager disabled_manager(disabled_args);
    EXPECT_FALSE(disabled_manager.allow_tarpit());
  }
}

TEST_P(TarpitManagerTest, TarpitManagerRequestCloseStreamTest) {
  std::optional<TarpitEntry::OutgoingResetPayload> outgoing_reset;
  const RefCountedPtr<Stream> stream = CreateMinimalTestStream(1u);
  ExpectNeverTarpitted(stream);

  const StatusFlag status = GetTarpitManager().RequestCloseStream(
      stream->GetStreamId(),
      /*reset_stream_error_code=*/
      static_cast<uint32_t>(Http2ErrorCode::kCancel),
      absl::CancelledError("test status"));
  EXPECT_TRUE(status.ok());

  ReceiveAndProcessTarpitEntry(stream, [&](TarpitEntry& entry) {
    EXPECT_TRUE(entry.IsOutgoingReset());
    EXPECT_FALSE(entry.IsOutgoingTrailingMetadata());
    EXPECT_FALSE(entry.IsIncomingReset());

    outgoing_reset = entry.TakeOutgoingResetPayload();
    ASSERT_TRUE(outgoing_reset.has_value());
    EXPECT_EQ(outgoing_reset->http2_error_code,
              static_cast<uint32_t>(Http2ErrorCode::kCancel));
    EXPECT_EQ(outgoing_reset->trailing_metadata_status,
              absl::CancelledError("test status"));
  });

  // Post-tarpit: OnInitiateReset cancels the call and initiates the stream
  // reset.
  const StreamStateChange reset_change = stream->OnInitiateReset(
      std::move(outgoing_reset->trailing_metadata_status));
  EXPECT_FALSE(reset_change.reads_became_closed);
  EXPECT_FALSE(reset_change.stream_became_closed);
  EXPECT_TRUE(stream->IsClosedForReads());
}

TEST_P(TarpitManagerTest, TarpitManagerStartTarpitTrailersTest) {
  std::optional<TarpitEntry::OutgoingTrailingMetadataPayload> outgoing_trailers;
  const RefCountedPtr<Stream> stream = CreateMinimalTestStream(3u);
  ExpectNeverTarpitted(stream);

  Arena::PoolPtr<ServerMetadata> trailers =
      Arena::MakePooledForOverwrite<ServerMetadata>();
  trailers->Set(GrpcTarPit());
  trailers->Set(GrpcStatusMetadata(), GRPC_STATUS_PERMISSION_DENIED);

  const StatusFlag status = GetTarpitManager().StartTarpitTrailers(
      stream->GetStreamId(), std::move(trailers));
  EXPECT_TRUE(status.ok());

  ReceiveAndProcessTarpitEntry(stream, [&](TarpitEntry& entry) {
    EXPECT_TRUE(entry.IsOutgoingTrailingMetadata());
    EXPECT_FALSE(entry.IsOutgoingReset());
    EXPECT_FALSE(entry.IsIncomingReset());

    outgoing_trailers = entry.TakeOutgoingTrailingMetadataPayload();
    ASSERT_TRUE(outgoing_trailers.has_value());
    EXPECT_NE(outgoing_trailers->metadata, nullptr);
    EXPECT_EQ(outgoing_trailers->metadata->get(GrpcStatusMetadata()),
              GRPC_STATUS_PERMISSION_DENIED);
  });

  // Post-tarpit: EnqueueTrailingMetadata enqueues delayed server trailing
  // metadata on the server.
  const absl::StatusOr<
      StreamDataQueue<ServerMetadataHandle>::StreamWritabilityUpdate>
      enqueue_result = stream->EnqueueTrailingMetadata(
          std::move(outgoing_trailers->metadata));
  EXPECT_TRUE(enqueue_result.ok());
  EXPECT_TRUE(stream->IsClosedForReads());
}

TEST_P(TarpitManagerTest, TarpitManagerIncomingResetTest) {
  std::optional<TarpitEntry::IncomingResetPayload> incoming_reset;
  const RefCountedPtr<Stream> stream = CreateMinimalTestStream(5u);
  ExpectNeverTarpitted(stream);

  const absl::Status incoming_status =
      absl::CancelledError("RST_STREAM received");
  const StatusFlag req_status = GetTarpitManager().RequestTarpitIncomingReset(
      stream->GetStreamId(), incoming_status);
  EXPECT_TRUE(req_status.ok());

  ReceiveAndProcessTarpitEntry(stream, [&](TarpitEntry& entry) {
    EXPECT_TRUE(entry.IsIncomingReset());
    EXPECT_FALSE(entry.IsOutgoingReset());
    EXPECT_FALSE(entry.IsOutgoingTrailingMetadata());

    incoming_reset = entry.TakeIncomingResetPayload();
    ASSERT_TRUE(incoming_reset.has_value());
    EXPECT_EQ(incoming_reset->status, incoming_status);
  });

  // Post-tarpit: OnResetReceived closes the stream.
  const StreamStateChange reset_change =
      stream->OnResetReceived(std::move(incoming_reset->status));
  EXPECT_TRUE(reset_change.stream_became_closed);
  EXPECT_TRUE(stream->IsClosedForWrites());
}

// Verifies ordering and strict expiration (expire_time <= now) behavior of
// TarpitManager queue. Entries configured with future durations remain in the
// queue, and entries whose expiration time has arrived are drained in the order
// of expiration time.
TEST_P(TarpitManagerTest, TarpitManagerOrderingAndDrainTest) {
  const Timestamp future_time = Timestamp::Now() + Duration::Hours(1);
  TarpitEntry entry_future1 = TarpitEntry::CreateOutgoingReset(
      1u, /*http2_error_code=*/0, /*trailing_metadata_status=*/absl::OkStatus(),
      future_time);
  TarpitEntry entry_future2 = TarpitEntry::CreateOutgoingReset(
      3u, /*http2_error_code=*/0, /*trailing_metadata_status=*/absl::OkStatus(),
      future_time);

  GetTarpitManager().AddToQueue(std::move(entry_future1));
  GetTarpitManager().AddToQueue(std::move(entry_future2));

  // Entries have not reached expiration time (expire_time > now).
  const std::vector<TarpitEntry> future_expired =
      GetTarpitManager().OnTimerExpired();
  EXPECT_TRUE(future_expired.empty());

  // Entries with past expiration times.
  const Timestamp now = Timestamp::Now();
  const Timestamp t1 = now - Duration::Seconds(30);
  const Timestamp t2 = now - Duration::Seconds(20);
  const Timestamp t3 = now - Duration::Seconds(10);

  // Add entries out of chronological order (t2, t3, t1).
  GetTarpitManager().AddToQueue(TarpitEntry::CreateOutgoingReset(
      20u, /*http2_error_code=*/0,
      /*trailing_metadata_status=*/absl::OkStatus(), t2));
  GetTarpitManager().AddToQueue(TarpitEntry::CreateOutgoingReset(
      30u, /*http2_error_code=*/0,
      /*trailing_metadata_status=*/absl::OkStatus(), t3));
  GetTarpitManager().AddToQueue(TarpitEntry::CreateOutgoingReset(
      10u, /*http2_error_code=*/0,
      /*trailing_metadata_status=*/absl::OkStatus(), t1));

  // Verify expiration order: t1, t2, t3.
  const std::vector<TarpitEntry> ordered_batch =
      GetTarpitManager().OnTimerExpired();
  ASSERT_EQ(ordered_batch.size(), 3u);
  EXPECT_EQ(ordered_batch[0].GetExpireTime(), t1);
  EXPECT_EQ(ordered_batch[0].GetStreamId(), 10u);
  EXPECT_EQ(ordered_batch[1].GetExpireTime(), t2);
  EXPECT_EQ(ordered_batch[1].GetStreamId(), 20u);
  EXPECT_EQ(ordered_batch[2].GetExpireTime(), t3);
  EXPECT_EQ(ordered_batch[2].GetStreamId(), 30u);

  // Enqueuing and draining multiple expired entries.
  for (uint32_t i = 1u; i <= 150u; ++i) {
    GetTarpitManager().AddToQueue(TarpitEntry::CreateOutgoingReset(
        i, /*http2_error_code=*/0,
        /*trailing_metadata_status=*/absl::OkStatus(), Timestamp::Now()));
  }

  // All expired entries are drained in a single batch.
  const std::vector<TarpitEntry> batch1 = GetTarpitManager().OnTimerExpired();
  EXPECT_EQ(batch1.size(), 150u);

  // No more expired entries remain.
  const std::vector<TarpitEntry> batch2 = GetTarpitManager().OnTimerExpired();
  EXPECT_TRUE(batch2.empty());
}

// Verifies that Shutdown() sets the shutdown flag and closes the receiver.
// Enqueuing new tarpit entries across all three producer methods after
// Shutdown() fails and returns a non-OK status.
TEST_P(TarpitManagerTest, TarpitManagerShutdownTest) {
  const RefCountedPtr<Stream> stream = CreateMinimalTestStream(1u);

  GetTarpitManager().Shutdown();

  EXPECT_FALSE(
      GetTarpitManager()
          .RequestCloseStream(stream->GetStreamId(), 0u, absl::CancelledError())
          .ok());

  Arena::PoolPtr<ServerMetadata> trailers =
      Arena::MakePooledForOverwrite<ServerMetadata>();
  trailers->Set(GrpcStatusMetadata(), GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_FALSE(
      GetTarpitManager()
          .StartTarpitTrailers(stream->GetStreamId(), std::move(trailers))
          .ok());

  EXPECT_FALSE(GetTarpitManager()
                   .RequestTarpitIncomingReset(stream->GetStreamId(),
                                               absl::CancelledError())
                   .ok());
}

// Verifies that WaitForTimerExpire() suspends when queue is empty, resumes when
// an entry is added to the queue via AddToQueue, resolves after the timer sleep
// duration, and cancels with CancelledError when the manager is shut down.
TEST_P(TarpitManagerTest, TarpitManagerWaitForTimerExpireTest) {
  const ChannelArgs short_delay_args =
      ChannelArgs()
          .Set(GRPC_ARG_HTTP_TARPIT_MIN_DURATION_MS, 10)
          .Set(GRPC_ARG_HTTP_TARPIT_MAX_DURATION_MS, 10);
  Init(short_delay_args);

  ExecCtx exec_ctx;

  Notification timer_done;
  GetParty()->Spawn("wait_timer_expire",
                    GetTarpitManager().WaitForTimerExpire(),
                    [&timer_done](const absl::Status status) {
                      EXPECT_TRUE(status.ok());
                      timer_done.Notify();
                    });

  // Enqueue an entry to wake up WaitForTimerExpire.
  const RefCountedPtr<Stream> stream = CreateMinimalTestStream(1u);
  AddToQueueOnParty(TarpitEntry::CreateOutgoingReset(
      stream->GetStreamId(), /*http2_error_code=*/0,
      /*trailing_metadata_status=*/absl::OkStatus(), Timestamp::Now()));

  timer_done.WaitForNotification();

  // Verify WaitForTimerExpire returns CancelledError after Shutdown.
  ShutdownOnParty();
  Notification shutdown_done;
  GetParty()->Spawn("wait_shutdown", GetTarpitManager().WaitForTimerExpire(),
                    [&shutdown_done](const absl::Status status) {
                      EXPECT_EQ(status,
                                absl::CancelledError("TarpitManager shutdown"));
                      shutdown_done.Notify();
                    });
  shutdown_done.WaitForNotification();
}

// Verifies TarpitState forward transitions (kNone -> kActive ->
// kCompleted) and state query accessors.
TEST_P(TarpitManagerTest, StreamTarpitStateTransitionsTest) {
  const RefCountedPtr<Stream> stream = CreateMinimalTestStream(1u);
  ExpectNeverTarpitted(stream);

  stream->SetTarpitActive();
  ExpectTarpitActive(stream);

  stream->SetTarpitCompleted();
  ExpectTarpitCompleted(stream);
}

// Verifies that TarpitManager::ChannelzProperties exports snapshot metrics.
TEST_P(TarpitManagerTest, TarpitManagerChannelzPropertiesTest) {
  const auto GetProp = [](const channelz::PropertyList& props,
                          const absl::string_view key)
      -> std::optional<channelz::PropertyValue> {
    for (const auto& [k, v] : props.property_list()) {
      if (k == key) return v;
    }
    return std::nullopt;
  };

  // Verify ChannelZ properties for an enabled manager with an empty queue.
  const ChannelArgs default_args;
  TarpitManager enabled_manager(default_args);

  const channelz::PropertyList initial_props =
      enabled_manager.ChannelzProperties();
  EXPECT_EQ(std::get<bool>(*GetProp(initial_props, "allow_tarpit")), true);
  EXPECT_EQ(std::get<Duration>(*GetProp(initial_props, "min_tarpit_duration")),
            Duration::Milliseconds(100));
  EXPECT_EQ(std::get<Duration>(*GetProp(initial_props, "max_tarpit_duration")),
            Duration::Seconds(1));
  EXPECT_EQ(std::get<bool>(*GetProp(initial_props, "is_shutdown")), false);
  EXPECT_EQ(std::get<uint64_t>(
                *GetProp(initial_props, "current_tarpitted_streams_count")),
            0u);
  EXPECT_EQ(std::get<bool>(*GetProp(initial_props, "is_queue_empty")), true);
  EXPECT_EQ(GetProp(initial_props, "earliest_expiration_time"), std::nullopt);

  // Also verify ChannelZ properties when tarpitting is disabled via channel
  // args.
  const ChannelArgs disabled_args =
      ChannelArgs().Set(GRPC_ARG_HTTP_ALLOW_TARPIT, false);
  const TarpitManager disabled_manager(disabled_args);
  const channelz::PropertyList disabled_props =
      disabled_manager.ChannelzProperties();
  EXPECT_EQ(std::get<bool>(*GetProp(disabled_props, "allow_tarpit")), false);
  EXPECT_EQ(std::get<uint64_t>(
                *GetProp(disabled_props, "current_tarpitted_streams_count")),
            0u);
  EXPECT_EQ(std::get<bool>(*GetProp(disabled_props, "is_queue_empty")), true);
  EXPECT_EQ(std::get<bool>(*GetProp(disabled_props, "is_shutdown")), false);
  EXPECT_EQ(GetProp(disabled_props, "earliest_expiration_time"), std::nullopt);

  // Step 2: Add entries to the queue and verify updated
  // current_tarpitted_streams_count, is_queue_empty: false, and
  // earliest_expiration_time.
  const Timestamp expire_time1 = Timestamp::Now() + Duration::Seconds(20);
  const Timestamp expire_time2 = Timestamp::Now() + Duration::Seconds(50);
  enabled_manager.AddToQueue(TarpitEntry::CreateOutgoingReset(
      1u, /*http2_error_code=*/0, /*trailing_metadata_status=*/absl::OkStatus(),
      expire_time1));
  enabled_manager.AddToQueue(TarpitEntry::CreateOutgoingReset(
      3u, /*http2_error_code=*/0, /*trailing_metadata_status=*/absl::OkStatus(),
      expire_time2));

  const channelz::PropertyList queued_props =
      enabled_manager.ChannelzProperties();
  EXPECT_EQ(std::get<bool>(*GetProp(queued_props, "allow_tarpit")), true);
  EXPECT_EQ(std::get<bool>(*GetProp(queued_props, "is_shutdown")), false);
  EXPECT_EQ(std::get<uint64_t>(
                *GetProp(queued_props, "current_tarpitted_streams_count")),
            2u);
  EXPECT_EQ(std::get<bool>(*GetProp(queued_props, "is_queue_empty")), false);
  ASSERT_NE(GetProp(queued_props, "earliest_expiration_time"), std::nullopt);
  EXPECT_EQ(
      std::get<Timestamp>(*GetProp(queued_props, "earliest_expiration_time")),
      expire_time1);

  // Step 3: Verify post-shutdown properties
  enabled_manager.Shutdown();
  const channelz::PropertyList shutdown_props =
      enabled_manager.ChannelzProperties();
  EXPECT_EQ(std::get<bool>(*GetProp(shutdown_props, "allow_tarpit")), true);
  EXPECT_EQ(std::get<bool>(*GetProp(shutdown_props, "is_shutdown")), true);
  EXPECT_EQ(std::get<uint64_t>(
                *GetProp(shutdown_props, "current_tarpitted_streams_count")),
            0u);
  EXPECT_EQ(std::get<bool>(*GetProp(shutdown_props, "is_queue_empty")), true);
  EXPECT_EQ(GetProp(shutdown_props, "earliest_expiration_time"), std::nullopt);
}

INSTANTIATE_TEST_SUITE_P(TarpitManagerTest, TarpitManagerTest,
                         ::testing::Bool());

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
