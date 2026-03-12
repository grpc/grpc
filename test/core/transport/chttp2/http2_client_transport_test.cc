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
#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/call/call_filters.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/crash.h"
#include "src/core/util/orphanable.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/transport_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace http2 {
namespace testing {

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using ::testing::MockFunction;
using ::testing::StrictMock;

constexpr absl::string_view kConnectionClosed = "Connection closed";
constexpr absl::string_view kPeerString =
    "PeerString: ipv4:127.0.0.1:12345, :path: "
    "/demo.Service/Step, GrpcStatusFromWire: true";

using EventSequenceEndpoint = util::testing::EventSequenceEndpoint;

class Http2ClientTransportTest : public Http2TransportTest {
 public:
  Http2ClientTransportTest() = default;
  // Http2TransportTest is not copyable or movable.
  Http2ClientTransportTest(const Http2ClientTransportTest&) = delete;
  Http2ClientTransportTest& operator=(const Http2ClientTransportTest&) = delete;
  Http2ClientTransportTest(Http2ClientTransportTest&&) = delete;
  Http2ClientTransportTest& operator=(Http2ClientTransportTest&&) = delete;

 protected:
  // Initializes the transport with the given channel args.
  void InitTransport(ChannelArgs channel_args,
                     absl::AnyInvocable<void(absl::StatusOr<uint32_t>)>
                         on_receive_settings = nullptr) {
    client_transport_ = MakeOrphanable<Http2ClientTransport>(
        std::move(endpoint()->promise_endpoint()), std::move(channel_args),
        event_engine(), std::move(on_receive_settings));
  }

  void SpawnTransportLoopsAndExchangeSettings() {
    ExecCtx ctx;
    auto step = endpoint()->NewStep();
    AddTransportStartExpectations(step.get());
    client_transport_->SpawnTransportLoops();
    step->Wait();
    // This tick ensures that settings ack is read by the ReadLoop. This is
    // needed as the ReadLoop stalls after reading the first settings frame and
    // re-starts after sending the first settings ack frame.
    event_engine()->Tick();
  }

  void SpawnTransportLoops() { client_transport_->SpawnTransportLoops(); }

  void AddTransportStartExpectations(EventSequenceEndpoint::Step* step) {
    step->ThenExpectWrite({
        EventEngineSlice(
            grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
        helper_.EventEngineSliceFromHttp2SettingsFrameClientDefault(),
    });

    step->ThenPerformRead(
        {helper_.EventEngineSliceFromHttp2SettingsFrameServerDefault(),
         helper_.EventEngineSliceFromHttp2SettingsFrameAck()});

    step->ThenExpectWrite({
        helper_.EventEngineSliceFromHttp2SettingsFrameAck(),
    });
  }

  void AddTransportCloseExpectations(EventSequenceEndpoint::Step* step) {
    step->ThenFailRead(absl::UnavailableError(kConnectionClosed));
    step->ThenExpectWrite({
        helper_.EventEngineSliceFromHttp2GoawayFrame(
            /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
            /*error_code=*/
            static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
    });
  }

  CallInitiator StartCall(ClientMetadataHandle initial_metadata) {
    auto call = MakeCall(std::move(initial_metadata));
    client_transport_->StartCall(call.handler.StartCall());
    return call.initiator;
  }

  // To allow access to protected members of TransportTest
  Http2ClientTransport* client_transport() const {
    return client_transport_.get();
  }

  OrphanablePtr<Http2ClientTransport> client_transport_;
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
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  EXPECT_EQ(client_transport_->filter_stack_transport(), nullptr);
  EXPECT_NE(client_transport_->client_transport(), nullptr);
  EXPECT_EQ(client_transport_->server_transport(), nullptr);
  EXPECT_EQ(client_transport_->GetTransportName(), "http2");

  std::unique_ptr<channelz::ZTrace> trace =
      client_transport()->GetZTrace("transport_frames");
  EXPECT_NE(trace, nullptr);

  auto socket_node = client_transport()->GetSocketNode();
  EXPECT_NE(socket_node, nullptr);

  auto step = endpoint()->NewStep();

  step->ThenPerformRead(
      {helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Hello!", /*stream_id=*/9, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Bye!", /*stream_id=*/11, /*end_stream=*/true)});
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/RFC9113::kUnknownStreamId, /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kProtocolError)),
  });
  step->Wait();

  // Uncomment this when you want to see ChannelZ Postmortem.
  // FAIL() << "Intentionally failing to display channelz data";

  // The stream object would have been deallocated already.
  // However, we would still have accounting of DATA frame message bytes written
  // in the transport flow control.
  // We did not write a DATA frame with a payload.
  EXPECT_EQ(client_transport()->TestOnlyTransportFlowControlWindow(),
            RFC9113::kHttp2InitialWindowSize);
  LOG(INFO) << "TestHttp2ClientTransportObjectCreation End";
}

