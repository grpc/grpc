//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/http2_client_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/grpc.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
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

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using ::testing::MockFunction;
using ::testing::StrictMock;
using transport::testing::Http2FrameTestHelper;
using util::testing::MockPromiseEndpoint;
using util::testing::TransportTest;

constexpr absl::string_view kConnectionClosed = "Connection closed";

static uint64_t Read8b(const uint8_t* input) {
  return static_cast<uint64_t>(input[0]) << 56 |
         static_cast<uint64_t>(input[1]) << 48 |
         static_cast<uint64_t>(input[2]) << 40 |
         static_cast<uint64_t>(input[3]) << 32 |
         static_cast<uint64_t>(input[4]) << 24 |
         static_cast<uint64_t>(input[5]) << 16 |
         static_cast<uint64_t>(input[6]) << 8 | static_cast<uint64_t>(input[7]);
}

static ClientMetadataHandle TestInitialMetadata() {
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
  return md;
}

// Encoded string of header ":path: /demo.Service/Step".
static const std::vector<uint8_t> kPathDemoServiceStep = {
    0x40, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x12, 0x2f,
    0x64, 0x65, 0x6d, 0x6f, 0x2e, 0x53, 0x65, 0x72, 0x76,
    0x69, 0x63, 0x65, 0x2f, 0x53, 0x74, 0x65, 0x70};

class Http2ClientTransportTest : public TransportTest {
 public:
  Http2ClientTransportTest() {
    grpc_tracer_set_enabled("http2_ph2_transport", true);
  }

 protected:
  Http2FrameTestHelper helper_;
  OrphanablePtr<Http2ClientTransport> client_transport_;
  PostMortem postmortem_;
};

////////////////////////////////////////////////////////////////////////////////
// Creation Test

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportObjectCreation) {
  // Event Engine      : FuzzingEventEngine
  // This test asserts :
  // 1. Tests Http2ClientTransport object creation and destruction. The object
  // creation itself begins the ReadLoop and the WriteLoop.
  // 2. Assert if the ReadLoop was invoked correctly or not.
  // 3. Tests trivial functions GetTransportName() , server_transport() and
  // client_transport().

  LOG(INFO) << "TestHttp2ClientTransportObjectCreation Begin";
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectRead(
      {helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Hello!", /*stream_id=*/9, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Bye!", /*stream_id=*/11, /*end_stream=*/true)},
      event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/RFC9113::kUnknownStreamId, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kProtocolError)),
      },
      event_engine().get());

  client_transport_ = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport_->SpawnTransportLoops();

  EXPECT_EQ(client_transport_->filter_stack_transport(), nullptr);
  EXPECT_NE(client_transport_->client_transport(), nullptr);
  EXPECT_EQ(client_transport_->server_transport(), nullptr);
  EXPECT_EQ(client_transport_->GetTransportName(), "http2");

  std::unique_ptr<channelz::ZTrace> trace =
      client_transport_->GetZTrace("transport_frames");
  EXPECT_NE(trace, nullptr);

  auto socket_node = client_transport_->GetSocketNode();
  EXPECT_NE(socket_node, nullptr);

  // Uncomment this when you want to see ChannelZ Postmortem.
  // FAIL() << "Intentionally failing to display channelz data";

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();

  // The stream object would have been deallocated already.
  // However, we would still have accounting of DATA frame message bytes written
  // in the transport flow control.
  // We did not write a DATA frame with a payload.
  EXPECT_EQ(client_transport_->TestOnlyTransportFlowControlWindow(),
            RFC9113::kHttp2InitialWindowSize);
  LOG(INFO) << "TestHttp2ClientTransportObjectCreation End";
}

