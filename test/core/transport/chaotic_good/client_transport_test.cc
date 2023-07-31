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

// IWYU pragma: no_include <sys/socket.h>

#include <stdio.h>

#include <memory>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_join.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::MockFunction;
using testing::Return;
using testing::StrictMock;

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

class ClientTransportTest : public ::testing::Test {
 public:
  ClientTransportTest()
      : control_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        data_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        memory_allocator_(
            ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
                "test")),
        control_endpoint_(*control_endpoint_ptr_),
        data_endpoint_(*data_endpoint_ptr_),
        event_engine_(std::make_shared<
                      grpc_event_engine::experimental::FuzzingEventEngine>(
            []() {
              grpc_timer_manager_set_threading(false);
              grpc_event_engine::experimental::FuzzingEventEngine::Options
                  options;
              return options;
            }(),
            fuzzing_event_engine::Actions())),
        client_transport_(
            std::make_unique<PromiseEndpoint>(
                std::unique_ptr<MockEndpoint>(control_endpoint_ptr_),
                SliceBuffer()),
            std::make_unique<PromiseEndpoint>(
                std::unique_ptr<MockEndpoint>(data_endpoint_ptr_),
                SliceBuffer()),
            std::static_pointer_cast<
                grpc_event_engine::experimental::EventEngine>(event_engine_)),
        arena_(MakeScopedArena(initial_arena_size, &memory_allocator_)),
        pipe_client_to_server_messages_(arena_.get()) {}

 private:
  MockEndpoint* control_endpoint_ptr_;
  MockEndpoint* data_endpoint_ptr_;
  size_t initial_arena_size = 1024;
  MemoryAllocator memory_allocator_;

 protected:
  MockEndpoint& control_endpoint_;
  MockEndpoint& data_endpoint_;
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  ClientTransport client_transport_;
  ScopedArenaPtr arena_;
  Pipe<MessageHandle> pipe_client_to_server_messages_;
};

TEST_F(ClientTransportTest, AddOneStream) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedString("test add stream."));
  auto message = arena_->MakePooled<Message>(std::move(buffer), 0);
  ClientMetadataHandle md;
  auto args = CallArgs{
      std::move(md), ClientInitialMetadataOutstandingToken::Empty(), nullptr,
      nullptr,       &pipe_client_to_server_messages_.receiver,      nullptr};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).WillOnce(Return(true));
  EXPECT_CALL(data_endpoint_, Write).WillOnce(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: send message into the pipe, and receive from the
          // pipe.
          Join(Seq(pipe_client_to_server_messages_.sender.Push(
                       std::move(message)),
                   [this] {
                     this->pipe_client_to_server_messages_.sender.Close();
                     return absl::OkStatus();
                   }),
               Seq(client_transport_.AddStream(std::move(args)),
                   [] { return absl::OkStatus(); })),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<absl::Status, absl::Status>& ret) {
            EXPECT_TRUE(std::get<0>(ret).ok());
            EXPECT_TRUE(std::get<1>(ret).ok());
            return absl::OkStatus();
          }),
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
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