////////////////////////////////////////////////////////////////////////////////
// Basic Transport Write Tests

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportWriteFromCall) {
  ExecCtx ctx;
  const std::string data_payload(kString1);

  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Client starts a new stream and sends Initial Metadata, Data frame and
  //    half-closes the stream.
  // 3. Server sends back Trailers-only.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
          kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
  });
  step->ThenExpectWrite(
      {helper_.EventEngineSliceFromHttp2DataFrame(data_payload,
                                                  /*stream_id=*/1,
                                                  /*end_stream=*/true)});

  step->ThenExpectWrite([](SliceBuffer& buffer) {});
  step->ThenPerformRead({helper_.EventEngineSliceFromHttp2HeaderFrame(
      std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
      /*stream_id=*/1,
      /*end_headers=*/true, /*end_stream=*/true)});

  auto initiator = StartCall(TestInitialMetadata());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  initiator.SpawnGuarded("test-send", [initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString(kString1)), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 on_done.Call();
                 return Empty{};
               });
  });

  step->Wait();

  // The stream object would have been deallocated already.
  // However, we would still have accounting of DATA frame message bytes written
  // in the transport flow control.
  // 5 bytes added tois for the gRPC header.
  EXPECT_EQ(
      client_transport()->TestOnlyTransportFlowControlWindow(),
      RFC9113::kHttp2InitialWindowSize - (size_t(data_payload.size()) + 5u));

  // Teardown the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// Ping tests

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingRead) {
  // Simple test to validate a proper ping ack is sent out on receiving a ping
  // request.
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Server sends a ping request.
  // 3. Client sends a ping ack.
  auto step = endpoint()->NewStep();

  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/false,
                                                 /*opaque=*/1234),
  });

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                 /*opaque=*/1234),
  });

  step->Wait();

  // Teardown the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingWrite) {
  // Test to validate  end-to-end ping request and response.
  // This test asserts the following:
  // 1. A ping request is written to the endpoint. The opaque id is not verified
  // while endpoint write as it is an internally generated random number.
  // 2. The ping request promise is resolved once ping ack is received.
  // 3. Redundant acks are ignored.
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Server sends a ping ack for an unknown opaque ID.
  // 3. Client sends a ping request.
  // 4. Server sends a ping ack for the ping request.
  StrictMock<MockFunction<void()>> ping_ack_received;
  EXPECT_CALL(ping_ack_received, Call());

  auto step = endpoint()->NewStep();

  // Redundant ack.
  step->ThenPerformRead(
      {helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                  /*opaque=*/1234)});

  step->ThenExpectWrite([&, step](SliceBuffer& buffer) {
    uint64_t opaque_id =
        VerifyPingFrameAndReturnOpaqueId(buffer, /*is_ack=*/false);
    // Now that we know the opaque ID, we expect the Server (Mock) to send back:
    // 1. Ping Ack with the same opaque ID.
    step->InsertReadAtHead(
        {helper_.EventEngineSliceFromHttp2PingFrame(/*ack=*/true,
                                                    /*opaque=*/opaque_id)});
  });

  client_transport()->TestOnlySpawnPromise("PingRequest", [this,
                                                           &ping_ack_received] {
    return Map(
        TrySeq([&] { return client_transport()->TestOnlyTriggerWriteCycle(); },
               [this] { return client_transport()->TestOnlySendPing([] {}); }),
        [&ping_ack_received](auto) {
          ping_ack_received.Call();
          LOG(INFO) << "PingAck Received. Ping Test done.";
        });
  });
  step->Wait();
  // Tick the event engine to process the Ping Ack.
  event_engine()->Tick();

  // Teardown the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, TestHttp2ClientTransportPingTimeout) {
  // Test to validate that the transport is closed when ping times out.
  // This test asserts the following:
  // 1. The ping request promise is never resolved as there is no ping ack.
  // 2. Transport is closed when ping times out.

  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs().Set("grpc.http2.ping_timeout_ms", 1000));
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Client sends a ping request.
  // 3. Ping timeout occurs and client transport is closed.
  auto step = endpoint()->NewStep();
  step->ThenExpectWrite([&](SliceBuffer& buffer) {
    GRPC_UNUSED uint64_t opaque_id =
        VerifyPingFrameAndReturnOpaqueId(buffer, /*is_ack=*/false);
  });

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/GRPC_CHTTP2_PING_TIMEOUT_STR, /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kRefusedStream)),
  });

  client_transport()->TestOnlySpawnPromise("PingRequest", [&] {
    return Map(
        TrySeq([&] { return client_transport()->TestOnlyTriggerWriteCycle(); },
               [&] { return client_transport()->TestOnlySendPing([] {}); }),
        [](auto) { Crash("Unreachable"); });
  });

  step->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// Header, Data and Continuation Frame Read Tests