////////////////////////////////////////////////////////////////////////////////
// Basic Transport Write Tests
TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportWriteFromCall) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  std::string data_payload = "Hello!";

  // Invoking read_close_trailing_metadata will result the ReadLoop to be woken
  // up and the trailing metadata to be received.
  auto read_close_trailing_metadata = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
      },
      event_engine().get());

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close_transport = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  // Expect Client Initial Metadata to be sent.
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());

  mock_endpoint.ExpectWriteWithCallback(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
           kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
       helper_.EventEngineSliceFromHttp2DataFrame(data_payload,
                                                  /*stream_id=*/1,
                                                  /*end_stream=*/false),
       helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                       /*end_stream=*/true)},
      event_engine().get(),
      [read_close_trailing_metadata = std::move(read_close_trailing_metadata)](
          SliceBuffer& out, SliceBuffer& expect) mutable {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_close_trailing_metadata();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();
  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  call.initiator.SpawnInfallible(
      "test-wait",
      [initator = call.initiator, &on_done,
       read_close_transport = std::move(read_close_transport)]() mutable {
        return Seq(
            initator.PullServerTrailingMetadata(),
            [&on_done, read_close_transport = std::move(read_close_transport)](
                ServerMetadataHandle metadata) mutable {
              on_done.Call();
              read_close_transport();
              return Empty{};
            });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();

  // The stream object would have been deallocated already.
  // However, we would still have accounting of DATA frame message bytes written
  // in the transport flow control.
  // "Hello!" is 6 bytes, plus 5 bytes gRPC header = 11 bytes.
  EXPECT_EQ(client_transport->TestOnlyTransportFlowControlWindow(),
            RFC9113::kHttp2InitialWindowSize - 11);
}

////////////////////////////////////////////////////////////////////////////////
// Ping tests

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingRead) {
  // Simple test to validate a proper ping ack is sent out on receiving a ping
  // request.
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());

  mock_endpoint.ExpectRead(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                     /*opaque=*/1234),
      },
      event_engine().get());

  // Break the read loop
  auto read_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                     /*opaque=*/1234),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_close();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingWrite) {
  // Test to validate  end-to-end ping request and response.
  // This test asserts the following:
  // 1. A ping request is written to the endpoint. The opaque id is not verified
  // while endpoint write as it is an internally generated random number.
  // 2. The ping request promise is resolved once ping ack is received.
  // 3. Redundant acks are ignored.
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> ping_ack_received;
  EXPECT_CALL(ping_ack_received, Call());

  // Redundant ping ack
  auto read_cb = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                     /*opaque=*/1234),
      },
      event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                     /*opaque=*/0),
      },
      event_engine().get(),
      [&mock_endpoint, &read_cb, this](SliceBuffer& out, SliceBuffer& expect) {
        char out_buffer[kFrameHeaderSize + 1] = {};
        char expect_buffer[kFrameHeaderSize + 1] = {};
        out.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, out_buffer);
        expect.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, expect_buffer);
        EXPECT_STREQ(out_buffer, expect_buffer);

        auto mutable_slice = out.JoinIntoSlice().TakeMutable();
        uint8_t* opaque_id_ptr = mutable_slice.data();
        uint64_t opaque_id = Read8b(opaque_id_ptr + kFrameHeaderSize);

        read_cb();
        // Ping ack MUST be read after the ping is triggered.
        mock_endpoint.ExpectRead(
            {
                helper_.EventEngineSliceFromHttp2PingFrame(
                    /*ack=*/true,
                    /*opaque=*/opaque_id),
            },
            event_engine().get());

        // Break the read loop
        mock_endpoint.ExpectReadClose(absl::UnavailableError(kConnectionClosed),
                                      event_engine().get());
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();
  client_transport->TestOnlySpawnPromise(
      "PingRequest", [&client_transport, &ping_ack_received] {
        return Map(TrySeq(client_transport->TestOnlyTriggerWriteCycle(),
                          [&client_transport] {
                            return client_transport->TestOnlySendPing([] {});
                          }),
                   [&ping_ack_received](auto) {
                     ping_ack_received.Call();
                     LOG(INFO) << "PingAck Received. Ping Test done.";
                   });
      });
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingTimeout) {
  // Test to validate that the transport is closed when ping times out.
  // This test asserts the following:
  // 1. The ping request promise is never resolved as there is no ping ack.
  // 2. Transport is closed when ping times out.

  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> ping_ack_received;

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                     /*opaque=*/0),
      },
      event_engine().get(), [](SliceBuffer& out, SliceBuffer& expect) {
        char out_buffer[kFrameHeaderSize];
        out.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, out_buffer);
        char expect_buffer[kFrameHeaderSize];
        expect.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, expect_buffer);

        EXPECT_STREQ(out_buffer, expect_buffer);
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/"Ping timeout", /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kRefusedStream)),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint),
      GetChannelArgs().Set("grpc.http2.ping_timeout_ms", 1000), event_engine(),
      /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();
  client_transport->TestOnlySpawnPromise("PingRequest", [&client_transport] {
    return Map(TrySeq(client_transport->TestOnlyTriggerWriteCycle(),
                      [&client_transport] {
                        return client_transport->TestOnlySendPing([] {});
                      }),
               [](auto) { Crash("Unreachable"); });
  });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportMultiplePings) {
  // This test sends 2 ping requests (max_inflight_pings is set to 2) and
  // verifies that one of the ping request is schedulled to honor
  // NextAllowedPingInterval. The second ping request will timeout as there is
  // no ack for it.
  // This test asserts the following:
  // 1. Both the ping requests are written on the endpoint.
  // 2. The first ping request is resolved after the ping ack is received.
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> ping_ack_received;
  EXPECT_CALL(ping_ack_received, Call());
  auto ping_complete = std::make_shared<Latch<void>>();
  absl::AnyInvocable<void()> read_cb_transport_close;

  // Redundant ping ack
  auto read_cb = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                     /*opaque=*/1234),
      },
      event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                     /*opaque=*/0),
      },
      event_engine().get(),
      [&mock_endpoint, &read_cb, this, &read_cb_transport_close](
          SliceBuffer& out, SliceBuffer& expect) {
        char out_buffer[kFrameHeaderSize + 1] = {};
        char expect_buffer[kFrameHeaderSize + 1] = {};
        out.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, out_buffer);
        expect.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, expect_buffer);
        EXPECT_STREQ(out_buffer, expect_buffer);

        auto mutable_slice = out.JoinIntoSlice().TakeMutable();
        auto* opaque_id_ptr = mutable_slice.data();
        uint64_t opaque_id = Read8b(opaque_id_ptr + kFrameHeaderSize);

        read_cb();
        mock_endpoint.ExpectRead(
            {
                helper_.EventEngineSliceFromHttp2PingFrame(
                    /*ack=*/true,
                    /*opaque=*/opaque_id),
            },
            event_engine().get());
        // Break the read loop
        read_cb_transport_close = mock_endpoint.ExpectDelayedReadClose(
            absl::UnavailableError(kConnectionClosed), event_engine().get());
      });

  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                     /*opaque=*/0),
      },
      event_engine().get(),
      [event_engine = event_engine().get(), &read_cb_transport_close](
          SliceBuffer& out, SliceBuffer& expect) {
        char out_buffer[kFrameHeaderSize];
        out.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, out_buffer);
        char expect_buffer[kFrameHeaderSize];
        expect.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, expect_buffer);

        EXPECT_STREQ(out_buffer, expect_buffer);
        read_cb_transport_close();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint),
      GetChannelArgs()
          .Set(GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS, 2)
          .Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  client_transport->TestOnlySpawnPromise(
      "PingRequest", [&client_transport, &ping_ack_received, ping_complete] {
        return Map(TrySeq(
                       client_transport->TestOnlyTriggerWriteCycle(),
                       [&client_transport] {
                         return client_transport->TestOnlySendPing([] {});
                       },
                       [ping_complete]() { ping_complete->Set(); }),
                   [&ping_ack_received](auto) {
                     ping_ack_received.Call();
                     LOG(INFO) << "PingAck Received. Ping Test done.";
                   });
      });
  client_transport->TestOnlySpawnPromise(
      "PingRequest", [&client_transport, ping_complete] {
        return Map(TrySeq(ping_complete->Wait(), Sleep(Duration::Seconds(5)),
                          [&client_transport] {
                            client_transport->TestOnlyTriggerWriteCycle();
                            return client_transport->TestOnlySendPing([] {});
                          }),
                   [](auto) { Crash("Unreachable"); });
      });
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Header, Data and Continuation Frame Read Tests

