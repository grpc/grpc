// Copyright 2023 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/server_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/status.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/transport/chaotic_good/mock_frame_transport.h"
#include "test/core/transport/util/transport_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

using testing::_;
using testing::MockFunction;
using testing::StrictMock;
using testing::WithArgs;

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using grpc_core::util::testing::TransportTest;

namespace grpc_core {
namespace chaotic_good {
namespace testing {

ServerMetadataHandle TestInitialMetadata() {
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(GrpcMessageMetadata(), Slice::FromStaticString("hello"));
  return md;
}

ServerMetadataHandle TestTrailingMetadata() {
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
  return md;
}

ChannelArgs MakeChannelArgs(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  return CoreConfiguration::Get()
      .channel_args_preconditioning()
      .PreconditionChannelArgs(nullptr)
      .SetObject<grpc_event_engine::experimental::EventEngine>(
          std::move(event_engine));
}

template <typename... PromiseEndpoints>
Config MakeConfig(const ChannelArgs& channel_args,
                  PromiseEndpoints... promise_endpoints) {
  Config config(channel_args);
  auto name_endpoint = [i = 0]() mutable { return absl::StrCat(++i); };
  std::vector<int> this_is_only_here_to_unpack_the_following_statement{
      (config.ServerAddPendingDataEndpoint(
           ImmediateConnection(name_endpoint(), std::move(promise_endpoints))),
       0)...};
  return config;
}

class MockCallDestination : public UnstartedCallDestination {
 public:
  MockCallDestination() : UnstartedCallDestination("MockCallDestination") {}
  ~MockCallDestination() override = default;
  MOCK_METHOD(void, Orphaned, (), (override));
  MOCK_METHOD(void, StartCall, (UnstartedCallHandler unstarted_call_handler),
              (override));
};

class MockServerConnectionFactory : public ServerConnectionFactory {
 public:
  MOCK_METHOD(PendingConnection, RequestDataConnection, (const ChannelArgs&),
              (override));
  void Orphaned() final {}
};

TEST_F(TransportTest, ReadAndWriteOneMessage) {
  auto owned_frame_transport =
      MakeOrphanable<MockFrameTransport>(event_engine());
  auto* frame_transport = owned_frame_transport.get();
  auto call_destination = MakeRefCounted<StrictMock<MockCallDestination>>();
  EXPECT_CALL(*call_destination, Orphaned()).Times(1);
  auto channel_args = MakeChannelArgs(event_engine());
  auto transport = MakeOrphanable<ChaoticGoodServerTransport>(
      channel_args, std::move(owned_frame_transport), MessageChunker(0, 1));
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(*call_destination, StartCall(_))
      .WillOnce(WithArgs<0>([&on_done](
                                UnstartedCallHandler unstarted_call_handler) {
        EXPECT_EQ(unstarted_call_handler.UnprocessedClientInitialMetadata()
                      .get_pointer(HttpPathMetadata())
                      ->as_string_view(),
                  "/demo.Service/Step");
        auto handler = unstarted_call_handler.StartCall();
        handler.SpawnInfallible("test-io", [&on_done, handler]() mutable {
          return Seq(
              handler.PullClientInitialMetadata(),
              [](ValueOrFailure<ClientMetadataHandle> md) {
                EXPECT_TRUE(md.ok());
                EXPECT_EQ(md.value()
                              ->get_pointer(HttpPathMetadata())
                              ->as_string_view(),
                          "/demo.Service/Step");
              },
              [handler]() mutable { return handler.PullMessage(); },
              [](ClientToServerNextMessage msg) {
                EXPECT_TRUE(msg.ok());
                EXPECT_TRUE(msg.has_value());
                EXPECT_EQ(msg.value().payload()->JoinIntoString(), "12345678");
              },
              [handler]() mutable { return handler.PullMessage(); },
              [](ClientToServerNextMessage msg) {
                EXPECT_TRUE(msg.ok());
                EXPECT_FALSE(msg.has_value());
              },
              [handler]() mutable {
                return handler.PushServerInitialMetadata(TestInitialMetadata());
              },
              [handler]() mutable {
                return handler.PushMessage(Arena::MakePooled<Message>(
                    SliceBuffer(Slice::FromCopiedString("87654321")), 0));
              },
              [handler, &on_done]() mutable {
                handler.PushServerTrailingMetadata(TestTrailingMetadata());
                on_done.Call();
              });
        });
      }));
  transport->SetCallDestination(std::move(call_destination));
  frame_transport->ExpectWrite(
      MakeProtoFrame<ServerInitialMetadataFrame>(1, "message: \"hello\""));
  frame_transport->ExpectWrite(MakeMessageFrame(1, "87654321"));
  frame_transport->ExpectWrite(
      MakeProtoFrame<ServerTrailingMetadataFrame>(1, "status: 0"));
  EXPECT_CALL(on_done, Call());
  frame_transport->Read(MakeProtoFrame<ClientInitialMetadataFrame>(
      1, "path: '/demo.Service/Step'"));
  frame_transport->Read(MakeMessageFrame(1, "12345678"));
  frame_transport->Read(ClientEndOfStream(1));
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  transport.reset();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

class MockTcpCallTracer : public TcpCallTracer {
 public:
  MOCK_METHOD(void, RecordEvent,
              (grpc_event_engine::experimental::internal::WriteEvent,
               absl::Time, size_t, const std::vector<TcpEventMetric>&),
              (override));
};

class MockCallTracerInterface : public CallTracerInterface {
 public:
  MOCK_METHOD(void, RecordSendInitialMetadata, (grpc_metadata_batch*),
              (override));
  MOCK_METHOD(void, MutateSendInitialMetadata, (grpc_metadata_batch*),
              (override));
  MOCK_METHOD(void, RecordSendTrailingMetadata, (grpc_metadata_batch*),
              (override));
  MOCK_METHOD(void, MutateSendTrailingMetadata, (grpc_metadata_batch*),
              (override));
  MOCK_METHOD(void, RecordSendMessage, (const Message&), (override));
  MOCK_METHOD(void, RecordSendCompressedMessage, (const Message&), (override));
  MOCK_METHOD(void, RecordReceivedInitialMetadata, (grpc_metadata_batch*),
              (override));
  MOCK_METHOD(void, RecordReceivedMessage, (const Message&), (override));
  MOCK_METHOD(void, RecordReceivedDecompressedMessage, (const Message&),
              (override));
  MOCK_METHOD(void, RecordCancel, (grpc_error_handle), (override));
  MOCK_METHOD(void, RecordIncomingBytes, (const TransportByteSize&),
              (override));
  MOCK_METHOD(void, RecordOutgoingBytes, (const TransportByteSize&),
              (override));
  MOCK_METHOD(std::shared_ptr<TcpCallTracer>, StartNewTcpTrace, (), (override));
  MOCK_METHOD(void, RecordAnnotation, (absl::string_view), (override));
  MOCK_METHOD(void, RecordAnnotation, (const Annotation&), (override));
  MOCK_METHOD(std::string, TraceId, (), (override));
  MOCK_METHOD(std::string, SpanId, (), (override));
  MOCK_METHOD(bool, IsSampled, (), (override));
};

TEST_F(TransportTest, DeferTcpTracerInitialization) {
  auto owned_frame_transport =
      MakeOrphanable<MockFrameTransport>(event_engine());
  auto* frame_transport = owned_frame_transport.get();
  auto call_destination = MakeRefCounted<StrictMock<MockCallDestination>>();
  EXPECT_CALL(*call_destination, Orphaned()).Times(1);
  auto channel_args = MakeChannelArgs(event_engine());
  auto transport = MakeOrphanable<ChaoticGoodServerTransport>(
      channel_args, std::move(owned_frame_transport), MessageChunker(0, 1));

  std::optional<CallHandler> call_handler;
  EXPECT_CALL(*call_destination, StartCall(_))
      .WillOnce(WithArgs<0>(
          [&call_handler](UnstartedCallHandler unstarted_call_handler) {
            call_handler.emplace(unstarted_call_handler.StartCall());
          }));

  transport->SetCallDestination(std::move(call_destination));

  // Trigger call creation on server.
  frame_transport->Read(MakeProtoFrame<ClientInitialMetadataFrame>(
      1, "path: '/demo.Service/Step'"));

  // Now call_handler should be populated.
  ASSERT_TRUE(call_handler.has_value());

  auto mock_tracer_impl =
      std::make_unique<StrictMock<MockCallTracerInterface>>();
  auto mock_tcp_tracer = std::make_shared<StrictMock<MockTcpCallTracer>>();

  EXPECT_CALL(*mock_tracer_impl, IsSampled()).WillOnce(::testing::Return(true));
  EXPECT_CALL(*mock_tracer_impl, StartNewTcpTrace())
      .WillOnce(::testing::Return(mock_tcp_tracer));

  auto* call_tracer =
      call_handler->arena()->New<CallTracer>(mock_tracer_impl.get());

  // Expect writes.
  frame_transport->ExpectWrite(
      MakeProtoFrame<ServerInitialMetadataFrame>(1, "message: \"hello\""));
  frame_transport->ExpectWrite(
      MakeProtoFrame<ServerTrailingMetadataFrame>(1, "status: 0"));

  // Push server metadata to resolve the promise and trigger tracer usage.
  // Set the context immediately before pushing metadata, simulating the timing
  // of an inline filter that executes just prior to metadata transmission.
  call_handler->SpawnInfallible(
      "test-io", [handler = *call_handler, call_tracer]() mutable {
        handler.arena()->SetContext<CallTracer>(call_tracer);
        return Seq(
            [handler]() mutable {
              return handler.PushServerInitialMetadata(TestInitialMetadata());
            },
            [handler]() mutable {
              handler.PushServerTrailingMetadata(TestTrailingMetadata());
              return Empty{};
            });
      });

  event_engine()->TickUntilIdle();
  transport.reset();
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