TEST_F(Http2ClientTransportTest, TestHeaderDataHeaderFrameOrder) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 1. Client starts a new stream and sends Initial Metadata and half-closes
  //    the stream.
  // 2. Server sends Initial Metadata, Data frame and Trailing Metadata.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(std::string(
          kPathDemoServiceStep.begin(), kPathDemoServiceStep.end())),
  });
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                      /*end_stream=*/true),
  });

  step->ThenPerformRead(
      {helper_.EventEngineSliceFromHttp2HeaderFrame(
           std::string(kPathDemoServiceStep.begin(),
                       kPathDemoServiceStep.end()),
           /*stream_id=*/1,
           /*end_headers=*/true, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2DataFrame(
           /*payload=*/"Hello", /*stream_id=*/1, /*end_stream=*/false),
       helper_.EventEngineSliceFromHttp2HeaderFrame(
           std::string(kPathDemoServiceStep.begin(),
                       kPathDemoServiceStep.end()),
           /*stream_id=*/1,
           /*end_headers=*/true, /*end_stream=*/true)});

  CallInitiator initiator = StartCall(TestInitialMetadata());

  LOG(INFO) << "Client sends HalfClose using FinishSends";
  initiator.SpawnGuarded("test-send", [initiator]() mutable {
    return Seq(
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  initiator.SpawnInfallible("test-wait", [initiator, &on_done, step]() mutable {
    return Seq(
        initiator.PullServerInitialMetadata(),
        [](std::optional<ServerMetadataHandle> header) {
          EXPECT_TRUE(header.has_value());
          EXPECT_EQ((*header)->DebugString(), kPeerString);
          LOG(INFO) << "PullServerInitialMetadata Resolved";
        },
        initiator.PullMessage(),
        [](ServerToClientNextMessage message) {
          EXPECT_TRUE(message.ok());
          EXPECT_TRUE(message.has_value());
          EXPECT_EQ(message.value().payload()->JoinIntoString(), "Hello");
          LOG(INFO) << "PullMessage Resolved";
        },
        initiator.PullServerTrailingMetadata(),
        [&on_done](std::optional<ServerMetadataHandle> header) {
          EXPECT_TRUE(header.has_value());
          EXPECT_EQ((*header)->DebugString(),
                    ":path: /demo.Service/Step, GrpcStatusFromWire: true");
          on_done.Call();
          LOG(INFO) << "PullServerTrailingMetadata Resolved";
          return Empty{};
        });
  });

  step->Wait();
  // Tick the event engine to process the pending read.
  event_engine()->Tick();

  // The stream object would have been deallocated already.
  // However, we would still have accounting of DATA frame message bytes written
  // in the transport flow control.
  // We did not write a DATA frame with a payload.
  EXPECT_EQ(client_transport()->TestOnlyTransportFlowControlWindow(),
            RFC9113::kHttp2InitialWindowSize);

  // Tear down the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, TestCanStreamReceiveDataFrames) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  // 1. Client starts a new stream and sends Initial Metadata.
  // 2. Server sends a data frame on the new stream followed by a GoAway frame.
  // 3. Client closes the stream with a RST_STREAM frame.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/false),
  });

  step->ThenPerformRead({
      helper_.EventEngineSliceFromEmptyHttp2DataFrame(1, false),
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/"kthxbye", /*last_stream_id=*/1,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kNoError)),
  });

  step->ThenExpectWrite({helper_.EventEngineSliceFromHttp2RstStreamFrame(
      /*stream_id=*/1, /*error_code=*/
      static_cast<uint32_t>(Http2ErrorCode::kStreamClosed))});

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/"kthxbye",
          /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(
        initiator.PullServerTrailingMetadata(),
        [&on_done](ServerMetadataHandle metadata) mutable {
          on_done.Call();
          EXPECT_EQ(metadata->get(GrpcStatusMetadata()).value(),
                    GRPC_STATUS_INTERNAL);
          EXPECT_EQ(
              metadata->get_pointer(GrpcMessageMetadata())->as_string_view(),
              "gRPC Error : DATA frames must follow initial "
              "metadata and precede trailing metadata.");
          return Empty{};
        });
  });

  step->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// Close Stream Tests

