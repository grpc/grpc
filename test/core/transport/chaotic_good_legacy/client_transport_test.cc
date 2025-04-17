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

#include "src/core/ext/transport/chaotic_good_legacy/client_transport.h"

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
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/transport/chaotic_good_legacy/transport_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"

using testing::MockFunction;
using testing::Return;
using testing::StrictMock;

using EventEngineSlice = grpc_event_engine::experimental::Slice;

using grpc_core::chaotic_good_legacy::testing::TransportTest;
using grpc_core::util::testing::MockPromiseEndpoint;

namespace grpc_core {
namespace chaotic_good_legacy {
namespace testing {

class MockClientConnectionFactory : public ClientConnectionFactory {
 public:
  MOCK_METHOD(PendingConnection, Connect, (absl::string_view), (override));
  void Orphaned() final {}
};

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

TEST_F(TransportTest, AddOneStream) {
  MockPromiseEndpoint control_endpoint(1000);
  MockPromiseEndpoint data_endpoint(1001);
  auto client_connection_factory =
      MakeRefCounted<StrictMock<MockClientConnectionFactory>>();
  static const std::string many_as(1024 * 1024, 'a');
  const auto server_initial_metadata =
      EncodeProto<chaotic_good_frame::ServerMetadata>("message: 'hello'");
  const auto server_trailing_metadata =
      EncodeProto<chaotic_good_frame::ServerMetadata>("status: 0");
  const auto client_initial_metadata =
      EncodeProto<chaotic_good_frame::ClientMetadata>(
          "path: '/demo.Service/Step'");
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kServerInitialMetadata, 0, 1,
                             server_initial_metadata.length()),
       server_initial_metadata.Copy(),
       SerializedFrameHeader(FrameType::kMessage, 1, 1, many_as.length()),
       SerializedFrameHeader(FrameType::kServerTrailingMetadata, 0, 1,
                             server_trailing_metadata.length()),
       server_trailing_metadata.Copy()},
      event_engine().get());
  data_endpoint.ExpectRead(
      {EventEngineSlice::FromCopiedString(many_as), Zeros(56)}, nullptr);
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .InSequence(control_endpoint.read_sequence)
      .WillOnce(Return(false));
  auto channel_args = MakeChannelArgs(event_engine());
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      channel_args, std::move(control_endpoint.promise_endpoint),
      MakeConfig(channel_args, std::move(data_endpoint.promise_endpoint)),
      client_connection_factory);
  auto call = MakeCall(TestInitialMetadata());
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kClientInitialMetadata, 0, 1,
                             client_initial_metadata.length()),
       client_initial_metadata.Copy()},
      nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kMessage, 0, 1, 1),
       EventEngineSlice::FromCopiedString("0"),
       SerializedFrameHeader(FrameType::kClientEndOfStream, 0, 1, 0)},
      nullptr);
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
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(TransportTest, AddOneStreamMultipleMessages) {
  MockPromiseEndpoint control_endpoint(1000);
  MockPromiseEndpoint data_endpoint(1001);
  auto client_connection_factory =
      MakeRefCounted<StrictMock<MockClientConnectionFactory>>();
  const auto server_initial_metadata =
      EncodeProto<chaotic_good_frame::ServerMetadata>("");
  const auto server_trailing_metadata =
      EncodeProto<chaotic_good_frame::ServerMetadata>("status: 0");
  const auto client_initial_metadata =
      EncodeProto<chaotic_good_frame::ClientMetadata>(
          "path: '/demo.Service/Step'");
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kServerInitialMetadata, 0, 1,
                             server_initial_metadata.length()),
       server_initial_metadata.Copy(),
       SerializedFrameHeader(FrameType::kMessage, 0, 1, 8),
       EventEngineSlice::FromCopiedString("12345678"),
       SerializedFrameHeader(FrameType::kMessage, 0, 1, 8),
       EventEngineSlice::FromCopiedString("87654321"),
       SerializedFrameHeader(FrameType::kServerTrailingMetadata, 0, 1,
                             server_trailing_metadata.length()),
       server_trailing_metadata.Copy()},
      event_engine().get());
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .InSequence(control_endpoint.read_sequence)
      .WillOnce(Return(false));
  auto channel_args = MakeChannelArgs(event_engine());
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      channel_args, std::move(control_endpoint.promise_endpoint),
      MakeConfig(channel_args, std::move(data_endpoint.promise_endpoint)),
      client_connection_factory);
  auto call = MakeCall(TestInitialMetadata());
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kClientInitialMetadata, 0, 1,
                             client_initial_metadata.length()),
       client_initial_metadata.Copy()},
      nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kMessage, 0, 1, 1),
       EventEngineSlice::FromCopiedString("0"),
       SerializedFrameHeader(FrameType::kMessage, 0, 1, 1),
       EventEngineSlice::FromCopiedString("1"),
       SerializedFrameHeader(FrameType::kClientEndOfStream, 0, 1, 0)},
      nullptr);
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
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

}  // namespace testing
}  // namespace chaotic_good_legacy
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
