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

#include "src/core/ext/transport/chaotic_good/client_transport.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "test/core/transport/chaotic_good/mock_promise_endpoint.h"
#include "test/core/transport/chaotic_good/transport_test.h"

using testing::MockFunction;
using testing::Return;
using testing::StrictMock;

using EventEngineSlice = grpc_event_engine::experimental::Slice;

namespace grpc_core {
namespace chaotic_good {
namespace testing {

// Encoded string of header ":path: /demo.Service/Step".
const uint8_t kPathDemoServiceStep[] = {
    0x40, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x12, 0x2f,
    0x64, 0x65, 0x6d, 0x6f, 0x2e, 0x53, 0x65, 0x72, 0x76,
    0x69, 0x63, 0x65, 0x2f, 0x53, 0x74, 0x65, 0x70};

// Encoded string of trailer "grpc-status: 0".
const uint8_t kGrpcStatus0[] = {0x10, 0x0b, 0x67, 0x72, 0x70, 0x63, 0x2d, 0x73,
                                0x74, 0x61, 0x74, 0x75, 0x73, 0x01, 0x30};

ClientMetadataHandle TestInitialMetadata() {
  auto md =
      GetContext<Arena>()->MakePooled<ClientMetadata>(GetContext<Arena>());
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
  return md;
}

// Send messages from client to server.
auto SendClientToServerMessages(CallInitiator initiator, int num_messages) {
  return Loop([initiator, num_messages, i = 0]() mutable {
    bool has_message = (i < num_messages);
    return If(
        has_message,
        Seq(initiator.PushMessage(GetContext<Arena>()->MakePooled<Message>(
                SliceBuffer(Slice::FromCopiedString(std::to_string(i))), 0)),
            [&i]() -> LoopCtl<absl::Status> {
              ++i;
              return Continue();
            }),
        [initiator]() mutable -> LoopCtl<absl::Status> {
          initiator.FinishSends();
          return absl::OkStatus();
        });
  });
}

ChannelArgs MakeChannelArgs() {
  return CoreConfiguration::Get()
      .channel_args_preconditioning()
      .PreconditionChannelArgs(nullptr);
}

TEST_F(TransportTest, AddOneStream) {
  MockPromiseEndpoint control_endpoint;
  MockPromiseEndpoint data_endpoint;
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kFragment, 7, 1, 26, 8, 56, 15),
       EventEngineSlice::FromCopiedBuffer(kPathDemoServiceStep,
                                          sizeof(kPathDemoServiceStep)),
       EventEngineSlice::FromCopiedBuffer(kGrpcStatus0, sizeof(kGrpcStatus0))},
      event_engine().get());
  data_endpoint.ExpectRead(
      {EventEngineSlice::FromCopiedString("12345678"), Zeros(56)}, nullptr);
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .InSequence(control_endpoint.read_sequence)
      .WillOnce(Return(false));
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint), MakeChannelArgs(),
      event_engine(), HPackParser(), HPackCompressor());
  auto call =
      MakeCall(event_engine().get(), Arena::Create(1024, memory_allocator()));
  transport->StartCall(std::move(call.handler));
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 1, 1,
                             sizeof(kPathDemoServiceStep), 0, 0, 0),
       EventEngineSlice::FromCopiedBuffer(kPathDemoServiceStep,
                                          sizeof(kPathDemoServiceStep))},
      nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 2, 1, 0, 1, 63, 0)},
      nullptr);
  data_endpoint.ExpectWrite(
      {EventEngineSlice::FromCopiedString("0"), Zeros(63)}, nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 4, 1, 0, 0, 0, 0)}, nullptr);
  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 1));
  });
  call.initiator.SpawnInfallible(
      "test-read", [&on_done, initiator = call.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_TRUE(md.ok());
              EXPECT_TRUE(md.value().has_value());
              EXPECT_EQ(md.value()
                            .value()
                            ->get_pointer(HttpPathMetadata())
                            ->as_string_view(),
                        "/demo.Service/Step");
              return Empty{};
            },
            initiator.PullMessage(),
            [](NextResult<MessageHandle> msg) {
              EXPECT_TRUE(msg.has_value());
              EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "12345678");
              return Empty{};
            },
            initiator.PullMessage(),
            [](NextResult<MessageHandle> msg) {
              EXPECT_FALSE(msg.has_value());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(), GRPC_STATUS_OK);
              on_done.Call();
              return Empty{};
            });
      });
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(TransportTest, AddOneStreamMultipleMessages) {
  MockPromiseEndpoint control_endpoint;
  MockPromiseEndpoint data_endpoint;
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kFragment, 3, 1, 26, 8, 56, 0),
       EventEngineSlice::FromCopiedBuffer(kPathDemoServiceStep,
                                          sizeof(kPathDemoServiceStep))},
      event_engine().get());
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kFragment, 6, 1, 0, 8, 56, 15),
       EventEngineSlice::FromCopiedBuffer(kGrpcStatus0, sizeof(kGrpcStatus0))},
      event_engine().get());
  data_endpoint.ExpectRead(
      {EventEngineSlice::FromCopiedString("12345678"), Zeros(56)}, nullptr);
  data_endpoint.ExpectRead(
      {EventEngineSlice::FromCopiedString("87654321"), Zeros(56)}, nullptr);
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .InSequence(control_endpoint.read_sequence)
      .WillOnce(Return(false));
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint), MakeChannelArgs(),
      event_engine(), HPackParser(), HPackCompressor());
  auto call =
      MakeCall(event_engine().get(), Arena::Create(8192, memory_allocator()));
  transport->StartCall(std::move(call.handler));
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 1, 1,
                             sizeof(kPathDemoServiceStep), 0, 0, 0),
       EventEngineSlice::FromCopiedBuffer(kPathDemoServiceStep,
                                          sizeof(kPathDemoServiceStep))},
      nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 2, 1, 0, 1, 63, 0)},
      nullptr);
  data_endpoint.ExpectWrite(
      {EventEngineSlice::FromCopiedString("0"), Zeros(63)}, nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 2, 1, 0, 1, 63, 0)},
      nullptr);
  data_endpoint.ExpectWrite(
      {EventEngineSlice::FromCopiedString("1"), Zeros(63)}, nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 4, 1, 0, 0, 0, 0)}, nullptr);
  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 2));
  });
  call.initiator.SpawnInfallible(
      "test-read", [&on_done, initiator = call.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_TRUE(md.ok());
              EXPECT_TRUE(md.value().has_value());
              EXPECT_EQ(md.value()
                            .value()
                            ->get_pointer(HttpPathMetadata())
                            ->as_string_view(),
                        "/demo.Service/Step");
              return Empty{};
            },
            initiator.PullMessage(),
            [](NextResult<MessageHandle> msg) {
              EXPECT_TRUE(msg.has_value());
              EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "12345678");
              return Empty{};
            },
            initiator.PullMessage(),
            [](NextResult<MessageHandle> msg) {
              EXPECT_TRUE(msg.has_value());
              EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "87654321");
              return Empty{};
            },
            initiator.PullMessage(),
            [](NextResult<MessageHandle> msg) {
              EXPECT_FALSE(msg.has_value());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(), GRPC_STATUS_OK);
              on_done.Call();
              return Empty{};
            });
      });
  // Wait until ClientTransport's internal activities to finish.
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