TEST_F(Http2ClientTransportTest, StreamCleanupTrailingMetadata) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  // 1. Client starts a new stream and sends Initial Metadata.
  // 2. Server sends Trailers-only.
  // 3. Client closes the stream and sends RST_STREAM.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/false),
  });
  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/true),
  });
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromEmptyHttp2DataFrame(/*stream_id=*/1,
                                                      /*end_stream=*/true),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  initiator.SpawnGuarded("wait-for-trailing-metadata", [&]() {
    return Map(initiator.PullServerTrailingMetadata(),
               [&](absl::StatusOr<ServerMetadataHandle> metadata) {
                 EXPECT_TRUE(metadata.ok());
                 EXPECT_EQ(
                     (*metadata)->DebugString(),
                     ":path: /demo.Service/Step, GrpcStatusFromWire: true");
                 on_done.Call();
                 return absl::OkStatus();
               });
  });

  step->Wait();

  // Tear down the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, StreamCleanupTrailingMetadataWithResetStream) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(1);

  // 1. Client starts a new stream and sends Initial Metadata.
  // 2. Server sends Trailers-only and RST_STREAM.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/false),
  });
  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/true),
      helper_.EventEngineSliceFromHttp2RstStreamFrame(),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  initiator.SpawnGuarded("wait-for-trailing-metadata", [&]() {
    return Map(initiator.PullServerTrailingMetadata(),
               [&](absl::StatusOr<ServerMetadataHandle> metadata) {
                 EXPECT_TRUE(metadata.ok());
                 EXPECT_EQ(
                     (*metadata)->DebugString(),
                     ":path: /demo.Service/Step, GrpcStatusFromWire: true");
                 on_done.Call();
                 return absl::OkStatus();
               });
  });

  step->Wait();

  // Tear down the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, StreamCleanupResetStream) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  // 1. Client starts a new stream and sends Initial Metadata.
  // 2. Server sends RST_STREAM.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/false),
  });
  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2RstStreamFrame(),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  initiator.SpawnGuarded("wait-for-trailing-metadata", [&]() {
    return Map(initiator.PullServerTrailingMetadata(),
               [&](absl::StatusOr<ServerMetadataHandle> metadata) {
                 EXPECT_TRUE(metadata.ok());
                 EXPECT_EQ((*metadata)->DebugString(),
                           "grpc-message: Reset stream frame received., "
                           "grpc-status: INTERNAL, GrpcCallWasCancelled: true");
                 on_done.Call();
                 return absl::OkStatus();
               });
  });

  step->Wait();
  // Tick to allow the transport to process the reset stream frame.
  event_engine()->Tick();

  // Tear down the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, Http2ClientTransportStreamAbortTest) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 1. Client starts a new stream and sends Initial Metadata.
  // 2. Client cancels the stream and sends RST_STREAM.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/false),
  });
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2RstStreamFrame(
          /*stream_id=*/1,
          /*error_code=*/static_cast<uint32_t>(http2::Http2ErrorCode::kCancel)),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  initiator.SpawnGuarded("cancel-call", [initiator]() mutable {
    return Seq(
        [initiator]() mutable {
          return initiator.Cancel(absl::CancelledError("CANCELLED"));
        },
        []() { return absl::OkStatus(); });
  });
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 EXPECT_STREQ(metadata->DebugString().c_str(),
                              "grpc-message: CANCELLED, grpc-status: "
                              "CANCELLED, GrpcCallWasCancelled: true");
                 on_done.Call();
                 return Empty{};
               });
  });

  step->Wait();

  // Tear down the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// Close Transport Tests

