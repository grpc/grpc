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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/status.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/transport/chaotic_good/mock_frame_transport.h"
#include "test/core/transport/chaotic_good/transport_test_helper.h"
#include "test/core/transport/util/mock_promise_endpoint.h"
#include "test/core/transport/util/transport_test.h"

using testing::MockFunction;
using testing::StrictMock;

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using grpc_core::util::testing::TransportTest;

namespace grpc_core {
namespace chaotic_good {
namespace testing {

ClientMetadataHandle TestInitialMetadata() {
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
  return md;
}

// Send messages from client to server.
auto SendClientToServerMessages(CallInitiator initiator, int num_messages) {
  return Loop([initiator, num_messages, i = 0]() mutable {
    bool has_message = (i < num_messages);
    return If(
        has_message,
        [initiator, &i]() mutable {
          return Seq(
              initiator.PushMessage(Arena::MakePooled<Message>(
                  SliceBuffer(Slice::FromCopiedString(std::to_string(i))), 0)),
              [&i]() -> LoopCtl<absl::Status> {
                ++i;
                return Continue();
              });
        },
        [initiator]() mutable -> LoopCtl<absl::Status> {
          initiator.FinishSends();
          return absl::OkStatus();
        });
  });
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

TEST_F(TransportTest, AddOneStream) {
  auto owned_frame_transport =
      MakeOrphanable<MockFrameTransport>(event_engine());
  auto* frame_transport = owned_frame_transport.get();
  static const std::string many_as(1024 * 1024, 'a');
  auto channel_args = MakeChannelArgs(event_engine());
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      channel_args, std::move(owned_frame_transport), MessageChunker(0, 1));
  auto call = MakeCall(TestInitialMetadata());
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  frame_transport->ExpectWrite(MakeProtoFrame<ClientInitialMetadataFrame>(
      1, "path: '/demo.Service/Step'"));
  frame_transport->ExpectWrite(MakeMessageFrame(1, "0"));
  frame_transport->ExpectWrite(ClientEndOfStream(1));
  transport->StartCall(call.handler.StartCall());
  call.initiator.SpawnGuarded("test-send",
                              [initiator = call.initiator]() mutable {
                                return SendClientToServerMessages(initiator, 1);
                              });
  call.initiator.SpawnInfallible(
      "test-read", [&on_done, initiator = call.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<std::optional<ServerMetadataHandle>> md) {
              EXPECT_TRUE(md.ok());
              EXPECT_TRUE(md.value().has_value());
              EXPECT_EQ(md.value()
                            .value()
                            ->get_pointer(GrpcMessageMetadata())
                            ->as_string_view(),
                        "hello");
            },
            [initiator]() mutable { return initiator.PullMessage(); },
            [](ServerToClientNextMessage msg) {
              EXPECT_TRUE(msg.ok());
              EXPECT_TRUE(msg.has_value());
              EXPECT_EQ(msg.value().payload()->JoinIntoString(), many_as);
            },
            [initiator]() mutable { return initiator.PullMessage(); },
            [](ServerToClientNextMessage msg) {
              EXPECT_TRUE(msg.ok());
              EXPECT_FALSE(msg.has_value());
            },
            [initiator]() mutable {
              return initiator.PullServerTrailingMetadata();
            },
            [&on_done](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(), GRPC_STATUS_OK);
              on_done.Call();
            });
      });
  frame_transport->Read(
      MakeProtoFrame<ServerInitialMetadataFrame>(1, "message: 'hello'"));
  frame_transport->Read(MakeMessageFrame(1, many_as));
  frame_transport->Read(
      MakeProtoFrame<ServerTrailingMetadataFrame>(1, "status: 0"));
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(TransportTest, AddOneStreamMultipleMessages) {
  auto owned_frame_transport =
      MakeOrphanable<MockFrameTransport>(event_engine());
  auto* frame_transport = owned_frame_transport.get();
  auto channel_args = MakeChannelArgs(event_engine());
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      channel_args, std::move(owned_frame_transport), MessageChunker(0, 1));
  auto call = MakeCall(TestInitialMetadata());
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  frame_transport->ExpectWrite(MakeProtoFrame<ClientInitialMetadataFrame>(
      1, "path: '/demo.Service/Step'"));
  frame_transport->ExpectWrite(MakeMessageFrame(1, "0"));
  frame_transport->ExpectWrite(MakeMessageFrame(1, "1"));
  frame_transport->ExpectWrite(ClientEndOfStream(1));
  transport->StartCall(call.handler.StartCall());
  call.initiator.SpawnGuarded("test-send",
                              [initiator = call.initiator]() mutable {
                                return SendClientToServerMessages(initiator, 2);
                              });
  call.initiator.SpawnInfallible(
      "test-read", [&on_done, initiator = call.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<std::optional<ServerMetadataHandle>> md) {
              EXPECT_TRUE(md.ok());
              EXPECT_TRUE(md.value().has_value());
            },
            initiator.PullMessage(),
            [](ServerToClientNextMessage msg) {
              EXPECT_TRUE(msg.ok());
              EXPECT_TRUE(msg.has_value());
              EXPECT_EQ(msg.value().payload()->JoinIntoString(), "12345678");
            },
            initiator.PullMessage(),
            [](ServerToClientNextMessage msg) {
              EXPECT_TRUE(msg.ok());
              EXPECT_TRUE(msg.has_value());
              EXPECT_EQ(msg.value().payload()->JoinIntoString(), "87654321");
            },
            initiator.PullMessage(),
            [](ServerToClientNextMessage msg) {
              EXPECT_TRUE(msg.ok());
              EXPECT_FALSE(msg.has_value());
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(), GRPC_STATUS_OK);
              on_done.Call();
            });
      });
  frame_transport->Read(MakeProtoFrame<ServerInitialMetadataFrame>(1, ""));
  frame_transport->Read(MakeMessageFrame(1, "12345678"));
  frame_transport->Read(MakeMessageFrame(1, "87654321"));
  frame_transport->Read(
      MakeProtoFrame<ServerTrailingMetadataFrame>(1, "status: 0"));
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(TransportTest, CheckFailure) {
  auto owned_frame_transport =
      MakeOrphanable<MockFrameTransport>(event_engine());
  auto* frame_transport = owned_frame_transport.get();
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      MakeChannelArgs(event_engine()), std::move(owned_frame_transport),
      MessageChunker(0, 1));
  frame_transport->Close();
  auto call = MakeCall(TestInitialMetadata());
  transport->StartCall(call.handler.StartCall());
  call.initiator.SpawnGuarded("test-send",
                              [initiator = call.initiator]() mutable {
                                return SendClientToServerMessages(initiator, 1);
                              });
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  call.initiator.SpawnInfallible(
      "test-read", [&on_done, initiator = call.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_TRUE(md.ok());
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_UNAVAILABLE);
              on_done.Call();
            });
      });
  // Wait until ClientTransport's internal activities to finish.
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