TEST_F(Http2ClientTransportTest, TestHeaderDataHeaderFrameOrder) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  // Make our mock_enpoint pretend that the peer sent
  // 1. A HEADER frame that contains our initial metadata
  // 2. A DATA frame with END_STREAM flag false.
  // 3. A HEADER frame that contains our trailing metadata.
  auto read_initial_metadata_cb = mock_endpoint.ExpectDelayedRead(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(
           std::string(kPathDemoServiceStep.begin(),
                       kPathDemoServiceStep.end()),
           /*stream_id=*/1,
           /*end_headers=*/true, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Hello", /*stream_id=*/1, /*end_stream=*/false)},
      event_engine().get());

  auto read_trailing_metadata_cb = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
      },
      event_engine().get());
  absl::AnyInvocable<void()> read_cb_transport_close;

  // Send
  // 1. Client Initial Metadata
  // 2. Data frame with END_STREAM flag set.
  // This will put stream in Half Close state.
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
              kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
          helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                          /*end_stream=*/true),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_initial_metadata_cb();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());

  LOG(INFO) << "Creating Http2ClientTransport";
  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();
  LOG(INFO) << "Initiating CallSpine";
  auto call = MakeCall(TestInitialMetadata());

  LOG(INFO) << "Create a stream and send client initial metadata";
  client_transport->StartCall(call.handler.StartCall());

  LOG(INFO) << "Client sends HalfClose using FinishSends";
  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return Seq(
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  call.initiator.SpawnInfallible("test-wait", [initator = call.initiator,
                                               &on_done,
                                               &read_trailing_metadata_cb,
                                               &read_cb_transport_close,
                                               &mock_endpoint, this]() mutable {
    return Seq(
        initator.PullServerInitialMetadata(),
        [](std::optional<ServerMetadataHandle> header) {
          EXPECT_TRUE(header.has_value());
          EXPECT_EQ((*header)->DebugString(),
                    ":path: /demo.Service/Step, GrpcStatusFromWire: true");
          LOG(INFO) << "PullServerInitialMetadata Resolved";
        },
        initator.PullMessage(),
        [](ServerToClientNextMessage message) {
          EXPECT_TRUE(message.ok());
          EXPECT_TRUE(message.has_value());
          EXPECT_EQ(message.value().payload()->JoinIntoString(), "Hello");
          LOG(INFO) << "PullMessage Resolved";
        },
        [&read_trailing_metadata_cb, &read_cb_transport_close, &mock_endpoint,
         this]() mutable {
          read_trailing_metadata_cb();
          read_cb_transport_close = mock_endpoint.ExpectDelayedReadClose(
              absl::UnavailableError(kConnectionClosed), event_engine().get());
        },
        initator.PullServerTrailingMetadata(),
        [&on_done,
         &read_cb_transport_close](std::optional<ServerMetadataHandle> header) {
          EXPECT_TRUE(header.has_value());
          EXPECT_EQ((*header)->DebugString(),
                    ":path: /demo.Service/Step, GrpcStatusFromWire: true");
          on_done.Call();
          read_cb_transport_close();
          LOG(INFO) << "PullServerTrailingMetadata Resolved";
          return Empty{};
        });
  });

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();

  // The stream object would have been deallocated already.
  // However, we would still have accounting of DATA frame message bytes written
  // in the transport flow control.
  // We did not write a DATA frame with a payload.
  EXPECT_EQ(client_transport->TestOnlyTransportFlowControlWindow(),
            RFC9113::kHttp2InitialWindowSize);
}