TEST_F(Http2ClientTransportTest, Http2ClientTransportAbortTest) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 1. Client starts a new stream and sends Initial Metadata.
  // 2. Transport is closed by simulating a failed endpoint read.
  // 3. Client sends GOAWAY and RST_STREAM.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/false),
  });
  step->ThenFailRead(absl::UnavailableError(kConnectionClosed));
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/kConnectionClosed, /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
      helper_.EventEngineSliceFromHttp2RstStreamFrame(
          /*stream_id=*/1, /*error_code=*/static_cast<uint32_t>(
              http2::Http2ErrorCode::kInternalError)),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 EXPECT_STREQ(metadata->DebugString().c_str(),
                              "grpc-message: Connection closed, grpc-status: "
                              "UNAVAILABLE, GrpcCallWasCancelled: true");
                 on_done.Call();
                 return Empty{};
               });
  });

  step->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// Goaway tests

TEST_F(Http2ClientTransportTest, ReadGracefulGoaway) {
  // This test is to verify that the transport closes after closing the last
  // stream when graceful goaway is received.
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();
  std::string data_payload = "Hello!";

  // 1. Client starts a new stream with Initial Metadata and half close.
  // 2. Server sends graceful GOAWAY.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({helper_.EventEngineSliceFromHttp2HeaderFrame(
      std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()))});
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2DataFrame(data_payload,
                                                 /*stream_id=*/1,
                                                 /*end_stream=*/true),
  });

  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          "Graceful GOAWAY", /*last_stream_id=*/1, /*error_code=*/
          Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kNoError)),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  initiator.SpawnGuarded("test-send", [initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 on_done.Call();
                 EXPECT_EQ(
                     metadata->DebugString(),
                     ":path: /demo.Service/Step, GrpcStatusFromWire: true");
                 return Empty{};
               });
  });
  step->Wait();
  // Tick to allow the transport to process the GOAWAY frame.
  event_engine()->Tick();

  // 3. Server sends Trailers-only for the stream.
  // 4. Client closes the stream, closes the transport and sends GOAWAY.
  auto step2 = endpoint()->NewStep();
  step2->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/true),
  });
  step2->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/RFC9113::kLastStreamClosed,
          /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
  });
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, ReadGracefulGoawayCannotStartNewStreams) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  std::string data_payload = "Hello!";
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(2);

  // 1. Client starts a new stream with Initial Metadata and half close.
  // 2. Server sends graceful GOAWAY.
  auto step = endpoint()->NewStep();

  // Check 1: Client sends headers and data
  step->ThenExpectWrite({helper_.EventEngineSliceFromHttp2HeaderFrame(
      std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()))});
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2DataFrame(data_payload,
                                                 /*stream_id=*/1,
                                                 /*end_stream=*/true),
  });

  // After stream 1 is started, server sends a GOAWAY and trailing metadata.
  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          "Graceful GOAWAY",
          /*last_stream_id=*/RFC9113::kMaxStreamId31Bit, /*error_code=*/
          Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kNoError)),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());
  initiator.SpawnGuarded("test-send", [initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 on_done.Call();
                 EXPECT_EQ(
                     metadata->DebugString(),
                     ":path: /demo.Service/Step, GrpcStatusFromWire: true");
                 return Empty{};
               });
  });

  step->Wait();
  // Tick to allow the transport to process the GOAWAY frame.
  event_engine()->Tick();

  // 3. Client attempts to start a new stream and fails.
  auto step2 = endpoint()->NewStep();

  CallInitiator initiator2 = StartCall(TestInitialMetadata());

  initiator2.SpawnGuarded("test-wait-call2", [&on_done, initiator2]() mutable {
    return Seq(
        initiator2.PullServerTrailingMetadata(),
        [&](ServerMetadataHandle metadata) mutable {
          EXPECT_EQ(
              metadata->get_pointer(GrpcMessageMetadata())->as_string_view(),
              "No more stream ids available");
          EXPECT_EQ(metadata->get(GrpcStatusMetadata()).value(),
                    GRPC_STATUS_RESOURCE_EXHAUSTED);
          on_done.Call();
          return absl::OkStatus();
        });
  });
  step2->Wait();

  // 4. Server sends trailers-only for the stream.
  // 5. Client closes the stream, closes the transport and sends GOAWAY.
  auto step3 = endpoint()->NewStep();
  step3->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true, /*end_stream=*/true),
  });
  step3->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/RFC9113::kLastStreamClosed,
          /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
  });
  step3->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// Flow Control Test

