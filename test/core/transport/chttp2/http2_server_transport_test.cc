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

#include "src/core/ext/transport/chttp2/transport/http2_server_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_destination.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/crash.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
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
using util::testing::EventSequenceEndpoint;

constexpr absl::string_view kConnectionClosed = "Connection closed";
const std::vector<uint8_t> kGrpcStatusOK = {0x40, 0x0b, 0x67, 0x72, 0x70,
                                            0x63, 0x2d, 0x73, 0x74, 0x61,
                                            0x74, 0x75, 0x73, 0x01, 0x30};
const std::vector<uint8_t> kGrpcStatusCancelled = {
    0x40, 0x0b, 0x67, 0x72, 0x70, 0x63, 0x2d, 0x73, 0x74, 0x61,
    0x74, 0x75, 0x73, 0x01, 0x31, 0x00, 0x0c, 0x67, 0x72, 0x70,
    0x63, 0x2d, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x00};

class Http2ServerTransportTest : public Http2TransportTest {
 public:
  Http2ServerTransportTest() = default;
  // Http2TransportTest is not copyable or movable.
  Http2ServerTransportTest(const Http2ServerTransportTest&) = delete;
  Http2ServerTransportTest& operator=(const Http2ServerTransportTest&) = delete;
  Http2ServerTransportTest(Http2ServerTransportTest&&) = delete;
  Http2ServerTransportTest& operator=(Http2ServerTransportTest&&) = delete;

  void TearDown() override {
    ExecCtx ctx;
    server_transport_.reset();
    Http2TransportTest::TearDown();
  }

 protected:
  // Initializes the transport with the given channel args.
  void InitTransport(ChannelArgs channel_args,
                     absl::AnyInvocable<void(absl::StatusOr<uint32_t>)>
                         on_receive_settings = nullptr) {
    server_transport_ = MakeOrphanable<Http2ServerTransport>(
        std::move(endpoint()->promise_endpoint()), std::move(channel_args),
        event_engine(), std::move(on_receive_settings),
        /*on_close_callback=*/nullptr);
  }

  void SpawnTransportLoopsAndExchangeSettings() {
    ExecCtx ctx;
    std::shared_ptr<EventSequenceEndpoint::Step> step = endpoint()->NewStep();
    server_transport_->SetCallDestination(
        MakeRefCounted<TestCallDestination>(this));
    AddTransportStartExpectations(step.get());
    step->Wait();
    // This tick ensures that settings ack is read by the ReadLoop. This is
    // needed as the ReadLoop stalls after reading the first settings frame and
    // re-starts after sending the first settings ack frame.
    event_engine()->Tick();
  }

  void AddTransportStartExpectations(EventSequenceEndpoint::Step* step) {
    step->ThenPerformRead({
        EventEngineSlice(
            grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)),
        helper_.SerializedDefaultClientSettingsFrame(),
    });

    step->ThenExpectWrite({helper_.SerializedDefaultServerSettingsFrame(),
                           helper_.SerializedSettingsFrameAck()});

