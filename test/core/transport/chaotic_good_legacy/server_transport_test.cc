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

#include "src/core/ext/transport/chaotic_good_legacy/server_transport.h"

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

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/transport/chaotic_good_legacy/transport_test.h"
#include "test/core/transport/util/mock_promise_endpoint.h"

using testing::_;
using testing::MockFunction;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

using EventEngineSlice = grpc_event_engine::experimental::Slice;

using grpc_core::util::testing::MockPromiseEndpoint;

namespace grpc_core {
namespace chaotic_good_legacy {
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
  ~MockCallDestination() override = default;
  MOCK_METHOD(void, Orphaned, (), (override));
  MOCK_METHOD(void, StartCall, (UnstartedCallHandler unstarted_call_handler),
              (override));
};

class MockServerConnectionFactory : public ServerConnectionFactory {
 public:
  MOCK_METHOD(PendingConnection, RequestDataConnection, (), (override));
  void Orphaned() final {}
};

TEST_F(TransportTest, ReadAndWriteOneMessage) {
  MockPromiseEndpoint control_endpoint(1);
  MockPromiseEndpoint data_endpoint(2);
  auto server_connection_factory =
      MakeRefCounted<StrictMock<MockServerConnectionFactory>>();
  auto call_destination = MakeRefCounted<StrictMock<MockCallDestination>>();
  EXPECT_CALL(*call_destination, Orphaned()).Times(1);
  auto channel_args = MakeChannelArgs(event_engine());
  auto transport = MakeOrphanable<ChaoticGoodServerTransport>(
      channel_args, std::move(control_endpoint.promise_endpoint),
      MakeConfig(channel_args, std::move(data_endpoint.promise_endpoint)),
      server_connection_factory);
  const auto server_initial_metadata =
      EncodeProto<chaotic_good_frame::ServerMetadata>("message: 'hello'");
  const auto server_trailing_metadata =
      EncodeProto<chaotic_good_frame::ServerMetadata>("status: 0");
  const auto client_initial_metadata =
      EncodeProto<chaotic_good_frame::ClientMetadata>(
          "path: '/demo.Service/Step'");
  // Once we set the acceptor, expect to read some frames.
  // We'll return a new request with a payload of "12345678".
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kClientInitialMetadata, 0, 1,
                             client_initial_metadata.length()),
       client_initial_metadata.Copy(),
       SerializedFrameHeader(FrameType::kMessage, 0, 1, 8),
       EventEngineSlice::FromCopiedString("12345678"),
       SerializedFrameHeader(FrameType::kClientEndOfStream, 0, 1, 0)},
      event_engine().get());
  // Once that's read we'll create a new call
  StrictMock<MockFunction<void()>> on_done;
  auto control_address =
      grpc_event_engine::experimental::URIToResolvedAddress("ipv4:1.2.3.4:5678")
          .value();
  EXPECT_CALL(*control_endpoint.endpoint, GetPeerAddress)
      .WillRepeatedly(
          [&control_address]() -> const grpc_event_engine::experimental::
                                   EventEngine::ResolvedAddress& {
                                     return control_address;
                                   });
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
  transport->SetCallDestination(call_destination);
  EXPECT_CALL(on_done, Call());
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .InSequence(control_endpoint.read_sequence)
      .WillOnce(Return(false));
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kServerInitialMetadata, 0, 1,
                             server_initial_metadata.length()),
       server_initial_metadata.Copy(),
       SerializedFrameHeader(FrameType::kMessage, 0, 1, 8),
       EventEngineSlice::FromCopiedString("87654321"),
       SerializedFrameHeader(FrameType::kServerTrailingMetadata, 0, 1,
                             server_trailing_metadata.length()),
       server_trailing_metadata.Copy()},
      nullptr);
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  ::testing::Mock::VerifyAndClearExpectations(control_endpoint.endpoint);
  ::testing::Mock::VerifyAndClearExpectations(data_endpoint.endpoint);
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
