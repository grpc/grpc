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

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
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

#include "src/core/ext/transport/chaotic_good/client_transport.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"

using testing::AtMost;
using testing::MockFunction;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

namespace grpc_core {
namespace chaotic_good {
namespace testing {

class MockEndpoint
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  MOCK_METHOD(
      bool, Read,
      (absl::AnyInvocable<void(absl::Status)> on_read,
       grpc_event_engine::experimental::SliceBuffer* buffer,
       const grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs*
           args),
      (override));

  MOCK_METHOD(
      bool, Write,
      (absl::AnyInvocable<void(absl::Status)> on_writable,
       grpc_event_engine::experimental::SliceBuffer* data,
       const grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs*
           args),
      (override));

  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetPeerAddress, (), (const, override));
  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetLocalAddress, (), (const, override));
};

struct MockPromiseEndpoint {
  StrictMock<MockEndpoint>* endpoint = new StrictMock<MockEndpoint>();
  PromiseEndpoint promise_endpoint{
      std::unique_ptr<StrictMock<MockEndpoint>>(endpoint), SliceBuffer()};
};

// Send messages from client to server.
auto SendClientToServerMessages(CallInitiator initiator, int num_messages) {
  return Loop([initiator, num_messages]() mutable {
    bool has_message = (num_messages > 0);
    return If(
        has_message,
        Seq(initiator.PushMessage(GetContext<Arena>()->MakePooled<Message>()),
            [&num_messages]() -> LoopCtl<absl::Status> {
              --num_messages;
              return Continue();
            }),
        [initiator]() mutable -> LoopCtl<absl::Status> {
          initiator.FinishSends();
          return absl::OkStatus();
        });
  });
}

ClientMetadataHandle TestInitialMetadata() {
  auto md =
      GetContext<Arena>()->MakePooled<ClientMetadata>(GetContext<Arena>());
  md->Set(HttpPathMetadata(), Slice::FromStaticString("/test"));
  return md;
}

class ClientTransportTest : public ::testing::Test {
 protected:
  const std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>&
  event_engine() {
    return event_engine_;
  }
  MemoryAllocator* memory_allocator() { return &allocator_; }

  ChannelArgs MakeChannelArgs() {
    return CoreConfiguration::Get()
        .channel_args_preconditioning()
        .PreconditionChannelArgs(nullptr);
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_{
          std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
              []() {
                grpc_timer_manager_set_threading(false);
                grpc_event_engine::experimental::FuzzingEventEngine::Options
                    options;
                return options;
              }(),
              fuzzing_event_engine::Actions())};
  MemoryAllocator allocator_ = MakeResourceQuota("test-quota")
                                   ->memory_quota()
                                   ->CreateMemoryAllocator("test-allocator");
};

