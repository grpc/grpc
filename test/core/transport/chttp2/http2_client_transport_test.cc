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
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/util/orphanable.h"
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
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
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
      event_engine());

  EXPECT_EQ(client_transport->filter_stack_transport(), nullptr);
  EXPECT_NE(client_transport->client_transport(), nullptr);
  EXPECT_EQ(client_transport->server_transport(), nullptr);
  EXPECT_EQ(client_transport->GetTransportName(), "http2");

  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
  LOG(INFO) << "TestHttp2ClientTransportObjectCreation End";
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportWriteFromQueue) {
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  auto read = mock_endpoint.ExpectDelayedReadClose(
      absl::UnavailableError("Connection closed"), event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
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
      event_engine());

  Http2Frame frame = Http2DataFrame{
      .stream_id = 9,
      .end_stream = false,
      .payload = SliceBuffer(Slice::FromExternalString("Hello!"))};

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
  SliceBuffer grpc_header;
  std::string data_payload = "Hello!";
  AppendGrpcHeaderToSliceBuffer(grpc_header, 0, data_payload.size());

  // Break the ReadLoop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
      },
      event_engine().get());

  // Expect Client Initial Metadata to be sent.
  mock_endpoint.ExpectWrite(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
           kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/(grpc_header.JoinIntoString() + data_payload),
           /*stream_id=*/1, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"", /*stream_id=*/1, /*end_stream=*/true)},
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine());
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
      "test-wait", [initator = call.initiator, &on_done]() mutable {
        return Seq(initator.PullServerTrailingMetadata(),
                   [&on_done](ServerMetadataHandle metadata) {
                     on_done.Call();
                     return Empty{};
                   });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(Http2ClientTransportTest, Http2ClientTransportAbortTest) {
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  SliceBuffer grpc_header;

  // Break the ReadLoop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
      },
      event_engine().get());

  // Expect Client Initial Metadata to be sent.
  mock_endpoint.ExpectWrite(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
          kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()))},
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine());
  auto call = MakeCall(TestInitialMetadata());
  client_transport->StartCall(call.handler.StartCall());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  call.initiator.SpawnGuarded(
      "cancel-call", [initiator = call.initiator]() mutable {
        return Seq(
            [initiator]() mutable {
              return initiator.Cancel(absl::CancelledError("Cancelled"));
            },
            []() { return absl::OkStatus(); });
      });
  call.initiator.SpawnInfallible(
      "test-wait", [initator = call.initiator, &on_done]() mutable {
        return Seq(initator.PullServerTrailingMetadata(),
                   [&on_done](ServerMetadataHandle metadata) {
                     on_done.Call();
                     return Empty{};
                   });
      });
  // Wait for Http2ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

// Ping tests
TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingRead) {
  // Simple test to validate a proper ping ack is sent out on receiving a ping
  // request.
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);

  mock_endpoint.ExpectRead(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                     /*opaque=*/1234),
      },
      event_engine().get());

  // Break the read loop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());

  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
      },
      event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                     /*opaque=*/1234),
      },
      event_engine().get());

  auto client_transport = MakeOrphanable<Http2ClientTransport>(
      std::move(mock_endpoint.promise_endpoint), GetChannelArgs(),
      event_engine());

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
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
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
      event_engine());
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

  // TODO(akshitpatel)[P1] : CloseTransport is not yet implemented, and hence
  // read loop is broken for the test to finish. Once close transport is
  // implemented, read loop should not be explicitly broken.
  MockPromiseEndpoint mock_endpoint(/*port=*/1000);
  StrictMock<MockFunction<void()>> ping_ack_received;

  // Break the read loop
  mock_endpoint.ExpectReadClose(absl::UnavailableError("Connection closed"),
                                event_engine().get());
  mock_endpoint.ExpectWrite(
      {
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
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
      event_engine());
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
          helper_.EventEngineSliceFromHttp2SettingsFrame({{4, 65535}}),
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
      event_engine());

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