TEST_F(Http2ClientTransportTest, TestFlowControlWindow) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Server sends two WINDOW_UPDATE frames.
  auto step = endpoint()->NewStep();

  // Simulate the client receiving two WINDOW_UPDATE frames from the peer.
  step->ThenPerformRead(
      {helper_.EventEngineSliceFromHttp2WindowUpdateFrame(/*stream_id=*/0,
                                                          /*increment=*/1000),
       helper_.EventEngineSliceFromHttp2WindowUpdateFrame(/*stream_id=*/0,
                                                          /*increment=*/500)});

  step->Wait();
  // Tick to allow the transport to process the WINDOW_UPDATE frames.
  event_engine()->Tick();

  EXPECT_TRUE(client_transport()->AreTransportFlowControlTokensAvailable());
  EXPECT_EQ(client_transport()->TestOnlyTransportFlowControlWindow(),
            RFC9113::kHttp2InitialWindowSize + 1000 + 500);

  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// ChannelArg Tests

TEST_F(Http2ClientTransportTest, TestInitialSequenceNumber) {
  // This test verifies the following:
  // 1. The initial sequence number is set to the value passed in the channel
  // args.

  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  constexpr uint32_t kInitialSequenceNumber = 5;
  InitTransport(GetChannelArgs().Set(GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER,
                                     kInitialSequenceNumber));
  SpawnTransportLoopsAndExchangeSettings();
  std::string data_payload = "Hello!";
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  // 2. Client starts a new stream (stream id = kInitialSequenceNumber) with
  //    Initial Metadata and half close.
  // 3. Server sends trailers-only for the stream.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({helper_.EventEngineSliceFromHttp2HeaderFrame(
      std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
      /*stream_id=*/kInitialSequenceNumber, /*end_headers=*/true,
      /*end_stream=*/false)});
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2DataFrame(
          data_payload,
          /*stream_id=*/kInitialSequenceNumber,
          /*end_stream=*/true),
  });

  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/kInitialSequenceNumber,
          /*end_headers=*/true, /*end_stream=*/true),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  initiator.SpawnGuarded("test-send", [initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 on_done.Call();
                 return Empty{};
               });
  });
  step->Wait();
  // Tick to allow the transport to process the header frame.
  event_engine()->Tick();

  // Teardown the transport.
  auto step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ClientTransportTest, TestMaxAllowedStreamId) {
  // This test verifies the following:
  // 1. Any new streams attempted after stream IDs are exhausted will fail with
  // RESOURCE_EXHAUSTED.
  // 2. The transport sends a GOAWAY frame when the last stream closes and no
  // more stream IDs are available.
  // Reads
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  constexpr uint32_t kMaxAllowedStreamId = RFC9113::kMaxStreamId31Bit;
  InitTransport(GetChannelArgs().Set(GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER,
                                     kMaxAllowedStreamId));
  SpawnTransportLoopsAndExchangeSettings();
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call()).Times(2);

  // 2. Client starts a new stream (stream id = kMaxAllowedStreamId) with
  //    Initial Metadata and half close.
  auto step = endpoint()->NewStep();

  step->ThenExpectWrite({helper_.EventEngineSliceFromHttp2HeaderFrame(
      std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
      /*stream_id=*/kMaxAllowedStreamId, /*end_headers=*/true,
      /*end_stream=*/false)});

  CallInitiator initiator = StartCall(TestInitialMetadata());

  initiator.SpawnInfallible("test-wait", [&, initiator]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&](ServerMetadataHandle metadata) mutable {
                 EXPECT_EQ(
                     metadata->DebugString(),
                     ":path: /demo.Service/Step, GrpcStatusFromWire: true");
                 on_done.Call();
                 return Empty{};
               });
  });

  step->Wait();
  // Tick to allow the transport to process the header frame.
  event_engine()->Tick();

  // 3. Client attempts to start a new stream and fails.
  auto step2 = endpoint()->NewStep();
  CallInitiator initiator2 = StartCall(TestInitialMetadata());
  initiator2.SpawnInfallible("test-wait-call2", [&, initiator2]() mutable {
    return Seq(initiator2.PullServerTrailingMetadata(),
               [&](ServerMetadataHandle metadata) mutable {
                 EXPECT_EQ(
                     metadata->DebugString(),
                     "grpc-message: No more stream ids available, grpc-status: "
                     "RESOURCE_EXHAUSTED, GrpcCallWasCancelled: true");
                 on_done.Call();
                 return Empty{};
               });
  });

  step2->Wait();

  // 4. Server sends trailers-only for the stream.
  // 5. Client closes the stream, closes the transport and sends GOAWAY.
  auto step3 = endpoint()->NewStep();
  step3->ThenPerformRead({helper_.EventEngineSliceFromHttp2HeaderFrame(
                              std::string(kPathDemoServiceStep.begin(),
                                          kPathDemoServiceStep.end()),
                              /*stream_id=*/kMaxAllowedStreamId,
                              /*end_headers=*/true, /*end_stream=*/true),
                          helper_.EventEngineSliceFromHttp2RstStreamFrame(
                              /*stream_id=*/kMaxAllowedStreamId)});

  step3->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2GoawayFrame(
          /*debug_data=*/RFC9113::kLastStreamClosed,
          /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
  });
  step3->Wait();
}