    step->ThenPerformRead({
        helper_.SerializedSettingsFrameAck(),
    });
  }

  // To allow access to protected members of TransportTest
  Http2ServerTransport* server_transport() const {
    return server_transport_.get();
  }

  class TestCallDestination : public UnstartedCallDestination {
   public:
    explicit TestCallDestination(Http2ServerTransportTest* test)
        : test_(test) {}
    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      LOG(INFO) << "TestCallDestination::StartCall";
      test_->stream_data_->StartCall(std::move(unstarted_call_handler));
    }
    void Orphaned() override { /*no-op*/ }

   private:
    Http2ServerTransportTest* test_;
  };

  struct StreamDataBase : public RefCounted<StreamDataBase> {
    virtual void StartCall(UnstartedCallHandler unstarted_call_handler) = 0;
  };

  template <typename PromiseFactoryFactory>
  struct StreamData : public StreamDataBase {
    explicit StreamData(PromiseFactoryFactory&& promise_factory_factory)
        : promise_factory_factory(std::move(promise_factory_factory)) {}

    void StartCall(UnstartedCallHandler unstarted_call_handler) override {
      LOG(INFO) << "StreamData::StartCall";
      auto call_handler = unstarted_call_handler.StartCall();
      call_handler.SpawnGuarded(
          "StreamData", std::move(promise_factory_factory(call_handler)));
    }

    PromiseFactoryFactory promise_factory_factory;
  };

  // The underlying promise passed to this function will be polled for the next
  // stream created on the transport. For now, we only support a single stream
  // active at a time.
  template <typename PromiseFactoryFactory>
  void AddStream(PromiseFactoryFactory&& promise_factory_factory) {
    stream_data_ = MakeRefCounted<StreamData<PromiseFactoryFactory>>(
        std::forward<PromiseFactoryFactory>(promise_factory_factory));
  }

  void AddTransportCloseExpectations(EventSequenceEndpoint::Step* step,
                                     uint32_t last_stream_id = 0) {
    step->ThenFailRead(absl::UnavailableError(kConnectionClosed));
    step->ThenExpectWrite({
        helper_.SerializedGoawayFrame(
            /*debug_data=*/kConnectionClosed, last_stream_id,
            /*error_code=*/
            static_cast<uint32_t>(Http2ErrorCode::kInternalError)),
    });
  }

 private:
  OrphanablePtr<Http2ServerTransport> server_transport_;
  RefCountedPtr<StreamDataBase> stream_data_;
};

TEST_F(Http2ServerTransportTest, TestHttp2ServerTransportObjectCreation) {
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  EXPECT_EQ(server_transport()->filter_stack_transport(), nullptr);
  EXPECT_EQ(server_transport()->client_transport(), nullptr);
  EXPECT_NE(server_transport()->server_transport(), nullptr);
  EXPECT_EQ(server_transport()->GetTransportName(), "http2");
  EXPECT_GT(server_transport()->TestOnlyTransportFlowControlWindow(), 0);

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step = endpoint()->NewStep();
  AddTransportCloseExpectations(step.get());
  step->Wait();
}

TEST_F(Http2ServerTransportTest, TestHttp2ServerTransportWriteFromCall) {
  const std::string data_payload(kString1);

  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Client initiates a stream.
  auto step = endpoint()->NewStep();
  auto factory_factory = [](CallHandler call_handler) {
    LOG(INFO) << "New stream created on the server";

    return [call_handler]() mutable {
      return TrySeq(
          call_handler.PullClientInitialMetadata(),
          [call_handler](ClientMetadataHandle metadata) mutable {
            LOG(INFO) << "Client initial metadata: " << metadata->DebugString();
            return call_handler.PushServerInitialMetadata(
                ServerMetadataFromStatus(absl::OkStatus()));
          },
          [call_handler]() mutable {
            MessageHandle message = Arena::MakePooled<Message>(
                SliceBuffer(Slice::FromExternalString(kString1)), 0);
            return call_handler.PushMessage(std::move(message));
          },
          [call_handler]() mutable {
            return call_handler.PushServerTrailingMetadata(
                ServerMetadataFromStatus(absl::CancelledError()));
          });
    };
  };
  AddStream(std::move(factory_factory));

  // Client sends a header frame.
  step->ThenPerformRead({
      helper_.SerializedHeaderFrame(std::string(kPathDemoServiceStep.begin(),
                                                kPathDemoServiceStep.end())),
  });

  step->ThenExpectWrite(
      {helper_.SerializedHeaderFrame(
           std::string(kGrpcStatusOK.begin(), kGrpcStatusOK.end()),
           /*stream_id=*/1, /*end_headers=*/true, /*end_stream=*/false),
       helper_.SerializedDataFrame(
           std::string(kString1.begin(), kString1.end()),
           /*stream_id=*/1, /*end_stream=*/false),
       helper_.SerializedHeaderFrame(std::string(kGrpcStatusCancelled.begin(),
                                                 kGrpcStatusCancelled.end()),
                                     /*stream_id=*/1, /*end_headers=*/true,
                                     /*end_stream=*/true),
       helper_.SerializedResetStreamFrame(
           /*stream_id=*/1,
           /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kNoError))});
  step->Wait();

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get(), /*last_stream_id=*/1);
  step2->Wait();
}