TEST_F(ClientTransportTest, AddOneStreamWithWriteFailed) {
  MockPromiseEndpoint control_endpoint;
  MockPromiseEndpoint data_endpoint;
  // Mock write failed and read is pending.
  EXPECT_CALL(*control_endpoint.endpoint, Write)
      .Times(AtMost(1))
      .WillOnce(
          WithArgs<0>([](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(absl::InternalError("control endpoint write failed."));
            return false;
          }));
  EXPECT_CALL(*data_endpoint.endpoint, Write)
      .Times(AtMost(1))
      .WillOnce(
          WithArgs<0>([](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(absl::InternalError("data endpoint write failed."));
            return false;
          }));
  EXPECT_CALL(*control_endpoint.endpoint, Read).WillOnce(Return(false));
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint), MakeChannelArgs(),
      event_engine(), HPackParser(), HPackCompressor());
  auto call =
      MakeCall(event_engine().get(), Arena::Create(8192, memory_allocator()));
  transport->StartCall(std::move(call.handler));
  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 1));
  });
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  call.initiator.SpawnInfallible(
      "test-read", [&on_done, initiator = call.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_FALSE(md.ok());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_UNAVAILABLE);
              on_done.Call();
              return Empty{};
            });
      });
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddOneStreamWithReadFailed) {
  MockPromiseEndpoint control_endpoint;
  MockPromiseEndpoint data_endpoint;
  // Mock read failed.
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .WillOnce(WithArgs<0>(
          [](absl::AnyInvocable<void(absl::Status)> on_read) mutable {
            on_read(absl::InternalError("control endpoint read failed."));
            // Return false to mock EventEngine read not finish.
            return false;
          }));
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint), MakeChannelArgs(),
      event_engine(), HPackParser(), HPackCompressor());
  auto call =
      MakeCall(event_engine().get(), Arena::Create(8192, memory_allocator()));
  transport->StartCall(std::move(call.handler));
  call.initiator.SpawnGuarded("test-send", [initiator =
                                                call.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 1));
  });
  StrictMock<MockFunction<void()>> on_done;
  EXPECT_CALL(on_done, Call());
  call.initiator.SpawnInfallible(
      "test-read", [&on_done, initiator = call.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_FALSE(md.ok());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_UNAVAILABLE);
              on_done.Call();
              return Empty{};
            });
      });
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddMultipleStreamWithWriteFailed) {
  // Mock write failed at first stream and second stream's write will fail too.
  MockPromiseEndpoint control_endpoint;
  MockPromiseEndpoint data_endpoint;
  EXPECT_CALL(*control_endpoint.endpoint, Write)
      .Times(AtMost(1))
      .WillRepeatedly(
          WithArgs<0>([](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(absl::InternalError("control endpoint write failed."));
            return false;
          }));
  EXPECT_CALL(*data_endpoint.endpoint, Write)
      .Times(AtMost(1))
      .WillRepeatedly(
          WithArgs<0>([](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(absl::InternalError("data endpoint write failed."));
            return false;
          }));
  EXPECT_CALL(*control_endpoint.endpoint, Read).WillOnce(Return(false));
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint), MakeChannelArgs(),
      event_engine(), HPackParser(), HPackCompressor());
  auto call1 =
      MakeCall(event_engine().get(), Arena::Create(8192, memory_allocator()));
  transport->StartCall(std::move(call1.handler));
  auto call2 =
      MakeCall(event_engine().get(), Arena::Create(8192, memory_allocator()));
  transport->StartCall(std::move(call2.handler));
  call1.initiator.SpawnGuarded("test-send-1", [initiator =
                                                   call1.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 1));
  });
  call2.initiator.SpawnGuarded("test-send-2", [initiator =
                                                   call2.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 1));
  });
  StrictMock<MockFunction<void()>> on_done1;
  EXPECT_CALL(on_done1, Call());
  StrictMock<MockFunction<void()>> on_done2;
  EXPECT_CALL(on_done2, Call());
  call1.initiator.SpawnInfallible(
      "test-read-1", [&on_done1, initiator = call1.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_FALSE(md.ok());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done1](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_UNAVAILABLE);
              on_done1.Call();
              return Empty{};
            });
      });
  call2.initiator.SpawnInfallible(
      "test-read-2", [&on_done2, initiator = call2.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_FALSE(md.ok());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done2](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_UNAVAILABLE);
              on_done2.Call();
              return Empty{};
            });
      });
  // Wait until ClientTransport's internal activities to finish.
  event_engine()->TickUntilIdle();
  event_engine()->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddMultipleStreamWithReadFailed) {
  MockPromiseEndpoint control_endpoint;
  MockPromiseEndpoint data_endpoint;
  // Mock read failed at first stream, and second stream's write will fail too.
  EXPECT_CALL(*control_endpoint.endpoint, Read)
      .WillOnce(WithArgs<0>(
          [](absl::AnyInvocable<void(absl::Status)> on_read) mutable {
            on_read(absl::InternalError("control endpoint read failed."));
            // Return false to mock EventEngine read not finish.
            return false;
          }));
  auto transport = MakeOrphanable<ChaoticGoodClientTransport>(
      std::move(control_endpoint.promise_endpoint),
      std::move(data_endpoint.promise_endpoint), MakeChannelArgs(),
      event_engine(), HPackParser(), HPackCompressor());
  auto call1 =
      MakeCall(event_engine().get(), Arena::Create(8192, memory_allocator()));
  transport->StartCall(std::move(call1.handler));
  auto call2 =
      MakeCall(event_engine().get(), Arena::Create(8192, memory_allocator()));
  transport->StartCall(std::move(call2.handler));
  call1.initiator.SpawnGuarded("test-send", [initiator =
                                                 call1.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 1));
  });
  call2.initiator.SpawnGuarded("test-send", [initiator =
                                                 call2.initiator]() mutable {
    return TrySeq(initiator.PushClientInitialMetadata(TestInitialMetadata()),
                  SendClientToServerMessages(initiator, 1));
  });
  StrictMock<MockFunction<void()>> on_done1;
  EXPECT_CALL(on_done1, Call());
  StrictMock<MockFunction<void()>> on_done2;
  EXPECT_CALL(on_done2, Call());
  call1.initiator.SpawnInfallible(
      "test-read", [&on_done1, initiator = call1.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_FALSE(md.ok());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done1](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_UNAVAILABLE);
              on_done1.Call();
              return Empty{};
            });
      });
  call2.initiator.SpawnInfallible(
      "test-read", [&on_done2, initiator = call2.initiator]() mutable {
        return Seq(
            initiator.PullServerInitialMetadata(),
            [](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
              EXPECT_FALSE(md.ok());
              return Empty{};
            },
            initiator.PullServerTrailingMetadata(),
            [&on_done2](ServerMetadataHandle md) {
              EXPECT_EQ(md->get(GrpcStatusMetadata()).value(),
                        GRPC_STATUS_UNAVAILABLE);
              on_done2.Call();
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
