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

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
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
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "test/core/transport/util/transport_test.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using ::testing::MockFunction;
using ::testing::StrictMock;
using transport::testing::Http2FrameTestHelper;
using util::testing::MockPromiseEndpoint;
using util::testing::TransportTest;

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
};

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportObjectCreation) {
  // Event Engine      : FuzzingEventEngine
  // This test asserts :
  // 1. Tests Http2ClientTransport object creation and destruction. The object
  // creation itself begins the ReadLoop and the WriteLoop.
  // 2. Assert if the ReadLoop was invoked correctly or not.
  // 3. Tests trivial functions GetTransportName() , server_transport() and
  // client_transport().

  LOG(INFO) << "TestHttp2ClientTransportObjectCreation Begin";
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

  // Break the ReadLoop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);

  EXPECT_EQ(client_transport->filter_stack_transport(), nullptr);
  EXPECT_NE(client_transport->client_transport(), nullptr);
  EXPECT_EQ(client_transport->server_transport(), nullptr);
  EXPECT_EQ(client_transport->GetTransportName(), "http2");

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  LOG(INFO) << "TestHttp2ClientTransportObjectCreation End";
}

////////////////////////////////////////////////////////////////////////////////
// Basic Transport Write Tests

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportWriteFromQueue) {
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  auto read = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError("Connection closed"), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2DataFrame(
              /*payload=*/"Hello!", /*stream_id=*/9, /*end_stream=*/false),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);

  SliceBuffer buffer;
  AppendGrpcHeaderToSliceBuffer(buffer, 0, 6);
  buffer.Append(SliceBuffer(Slice::FromExternalString("Hello!")));

  Http2Frame frame = Http2DataFrame{/*stream_id=*/9, /*end_stream=*/false,
                                    /*payload=*/std::move(buffer)};

  auto promise =
      client_transport->TestOnlyEnqueueOutgoingFrame(std::move(frame));
  EXPECT_THAT(promise(), IsReady());
  read();

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportWriteFromCall) {
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  std::string data_payload = "Hello!";

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError("Connection closed"), event_engine().get());

  // Expect Client Initial Metadata to be sent.
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),

      },
      event_engine().get());

  mock_endpoint.ExpectWrite(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
           kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
       helper_.EventEngineSliceFromHttp2DataFrame(data_payload,
                                                  /*stream_id=*/1,
                                                  /*end_stream=*/false),
       helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                       /*end_stream=*/true)},
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);
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
        [initiator]() mutable {
          return initiator.Cancel(absl::CancelledError("Cancelled"));
        },
        []() { return absl::OkStatus(); });
  });
  call.initiator.SpawnInfallible(
      "test-wait", [initator = call.initiator, &on_done,
                    read_close = std::move(read_close)]() mutable {
        return Seq(initator.PullServerTrailingMetadata(),
                   [&on_done, read_close = std::move(read_close)](
                       ServerMetadataHandle metadata) mutable {
                     on_done.Call();
                     read_close();
                     return Empty{};
                   });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, Http2ClientTransportAbortTest) {
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError("Connection closed"), event_engine().get());

  // Expect Client Initial Metadata to be sent.
  mock_endpoint.ExpectWrite(
      {
          EventEngineSlice(
              grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
          helper_.EventEngineSliceFromHttp2SettingsFrameDefault(),
      },
      event_engine().get());
  mock_endpoint.ExpectWrite(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
          kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()))},
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);
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
                     read_close();
                     return Empty{};
                   });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