TEST_F(Http2ServerTransportTest, ClientInitiatedCancellationTest) {
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();
  StrictMock<MockFunction<void(bool)>> on_done;
  EXPECT_CALL(on_done, Call(true));

  auto factory_factory = [&on_done](CallHandler call_handler) {
    return [call_handler, &on_done]() mutable {
      return TrySeq(
          call_handler.PullClientInitialMetadata(),
          [call_handler, &on_done](ClientMetadataHandle metadata) mutable {
            return Map(call_handler.WasCancelled(),
                       [&on_done](bool cancelled) -> absl::Status {
                         on_done.Call(cancelled);
                         return absl::OkStatus();
                       });
          });
    };
  };
  AddStream(std::move(factory_factory));

  auto step = endpoint()->NewStep();
  // Client sends a header frame to start stream 1.
  step->ThenPerformRead({
      helper_.SerializedHeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true,
          /*end_stream=*/false),
  });
  // Client sends RST_STREAM.
  step->ThenPerformRead({
      helper_.SerializedResetStreamFrame(
          /*stream_id=*/1,
          /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kCancel)),
  });
  step->Wait();
  // Tick to allow the server transport to process the RST_STREAM frame and
  // cancel the stream.
  event_engine()->Tick();

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get(), /*last_stream_id=*/1);
  step2->Wait();
}

TEST_F(Http2ServerTransportTest, ServerApplicationInitiatedCancellationTest) {
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  auto factory_factory = [](CallHandler call_handler) {
    return [call_handler]() mutable {
      return TrySeq(call_handler.PullClientInitialMetadata(),
                    [call_handler](ClientMetadataHandle metadata) mutable {
                      call_handler.PushServerTrailingMetadata(
                          ServerMetadataFromStatus(absl::CancelledError()));
                      return absl::OkStatus();
                    });
    };
  };
  AddStream(std::move(factory_factory));

  auto step = endpoint()->NewStep();
  // Client sends a header frame to start stream 1.
  step->ThenPerformRead({
      helper_.SerializedHeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true,
          /*end_stream=*/false),
  });

  // We expect Trailers-Only with CANCELLED status.
  // And we ALSO expect RST_STREAM(NO_ERROR) because trailing metadata is
  // followed by RST_STREAM.
  step->ThenExpectWrite(
      {helper_.SerializedHeaderFrame(std::string(kGrpcStatusCancelled.begin(),
                                                 kGrpcStatusCancelled.end()),
                                     /*stream_id=*/1, /*end_headers=*/true,
                                     /*end_stream=*/true),
       helper_.SerializedResetStreamFrame(
           /*stream_id=*/1,
           /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kNoError))});

  step->Wait();

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get(), /*last_stream_id=*/1);
  step2->Wait();
}

TEST_F(Http2ServerTransportTest, ServerApplicationErrorTest) {
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  auto factory_factory = [](CallHandler call_handler) {
    return [call_handler]() mutable {
      return TrySeq(call_handler.PullClientInitialMetadata(),
                    [](ClientMetadataHandle metadata) -> absl::Status {
                      // Server application fails with error.
                      return absl::CancelledError();
                    });
    };
  };
  AddStream(std::move(factory_factory));

  auto step = endpoint()->NewStep();
  // Client sends a header frame to start stream 1.
  step->ThenPerformRead({
      helper_.SerializedHeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true,
          /*end_stream=*/false),
  });

  // We expect Trailers-Only with CANCELLED status.
  // And we ALSO expect RST_STREAM(NO_ERROR) because trailing metadata is
  // followed by RST_STREAM.
  step->ThenExpectWrite(
      {helper_.SerializedHeaderFrame(std::string(kGrpcStatusCancelled.begin(),
                                                 kGrpcStatusCancelled.end()),
                                     /*stream_id=*/1, /*end_headers=*/true,
                                     /*end_stream=*/true),
       helper_.SerializedResetStreamFrame(
           /*stream_id=*/1,
           /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kNoError))});

  step->Wait();

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get(), /*last_stream_id=*/1);
  step2->Wait();
}