// TODO(akshitpatel) [PH2][P1] Enable this after fixing bug in Close Path
TEST_F(Http2ClientTransportTest, DISABLED_TestCanStreamReceiveDataFrames) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  auto read_cb = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromEmptyHttp2DataFrame(1, false),
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/"kthxbye", /*last_stream_id=*/1,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kNoError)),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/false),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_cb();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/"kthxbye",
              /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());
  mock_endpoint.ExpectWrite(
      {// This looks wrong. It should have been RST_STREAM with error message
       // helper_.EventEngineSliceFromEmptyHttp2DataFrame(1, true),
       helper_.EventEngineSliceFromHttp2RstStreamFrame(
           /*stream_id=*/1, /*error_code=*/
           static_cast<uint32_t>(Http2ErrorCode::kInternalError))},
      event_engine().get());
  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  auto read_close_transport = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());
  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());
  call.initiator.SpawnInfallible(
      "test-wait",
      [initator = call.initiator, &on_done,
       read_close_transport = std::move(read_close_transport)]() mutable {
        return Seq(
            initator.PullServerTrailingMetadata(),
            [&on_done, read_close_transport = std::move(read_close_transport)](
                ServerMetadataHandle metadata) mutable {
              on_done.Call();
              EXPECT_EQ(metadata->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_INTERNAL);
              EXPECT_EQ(metadata->get_pointer(GrpcMessageMetadata())
                            ->as_string_view(),
                        "gRPC Error : DATA frames must follow initial "
                        "metadata and precede trailing metadata.");
              read_close_transport();
              return Empty{};
            });
      });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Close Stream Tests