////////////////////////////////////////////////////////////////////////////////
// Ping tests

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingRead) {
  // Simple test to validate a proper ping ack is sent out on receiving a ping
  // request.
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
  mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError("Connection closed"), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                     /*opaque=*/1234),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);

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
        mock_endpoint.ExpectReadClose(
            absl::UnavailableError("Connection closed"), event_engine().get());
      });

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);
  client_transport->TestOnlySpawnPromise(
      "PingRequest", [&client_transport, &ping_ack_received] {
        return Map(TrySeq(client_transport->TestOnlyEnqueueOutgoingFrame(
                              Http2EmptyFrame{}),
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

  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> ping_ack_received;

  // ExpectDelayedReadClose returns a callable. Till this callable is invoked,
  // the ReadLoop is blocked. The reason we need to do this is once the
  // ReadLoop is broken, it would trigger a CloseTransport and the pending
  // asserts would never be satisfied.
  auto read_close = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError("Connection closed"), event_engine().get());
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

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);
  client_transport->TestOnlySpawnPromise("PingRequest", [&client_transport] {
    return Map(TrySeq(client_transport->TestOnlyEnqueueOutgoingFrame(
                          Http2EmptyFrame{}),
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
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> ping_ack_received;
  EXPECT_CALL(ping_ack_received, Call());
  auto ping_complete = std::make_shared<Latch<void>>();

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
        mock_endpoint.ExpectReadClose(
            absl::UnavailableError("Connection closed"), event_engine().get());
      });

  mock_endpoint.ExpectWriteWithCallback(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                     /*opaque=*/0),
      },
      event_engine().get(),
      [event_engine = event_engine().get()](SliceBuffer& out,
                                            SliceBuffer& expect) {
        char out_buffer[kFrameHeaderSize];
        out.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, out_buffer);
        char expect_buffer[kFrameHeaderSize];
        expect.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, expect_buffer);

        EXPECT_STREQ(out_buffer, expect_buffer);
      });

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint),
      GetChannelArgs().Set(GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS, 2),
      event_engine(), nullptr);

  client_transport->TestOnlySpawnPromise(
      "PingRequest", [&client_transport, &ping_ack_received, ping_complete] {
        return Map(TrySeq(
                       client_transport->TestOnlyEnqueueOutgoingFrame(
                           Http2EmptyFrame{}),
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
        return Map(TrySeq(
                       ping_complete->Wait(),
                       [&client_transport] {
                         return client_transport->TestOnlyEnqueueOutgoingFrame(
                             Http2EmptyFrame{});
                       },
                       [&client_transport] {
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
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

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
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
              kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
          helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                          /*end_stream=*/true),
      },
      event_engine().get());

  // Make our mock_enpoint pretend that the peer sent
  // 1. A HEADER frame that contains our initial metadata
  // 2. A DATA frame with END_STREAM flag false.
  // 3. A HEADER frame that contains our trailing metadata.
  mock_endpoint.ExpectRead(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(
           std::string(kPathDemoServiceStep.begin(),
                       kPathDemoServiceStep.end()),
           /*stream_id=*/1,
           /*end_headers=*/true, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Hello", /*stream_id=*/1, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2HeaderFrame(
           // Warning(tjagtap) : This is a hardcoded made up header. This is not
           // what HPack compression would have given. This may break sometime
           // in the future, not sure.
           std::string(kPathDemoServiceStep.begin(),
                       kPathDemoServiceStep.end()),
           /*stream_id=*/1,
           /*end_headers=*/true, /*end_stream=*/true)},
      event_engine().get());

  // We need this to break the ReadLoop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());

  LOG(INFO) << "Creating Http2ClientTransport";
  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine(), nullptr);
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

  call.initiator.SpawnInfallible(
      "test-wait", [initator = call.initiator, &on_done]() mutable {
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
            initator.PullServerTrailingMetadata(),
            [&on_done](std::optional<ServerMetadataHandle> header) {
              EXPECT_TRUE(header.has_value());
              EXPECT_EQ((*header)->DebugString(),
                        ":path: /demo.Service/Step, GrpcStatusFromWire: true");
              on_done.Call();
              LOG(INFO) << "PullServerTrailingMetadata Resolved";
              return Empty{};
            });
      });

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST(Http2CommonTransportTest, TestReadChannelArgs) {
  // Test to validate that ReadChannelArgs reads all the channel args
  // correctly.
  Http2Settings settings;
  ChannelArgs channel_args =
      ChannelArgs()
          .Set(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER, 2048)
          .Set(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES, 1024)
          .Set(GRPC_ARG_HTTP2_MAX_FRAME_SIZE, 16384)
          .Set(GRPC_ARG_EXPERIMENTAL_HTTP2_PREFERRED_CRYPTO_FRAME_SIZE, true)
          .Set(GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY, 1)
          .Set(GRPC_ARG_SECURITY_FRAME_ALLOWED, true);
  ReadSettingsFromChannelArgs(channel_args, settings, /*is_client=*/true);
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

  ReadSettingsFromChannelArgs(ChannelArgs(), settings2, /*is_client=*/true);
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

class SettingsTimeoutManagerTest : public ::testing::Test {
 protected:
  RefCountedPtr<Party> MakeParty() {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return Party::Make(std::move(arena));
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

constexpr uint32_t kSettingsShortTimeout = 300;
constexpr uint32_t kSettingsLongTimeoutTest = 1400;

auto MockStartSettingsTimeout(SettingsTimeoutManager& manager) {
  LOG(INFO) << "MockStartSettingsTimeout Factory";
  return manager.WaitForSettingsTimeout();
}

auto MockSettingsAckReceived(SettingsTimeoutManager& manager) {
  LOG(INFO) << "MockSettingsAckReceived Factory";
  return [&manager]() -> Poll<absl::Status> {
    LOG(INFO) << "MockSettingsAckReceived OnSettingsAckReceived";
    manager.OnSettingsAckReceived();
    return absl::OkStatus();
  };
}

auto MockSettingsAckReceivedDelayed(SettingsTimeoutManager& manager) {
  LOG(INFO) << "MockSettingsAckReceived Factory";
  return TrySeq(Sleep(Duration::Milliseconds(kSettingsShortTimeout * 0.8)),
                [&manager]() -> Poll<absl::Status> {
                  LOG(INFO) << "MockSettingsAckReceived OnSettingsAckReceived";
                  manager.OnSettingsAckReceived();
                  return absl::OkStatus();
                });
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutOneSetting) {
  // First start the timer and then immediately send the ACK
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                              MockSettingsAckReceived(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettings) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettingsDelayed) {
  // Starting the timer and sending the ACK immediately three times in a row.
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceivedDelayed(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutOneSettingRareOrder) {
  // Emulating the case where we receive the ACK before we even spawn the timer.
  // This could happen if our write promise gets blocked on a very large write
  // and the RTT is low and peer responsiveness is high.
  //
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                              MockStartSettingsTimeout(manager)),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettingsRareOrder) {
  // Emulating the case where we receive the ACK before we even spawn the timer.
  // This could happen if our write promise gets blocked on a very large write
  // and the RTT is low and peer responsiveness is high.
  //
  // Check that the status must always be OK.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, NoTimeoutThreeSettingsMixedOrder) {
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(ChannelArgs(),
                             Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification;
  party->Spawn(
      "SettingsTimeoutManagerTest",
      TrySeq(TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockSettingsAckReceived(manager),
                                     MockStartSettingsTimeout(manager)),
             TryJoin<absl::StatusOr>(MockStartSettingsTimeout(manager),
                                     MockSettingsAckReceived(manager))),
      [&notification](absl::StatusOr<std::tuple<Empty, Empty>> status) {
        EXPECT_TRUE(status.ok());
        notification.Notify();
      });
  notification.WaitForNotification();
}

TEST_F(SettingsTimeoutManagerTest, TimeoutOneSetting) {
  // Testing one timeout test
  // Also ensuring that receiving the ACK after the timeout does not crash or
  // leak memory.
  auto party = MakeParty();
  SettingsTimeoutManager manager;
  ExecCtx exec_ctx;
  manager.SetSettingsTimeout(
      ChannelArgs().Set(GRPC_ARG_SETTINGS_TIMEOUT, kSettingsShortTimeout),
      Duration::Milliseconds(kSettingsShortTimeout));
  Notification notification1;
  Notification notification2;
  party->Spawn("SettingsTimeoutManagerTestStart",
               MockStartSettingsTimeout(manager),
               [&notification1](absl::Status status) {
                 EXPECT_TRUE(absl::IsCancelled(status));
                 EXPECT_EQ(status.message(), RFC9113::kSettingsTimeout);
                 notification1.Notify();
               });
  party->Spawn(
      "SettingsTimeoutManagerTestAck",
      TrySeq(Sleep(Duration::Milliseconds(kSettingsLongTimeoutTest)),
             MockSettingsAckReceived(manager)),
      [&notification2](absl::Status status) { notification2.Notify(); });
  notification1.WaitForNotification();
  notification2.WaitForNotification();
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