// TODO(tjagtap): [PH2][P0] Re-enable this test once the bug is fixed.
TEST_F(Http2ServerTransportTest,
       DISABLED_ServerApplicationErrorClientHalfClosedTest) {
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  auto factory_factory = [](CallHandler call_handler) {
    return [call_handler]() mutable {
      return TrySeq(call_handler.PullClientInitialMetadata(),
                    [](ClientMetadataHandle metadata) -> absl::Status {
                      // Server application fails with error.
                      return absl::CancelledError();
                    });
    };
  };
  AddStream(std::move(factory_factory));

  auto step = endpoint()->NewStep();
  // Client sends a header frame to start stream 1, and half-closes.
  step->ThenPerformRead({
      helper_.SerializedHeaderFrame(
          std::string(kPathDemoServiceStep.begin(), kPathDemoServiceStep.end()),
          /*stream_id=*/1,
          /*end_headers=*/true,
          /*end_stream=*/true),
  });

  // We expect Trailers-Only with CANCELLED status.
  // And we ALSO expect RST_STREAM(NO_ERROR) because trailing metadata is
  // followed by RST_STREAM, even if the client already half-closed.
  step->ThenExpectWrite(
      {helper_.SerializedHeaderFrame(std::string(kGrpcStatusCancelled.begin(),
                                                 kGrpcStatusCancelled.end()),
                                     /*stream_id=*/1, /*end_headers=*/true,
                                     /*end_stream=*/true),
       helper_.SerializedResetStreamFrame(
           /*stream_id=*/1,
           /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kNoError))});

  step->Wait();

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get(), /*last_stream_id=*/1);
  step2->Wait();
}

////////////////////////////////////////////////////////////////////////////////
// Ping tests

TEST_F(Http2ServerTransportTest, TestHttp2ServerTransportPingRead) {
  // Simple test to validate a proper ping ack is sent out on receiving a ping
  // request.
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Client sends a ping request.
  // 3. Server sends a ping ack.
  std::shared_ptr<EventSequenceEndpoint::Step> step = endpoint()->NewStep();

  step->ThenPerformRead({
      helper_.SerializedPingFrame(/*ack=*/false,
                                  /*opaque=*/1234),
  });

  step->ThenExpectWrite({
      helper_.SerializedPingFrame(/*ack=*/true,
                                  /*opaque=*/1234),
  });

  step->Wait();

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ServerTransportTest, TestHttp2ServerTransportPingWrite) {
  // Test to validate end-to-end ping request and response.
  // This test asserts the following:
  // 1. A ping request is written to the endpoint. The opaque id is not verified
  // while endpoint write as it is an internally generated random number.
  // 2. The ping request promise is resolved once ping ack is received.
  // 3. Redundant acks are ignored.
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Client sends a ping ack for an unknown opaque ID.
  // 3. Server sends a ping request.
  // 4. Client sends a ping ack for the ping request.
  StrictMock<MockFunction<void()>> ping_ack_received;
  EXPECT_CALL(ping_ack_received, Call());

  std::shared_ptr<EventSequenceEndpoint::Step> step = endpoint()->NewStep();

  // Redundant ack.
  step->ThenPerformRead({helper_.SerializedPingFrame(/*ack=*/true,
                                                     /*opaque=*/1234)});

  step->ThenExpectWrite([&, step](SliceBuffer& buffer) {
    uint64_t opaque_id =
        VerifyPingFrameAndReturnOpaqueId(buffer, /*is_ack=*/false);
    // Now that we know the opaque ID, we expect the Client (Mock) to send back:
    // 1. Ping Ack with the same opaque ID.
    step->InsertReadAtHead({helper_.SerializedPingFrame(/*ack=*/true,
                                                        /*opaque=*/opaque_id)});
  });

  server_transport()->TestOnlySpawnPromise("PingRequest", [this,
                                                           &ping_ack_received] {
    return Map(
        TrySeq([&] { return server_transport()->TestOnlyTriggerWriteCycle(); },
               [this] { return server_transport()->TestOnlySendPing([] {}); }),
        [&ping_ack_received](auto) {
          ping_ack_received.Call();
          LOG(INFO) << "PingAck Received. Ping Test done.";
        });
  });
  step->Wait();
  // Tick the event engine to process the Ping Ack.
  event_engine()->Tick();

  // Teardown the transport.
  std::shared_ptr<EventSequenceEndpoint::Step> step2 = endpoint()->NewStep();
  AddTransportCloseExpectations(step2.get());
  step2->Wait();
}