TEST_F(Http2ClientTransportTest, StreamCleanupTrailingMetadata) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(2);
  auto read_cb = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
      },
      event_engine().get());
  auto read_cb_transport_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/false),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_cb();
      });

  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                          /*end_stream=*/true),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        on_done.Call();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      nullptr);

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  call.initiator.SpawnGuarded("wait-for-trailing-metadata", [&]() {
    return Map(call.initiator.PullServerTrailingMetadata(),
               [&](absl::StatusOr<ServerMetadataHandle> metadata) {
                 EXPECT_TRUE(metadata.ok());
                 EXPECT_EQ(
                     (*metadata)->DebugString(),
                     ":path: /demo.Service/Step, GrpcStatusFromWire: true");
                 on_done.Call();
                 read_cb_transport_close();
                 return absl::OkStatus();
               });
  });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, StreamCleanupTrailingMetadataWithResetStream) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(1);
  auto read_cb = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
          helper_.EventEngineSliceFromHttp2RstStreamFrame(),
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
          helper_.EventEngineSliceFromHttp2RstStreamFrame(),
      },
      event_engine().get());
  auto read_cb_transport_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/false),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_cb();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      nullptr);
  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  call.initiator.SpawnGuarded("wait-for-trailing-metadata", [&]() {
    return Map(call.initiator.PullServerTrailingMetadata(),
               [&](absl::StatusOr<ServerMetadataHandle> metadata) {
                 EXPECT_TRUE(metadata.ok());
                 EXPECT_EQ(
                     (*metadata)->DebugString(),
                     ":path: /demo.Service/Step, GrpcStatusFromWire: true");
                 on_done.Call();
                 read_cb_transport_close();
                 return absl::OkStatus();
               });
  });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, StreamCleanupResetStream) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  auto read_cb = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2RstStreamFrame(),
          helper_.EventEngineSliceFromHttp2RstStreamFrame(),
      },
      event_engine().get());
  auto read_cb_transport_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/false),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_cb();
      });

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      nullptr);

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  call.initiator.SpawnGuarded("wait-for-trailing-metadata", [&]() {
    return Map(call.initiator.PullServerTrailingMetadata(),
               [&](absl::StatusOr<ServerMetadataHandle> metadata) {
                 EXPECT_TRUE(metadata.ok());
                 EXPECT_EQ((*metadata)->DebugString(),
                           "grpc-message: Reset stream frame received., "
                           "grpc-status: INTERNAL, GrpcCallWasCancelled: true");
                 on_done.Call();
                 read_cb_transport_close();
                 return absl::OkStatus();
               });
  });

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Close Transport Tests

