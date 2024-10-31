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
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/transport/chaotic_good/mock_promise_endpoint.h"
#include "test/core/transport/chaotic_good/transport_test.h"

using testing::_;
using testing::MockFunction;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

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
const uint8_t kGrpcStatus0[] = {0x40, 0x0b, 0x67, 0x72, 0x70, 0x63, 0x2d, 0x73,
                                0x74, 0x61, 0x74, 0x75, 0x73, 0x01, 0x30};

ServerMetadataHandle TestInitialMetadata() {
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
  return md;
}

ServerMetadataHandle TestTrailingMetadata() {
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  md->Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
  return md;
}

class MockCallDestination : public UnstartedCallDestination {
 public:
  ~MockCallDestination() override = default;
  MOCK_METHOD(void, Orphaned, (), (override));
  MOCK_METHOD(void, StartCall, (UnstartedCallHandler unstarted_call_handler),
              (override));
};

TEST_F(TransportTest, ReadAndWriteOneMessage) {
  MockPromiseEndpoint control_endpoint(1);
  MockPromiseEndpoint data_endpoint(2);
  auto call_destination = MakeRefCounted<StrictMock<MockCallDestination>>();
  EXPECT_CALL(*call_destination, Orphaned()).Times(1);
  auto transport = MakeOrphanable<ChaoticGoodServerTransport>(
      CoreConfiguration::Get()
          .channel_args_preconditioning()
          .PreconditionChannelArgs(nullptr),
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint), event_engine(), HPackParser(),
      HPackCompressor());
  // Once we set the acceptor, expect to read some frames.
  // We'll return a new request with a payload of "12345678".
  control_endpoint.ExpectRead(
      {SerializedFrameHeader(FrameType::kFragment, 7, 1, 26, 8, 56, 0),
       EventEngineSlice::FromCopiedBuffer(kPathDemoServiceStep,
                                          sizeof(kPathDemoServiceStep))},
      event_engine().get());
  data_endpoint.ExpectRead(
      {EventEngineSlice::FromCopiedString("12345678"), Zeros(56)}, nullptr);
  // Once that's read we'll create a new call
  StrictMock<MockFunction<void()>> on_done;
  auto control_address =
      grpc_event_engine::experimental::URIToResolvedAddress("ipv4:1.2.3.4:5678")
          .value();
  EXPECT_CALL(*control_endpoint.endpoint, GetPeerAddress)
      .WillRepeatedly([&control_address]() { return control_address; });
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
                return Empty{};
              },
              [handler]() mutable { return handler.PullMessage(); },
              [](ClientToServerNextMessage msg) {
                EXPECT_TRUE(msg.ok());
                EXPECT_TRUE(msg.has_value());
                EXPECT_EQ(msg.value().payload()->JoinIntoString(), "12345678");
                return Empty{};
              },
              [handler]() mutable { return handler.PullMessage(); },
              [](ClientToServerNextMessage msg) {
                EXPECT_TRUE(msg.ok());
                EXPECT_FALSE(msg.has_value());
                return Empty{};
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
                return Empty{};
              });
        });
      }));
  transport->SetCallDestination(call_destination);
  EXPECT_CALL(on_done, Call());
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .InSequence(control_endpoint.read_sequence)
      .WillOnce(Return(false));
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 1, 1,
                             sizeof(kPathDemoServiceStep), 0, 0, 0),
       EventEngineSlice::FromCopiedBuffer(kPathDemoServiceStep,
                                          sizeof(kPathDemoServiceStep))},
      nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 2, 1, 0, 8, 56, 0)},
      nullptr);
  data_endpoint.ExpectWrite(
      {EventEngineSlice::FromCopiedString("87654321"), Zeros(56)}, nullptr);
  control_endpoint.ExpectWrite(
      {SerializedFrameHeader(FrameType::kFragment, 4, 1, 0, 0, 0,
                             sizeof(kGrpcStatus0)),
       EventEngineSlice::FromCopiedBuffer(kGrpcStatus0, sizeof(kGrpcStatus0))},
      nullptr);
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