///////////////////////////////////////////////////////////////////////////////
// Stream Stall/Unstall Tests

TEST_F(Http2ClientTransportTest,
       TestHttp2ClientStreamWindowUpdateUnblocksStream) {
  // This test asserts that when the peer sends a Stream WINDOW_UPDATE frame,
  // a stream that was stalled due to zero flow control window
  // becomes writable and sends its pending data.

  ExecCtx ctx;
  // 1. Initialize transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Peer reduces the initial window size to 0.
  auto step = endpoint()->NewStep();
  std::vector<Http2SettingsFrame::Setting> settings;
  settings.push_back({Http2Settings::kInitialWindowSizeWireId, 0u});
  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2SettingsFrame(settings),
  });
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2SettingsFrameAck(),
  });
  step->Wait();

  // 3. Client starts call. Sends initial metadata, but NOT data (initial
  //    window size is 0).
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  auto step2 = endpoint()->NewStep();
  step2->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/false),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  initiator.SpawnGuarded("test-send", [initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 on_done.Call();
                 return Empty{};
               });
  });

  step2->Wait();
  event_engine()->Tick();

  // 4. Peer sends WINDOW_UPDATE frame.
  auto step3 = endpoint()->NewStep();
  step3->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2WindowUpdateFrame(
          /*stream_id=*/1, /*increment=*/65535),
  });

  // 5. Client processes the WINDOW_UPDATE frame and sends the stalled DATA
  //    frame.
  step3->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2DataFrame("Hello!",
                                                 /*stream_id=*/1,
                                                 /*end_stream=*/true),
  });

  // 6. Peer sends Trailing metadata.
  step3->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/true),
  });

  step3->Wait();
  // Tick to allow the transport to process the Trailing metadata.
  event_engine()->Tick();

  // Tear down the transport.
  auto step4 = endpoint()->NewStep();
  AddTransportCloseExpectations(step4.get());
  step4->Wait();
}