TEST_F(Http2ClientTransportTest, Http2ClientTransportAbortTest) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  // Expect Client Initial Metadata to be sent. We do not expect any writes
  // after the abort. The stream is cancelled while in the IDLE state. The
  // transport will not send a RST_STREAM frame for a stream that has not yet
  // sent headers, as the server would not have created the stream yet.
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      nullptr);

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();
  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  call.initiator.SpawnGuarded(
      "cancel-call", [initiator = call.initiator]() mutable {
        return Seq(
            [initiator]() mutable {
              return initiator.Cancel(absl::CancelledError("CANCELLED"));
            },
            []() { return absl::OkStatus(); });
      });
  call.initiator.SpawnInfallible(
      "test-wait", [initator = call.initiator, &on_done,
                    read_close = std::move(read_close)]() mutable {
        return Seq(initator.PullServerTrailingMetadata(),
                   [&on_done, read_close = std::move(read_close)](
                       ServerMetadataHandle metadata) mutable {
                     EXPECT_STREQ(metadata->DebugString().c_str(),
                                  "grpc-message: CANCELLED, grpc-status: "
                                  "CANCELLED, GrpcCallWasCancelled: true");
                     on_done.Call();
                     std::move(read_close)();
                     return Empty{};
                   });
      });

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Goaway tests