TEST_F(Http2ServerTransportTest, TestHttp2ServerTransportPingTimeout) {
  // Test to validate that the transport is closed when ping times out.
  // This test asserts the following:
  // 1. The ping request promise is never resolved as there is no ping ack.
  // 2. Transport is closed when ping times out.

  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs().Set("grpc.http2.ping_timeout_ms", 1000));
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Server sends a ping request.
  // 3. Ping timeout occurs and server transport is closed.
  std::shared_ptr<EventSequenceEndpoint::Step> step = endpoint()->NewStep();
  step->ThenExpectWrite([&](SliceBuffer& buffer) {
    GRPC_UNUSED uint64_t opaque_id =
        VerifyPingFrameAndReturnOpaqueId(buffer, /*is_ack=*/false);
  });

  step->ThenExpectWrite({
      helper_.SerializedGoawayFrame(
          /*debug_data=*/GRPC_CHTTP2_PING_TIMEOUT_STR, /*last_stream_id=*/0,
          /*error_code=*/
          static_cast<uint32_t>(Http2ErrorCode::kRefusedStream)),
  });

  server_transport()->TestOnlySpawnPromise("PingRequest", [&] {
    return Map(
        TrySeq([&] { return server_transport()->TestOnlyTriggerWriteCycle(); },
               [&] { return server_transport()->TestOnlySendPing([] {}); }),
        [](auto) { Crash("Unreachable"); });
  });

  step->Wait();
}

TEST_F(Http2ServerTransportTest, TestServerInitiatedGoAway) {
  ExecCtx ctx;
  // 1. Initialize the transport and exchange settings.
  InitTransport(GetChannelArgs());
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Expect a GOAWAY frame to be written and socket closed.
  auto step = endpoint()->NewStep();
  step->ThenExpectWrite({
      helper_.SerializedGoawayFrame(
          /*debug_data=*/"Server is stopping to serve requests.",
          /*last_stream_id=*/0,
          /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kRefusedStream)),
  });

  // 3. Trigger server-initiated GOAWAY by calling PerformOp.
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->disconnect_with_error =
      absl::UnavailableError("Server is stopping to serve requests.");
  server_transport()->PerformOp(op);

  // 4. Wait for the GOAWAY frame to be written.
  step->Wait();
}

TEST_F(Http2ServerTransportTest, TestKeepAliveTimeout) {
  // Test to validate that the transport is closed when keepalive ping times
  // out. This test asserts the following:
  // 1. Keepalive ping is sent after keepalive_time.
  // 2. Transport is closed when keepalive ping times out.
  // 3. GOAWAY frame with kRefusedStream and GRPC_CHTTP2_KEEPALIVE_TIMEOUT_STR
  // is written.
  ExecCtx ctx;

  // 1. Initialize transport with short keepalive time and timeout, and permit
  // keepalive without calls.
  InitTransport(GetChannelArgs()
                    .Set(GRPC_ARG_KEEPALIVE_TIME_MS, 10)
                    .Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10)
                    .Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, true));
  SpawnTransportLoopsAndExchangeSettings();

  // 2. Expect the server to send a Keepalive Ping.
  auto step1 = endpoint()->NewStep();
  step1->ThenExpectWrite([&](SliceBuffer& buffer) {
    GRPC_UNUSED uint64_t opaque_id =
        VerifyPingFrameAndReturnOpaqueId(buffer, /*is_ack=*/false);
  });
  step1->Wait();

  // 3. Expect the server to write a GOAWAY frame with kRefusedStream and
  // Keepalive Timeout message, and close the socket.
  auto step2 = endpoint()->NewStep();
  step2->ThenExpectWrite({
      helper_.SerializedGoawayFrame(
          /*debug_data=*/GRPC_CHTTP2_KEEPALIVE_TIMEOUT_STR,
          /*last_stream_id=*/0,
          /*error_code=*/static_cast<uint32_t>(Http2ErrorCode::kRefusedStream)),
  });

  step2->Wait();
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