TEST_F(Http2ClientTransportTest,
       TestHttp2ClientInitialWindowSizeIncreaseUnblocksStreams) {
  // This test asserts that when the peer increases the initial window size,
  // a stream that was stalled due to zero flow control window
  // becomes writable and sends its pending data.

  ExecCtx ctx;
  // 1. Initialize transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Peer reduces the initial window size to 0.
  auto step = endpoint()->NewStep();
  std::vector<Http2SettingsFrame::Setting> settings;
  settings.push_back({Http2Settings::kInitialWindowSizeWireId, 0u});
  step->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2SettingsFrame(settings),
  });
  step->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2SettingsFrameAck(),
  });
  step->Wait();

  // 3. Client starts call. Sends initial metadata, but NOT data (window is 0).
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());

  auto step2 = endpoint()->NewStep();
  step2->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/false),
  });

  CallInitiator initiator = StartCall(TestInitialMetadata());

  initiator.SpawnGuarded("test-send", [initiator]() mutable {
    return Seq(
        initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromExternalString("Hello!")), 0)),
        [initiator = initiator]() mutable { return initiator.FinishSends(); },
        []() { return absl::OkStatus(); });
  });
  initiator.SpawnInfallible("test-wait", [initiator, &on_done]() mutable {
    return Seq(initiator.PullServerTrailingMetadata(),
               [&on_done](ServerMetadataHandle metadata) mutable {
                 on_done.Call();
                 return Empty{};
               });
  });

  step2->Wait();
  event_engine()->Tick();

  // 4. Peer increases the initial window size.
  // 5. Client processes the settings frame and sends the stalled DATA frame.
  auto step3 = endpoint()->NewStep();
  settings.clear();
  settings.push_back({Http2Settings::kInitialWindowSizeWireId, 65535u});
  step3->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2SettingsFrame(settings),
  });

  step3->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2SettingsFrameAck(),
  });

  step3->ThenExpectWrite({
      helper_.EventEngineSliceFromHttp2DataFrame("Hello!",
                                                 /*stream_id=*/1,
                                                 /*end_stream=*/true),
  });

  // 7. Peer sends Trailing metadata.
  step3->ThenPerformRead({
      helper_.EventEngineSliceFromHttp2HeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/true),
  });

  step3->Wait();
  // Tick to allow the transport to process the Trailing metadata.
  event_engine()->Tick();

  // Tear down the transport.
  auto step4 = endpoint()->NewStep();
  AddTransportCloseExpectations(step4.get());
  step4->Wait();
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