TEST_F(Http2ClientTransportTest, ReadImmediateGoaway) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kProtocolError)),
      },
      event_engine().get());
  mock_endpoint.ExpectRead(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              kConnectionClosed, /*last_stream_id=*/0, /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kProtocolError)),
      },
      event_engine().get());
  mock_endpoint.ExpectReadClose(absl::UnavailableError(kConnectionClosed),
                                event_engine().get());
  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, ReadGracefulGoaway) {
  // This test is to verify that the transport closes after closing the last
  // stream when graceful goaway is received.
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  std::string data_payload = "Hello!";

  // Invoking read_close_trailing_metadata will result the ReadLoop to be woken
  // up and the trailing metadata to be received.
  auto read_close_trailing_metadata = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              "Graceful GOAWAY", /*last_stream_id=*/1, /*error_code=*/
              Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kNoError)),
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
      },
      event_engine().get());

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close_transport = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  // Expect Client Initial Metadata to be sent.
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());

  mock_endpoint.ExpectWriteWithCallback(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
           kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
       helper_.EventEngineSliceFromHttp2DataFrame(data_payload,
                                                  /*stream_id=*/1,
                                                  /*end_stream=*/false),
       helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                       /*end_stream=*/true)},
      event_engine().get(),
      [read_close_trailing_metadata = std::move(read_close_trailing_metadata)](
          SliceBuffer& out, SliceBuffer& expect) mutable {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_close_trailing_metadata();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/"Received GOAWAY frame and no more streams to "
                             "close.",
              /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  call.initiator.SpawnInfallible(
      "test-wait",
      [initator = call.initiator, &on_done,
       read_close_transport = std::move(read_close_transport)]() mutable {
        return Seq(
            initator.PullServerTrailingMetadata(),
            [&on_done, read_close_transport = std::move(read_close_transport)](
                ServerMetadataHandle metadata) mutable {
              on_done.Call();
              EXPECT_EQ(metadata->DebugString(),
                        ":path: /demo.Service/Step, GrpcStatusFromWire: true");
              return Empty{};
            });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, ReadGracefulGoawayCannotStartNewStreams) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  std::string data_payload = "Hello!";
  absl::AnyInvocable<void()> start_new_stream_cb;

  // After stream 1 is started, server sends a GOAWAY and trailing metadata.
  auto read_frames = mock_endpoint.ExpectDelayedRead(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              "Graceful GOAWAY", /*last_stream_id=*/1, /*error_code=*/
              Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kNoError)),
          helper_.EventEngineSliceFromHttp2HeaderFrame(
              std::string(kPathDemoServiceStep.begin(),
                          kPathDemoServiceStep.end()),
              /*stream_id=*/1,
              /*end_headers=*/true, /*end_stream=*/true),
      },
      event_engine().get());

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close_transport = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());

  mock_endpoint.ExpectWriteWithCallback(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
           kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
       helper_.EventEngineSliceFromHttp2DataFrame(data_payload,
                                                  /*stream_id=*/1,
                                                  /*end_stream=*/false),
       helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                       /*end_stream=*/true)},
      event_engine().get(),
      [&, read_frames = std::move(read_frames)](SliceBuffer& out,
                                                SliceBuffer& expect) mutable {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        read_frames();
        start_new_stream_cb();
      });
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/"Received GOAWAY frame and no more streams to "
                             "close.",
              /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), /*on_receive_settings=*/nullptr);
  client_transport->SpawnTransportLoops();

  auto call = MakeCall(TestInitialMetadata());
  start_new_stream_cb = [&]() {
    auto call2 = MakeCall(TestInitialMetadata());
    client_transport->StartCall(call2.handler.StartCall());
    call2.initiator.SpawnGuarded(
        "test-wait-call2", [initiator = call2.initiator]() mutable {
          return Seq(initiator.PullServerTrailingMetadata(),
                     [](ServerMetadataHandle metadata) mutable {
                       EXPECT_EQ(metadata->get(GrpcStatusMetadata()).value(),
                                 GRPC_STATUS_RESOURCE_EXHAUSTED);
                       EXPECT_EQ(metadata->get_pointer(GrpcMessageMetadata())
                                     ->as_string_view(),
                                 "No more stream ids available");
                       return absl::OkStatus();
                     });
        });
  };
  client_transport->StartCall(call.handler.StartCall());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  call.initiator.SpawnInfallible(
      "test-wait",
      [initator = call.initiator, &on_done,
       read_close_transport = std::move(read_close_transport)]() mutable {
        return Seq(
            initator.PullServerTrailingMetadata(),
            [&on_done, read_close_transport = std::move(read_close_transport)](
                ServerMetadataHandle metadata) mutable {
              on_done.Call();
              EXPECT_EQ(metadata->DebugString(),
                        ":path: /demo.Service/Step, GrpcStatusFromWire: true");
              read_close_transport();
              return Empty{};
            });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Flow Control Test

TEST_F(Http2ClientTransportTest, TestFlowControlWindow) {
  ExecCtx ctx;
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  mock_endpoint.ExpectRead(
      {helper_.EventEngineSliceFromHttp2SettingsFrameDefault()},
      event_engine().get());

  // Simulate the client receiving two WINDOW_UPDATE frames from the peer.
  mock_endpoint.ExpectRead(
      {helper_.EventEngineSliceFromHttp2WindowUpdateFrame(/*stream_id=*/0,
                                                          /*increment=*/1000),
       helper_.EventEngineSliceFromHttp2WindowUpdateFrame(/*stream_id=*/0,
                                                          /*increment=*/500)},
      event_engine().get());

  // Break the ReadLoop
  auto read_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError(kConnectionClosed), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());

  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2SettingsFrameAck(),
      },
      event_engine().get(), [&](SliceBuffer& out, SliceBuffer& expect) {
        EXPECT_EQ(out.JoinIntoString(), expect.JoinIntoString());
        std::move(read_close)();
      });

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2GoawayFrame(
              /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
              /*error_code=*/
              static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      },
      nullptr);

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);
  client_transport->SpawnTransportLoops();

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();

  EXPECT_TRUE(client_transport->AreTransportFlowControlTokensAvailable());
  EXPECT_EQ(client_transport->TestOnlyTransportFlowControlWindow(),
            RFC9113::kHttp2InitialWindowSize + 1000 + 500);

  event_engine()->UnsetGlobalHooks();
}

// TODO(tjagtap) : [PH2][P2] Write tests similar to
// TestHeaderDataHeaderFrameOrder for Continuation frame read.

// TODO(tjagtap) : [PH2][P3] Write tests for following failure cases
// 1. Client receives header frame with unknown stream id.
// 2. Client receives DATA frame with unknown stream id.
// 3. Client receives DATA frame when it is waiting for a continuation frame.
// 4. Received 1 initial metadata, and then 1 trailing metadata but trailing
// metadata HEADER frame does not have END_STREAM set.
// 5. Received HEADER frame after half close.
// 6. Received DATA frame after half close.

}  // namespace testing

}  // namespace http2
}  // namespace grpc_core

// TODO(tjagtap) : [PH2][P1] BURNING : Write a test for Settings, and Settings
// Acks, Incoming and Outgoing

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
