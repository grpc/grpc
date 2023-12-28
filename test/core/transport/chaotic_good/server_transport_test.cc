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

// IWYU pragma: no_include <sys/socket.h>

#include <algorithm>  // IWYU pragma: keep
#include <memory>
#include <string>  // IWYU pragma: keep
#include <vector>  // IWYU pragma: keep

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"     // IWYU pragma: keep
#include "absl/strings/str_format.h"  // IWYU pragma: keep
#include "absl/types/optional.h"      // IWYU pragma: keep
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>  // IWYU pragma: keep
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/status.h>  // IWYU pragma: keep

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"      // IWYU pragma: keep
#include "src/core/lib/transport/metadata_batch.h"  // IWYU pragma: keep
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"

using testing::MockFunction;
using testing::Return;
using testing::Sequence;
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

class ServerTransportTest : public ::testing::Test {
 public:
  ServerTransportTest()
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
        arena_(MakeScopedArena(initial_arena_size, &memory_allocator_)) {}
  void InitialServerTransport() {
    server_transport_ = std::make_unique<ServerTransport>(
        std::make_unique<PromiseEndpoint>(
            std::unique_ptr<MockEndpoint>(control_endpoint_ptr_),
            SliceBuffer()),
        std::make_unique<PromiseEndpoint>(
            std::unique_ptr<MockEndpoint>(data_endpoint_ptr_), SliceBuffer()),
        std::static_pointer_cast<grpc_event_engine::experimental::EventEngine>(
            event_engine_),
        on_accept_.AsStdFunction());
  }
  void AddReadExpectation() {
    EXPECT_CALL(control_endpoint_, Read)
        .InSequence(control_endpoint_sequence)
        .WillOnce(WithArgs<0, 1>(
            [](absl::AnyInvocable<void(absl::Status)> on_read,
               grpc_event_engine::experimental::SliceBuffer* buffer) mutable {
              // Construct test frame for EventEngine read: headers  (26
              // bytes), message(8 bytes), message padding (56 byte),
              // trailers (0 bytes).
              const std::string frame_header = {
                  static_cast<char>(0x80),  // frame type = fragment
                  0x01,                     // flag = has header
                  0x00,
                  0x00,
                  0x01,  // stream id = 1
                  0x00,
                  0x00,
                  0x00,
                  0x1a,  // header length = 26
                  0x00,
                  0x00,
                  0x00,
                  0x08,  // message length = 8
                  0x00,
                  0x00,
                  0x00,
                  0x38,  // message padding =56
                  0x00,
                  0x00,
                  0x00,
                  0x00,  // trailer length = 0
                  0x00,
                  0x00,
                  0x00};
              // Schedule mock_endpoint to read buffer.
              grpc_event_engine::experimental::Slice slice(
                  grpc_slice_from_cpp_string(frame_header));
              buffer->Append(std::move(slice));
              return true;
            }));
    EXPECT_CALL(control_endpoint_, Read)
        .InSequence(control_endpoint_sequence)
        .WillOnce(WithArgs<1>(
            [](grpc_event_engine::experimental::SliceBuffer* buffer) {
              // Encoded string of header ":path: /demo.Service/Step".
              const std::string header = {
                  0x10, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x12, 0x2f,
                  0x64, 0x65, 0x6d, 0x6f, 0x2e, 0x53, 0x65, 0x72, 0x76,
                  0x69, 0x63, 0x65, 0x2f, 0x53, 0x74, 0x65, 0x70};
              // Schedule mock_endpoint to read buffer.
              grpc_event_engine::experimental::Slice slice(
                  grpc_slice_from_cpp_string(header));
              buffer->Append(std::move(slice));
              return true;
            }));
    EXPECT_CALL(on_accept_, Call)
        .InSequence(control_endpoint_sequence)
        .WillOnce(WithArgs<0>([this](ClientMetadata& md) {
          EXPECT_FALSE(md.empty());
          EXPECT_EQ(md.get_pointer(HttpPathMetadata())->as_string_view(),
                    "/demo.Service/Step");
          call_ = MakeRefCounted<Call>(arena_, 1, event_engine_);
          call_initiator_ = std::make_shared<CallInitiator>(call_);
          std::cout << "set pipes in call_initiator "
                    << "\n";
          fflush(stdout);
          return call_initiator_;
        }));
    EXPECT_CALL(control_endpoint_, Read)
        .InSequence(control_endpoint_sequence)
        .WillOnce(Return(false));
    EXPECT_CALL(data_endpoint_, Read)
        .InSequence(data_endpoint_sequence)
        .WillOnce(WithArgs<1>(
            [this](grpc_event_engine::experimental::SliceBuffer* buffer) {
              const std::string message_padding = {
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
              grpc_event_engine::experimental::Slice slice(
                  grpc_slice_from_cpp_string(message_padding + message_));
              buffer->Append(std::move(slice));
              return true;
            }));
  }
  auto ReadClientToServerMessage(const std::shared_ptr<CallInitiator>& r) {
    std::cout << "read client message "
              << "\n";
    fflush(stdout);
    auto read_client_message = [r, this] {
      return Seq(r->PullClientToServerMessage(),
                 [this, r](NextResult<MessageHandle> message) {
                   EXPECT_TRUE(message.has_value());
                   EXPECT_EQ(message.value()->payload()->JoinIntoString(),
                             message_);
                   r->CloseClientToServerPipe();
                   std::cout << "read client message done"
                             << "\n";
                   fflush(stdout);
                   return absl::OkStatus();
                 });
    };
    return read_client_message;
  }
  auto WriteServerToClientMessage(const std::shared_ptr<CallInitiator>& r) {
    std::cout << "write server message "
              << "\n";
    fflush(stdout);
    auto write_server_message = [r, this] {
      return Seq(r->PushServerToClientMessage(arena_->MakePooled<Message>()),
                 [r](bool success) {
                   EXPECT_TRUE(success);
                   r->CloseServerToClientPipe();
                   r->CloseServerInitialMetadataPipe();
                   std::cout << "write server message done"
                             << "\n";
                   fflush(stdout);
                   return absl::OkStatus();
                 });
    };
    return write_server_message;
  }

 private:
  MockEndpoint* control_endpoint_ptr_;
  MockEndpoint* data_endpoint_ptr_;
  size_t initial_arena_size = 1024;
  MemoryAllocator memory_allocator_;
  Sequence control_endpoint_sequence;
  Sequence data_endpoint_sequence;

 protected:
  MockEndpoint& control_endpoint_;
  MockEndpoint& data_endpoint_;
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  std::unique_ptr<ServerTransport> server_transport_;
  StrictMock<MockFunction<std::shared_ptr<CallInitiator>(ClientMetadata&)>>
      on_accept_;
  RefCountedPtr<Call> call_;
  std::shared_ptr<CallInitiator> call_initiator_;
  // Added to verify received message payload.
  const std::string message_ = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  std::shared_ptr<Arena> arena_;
};

TEST_F(ServerTransportTest, ReadAndWriteOneMessage) {
  AddReadExpectation();
  EXPECT_CALL(control_endpoint_, Write).WillOnce(Return(true));
  EXPECT_CALL(data_endpoint_, Write).WillOnce(Return(true));
  InitialServerTransport();
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus())).Times(2);
  auto on_done_action = [&on_done](absl::Status status) {
    on_done.Call(std::move(status));
  };
  call_initiator_->Spawn(ReadClientToServerMessage(call_initiator_),
                         on_done_action);
  call_initiator_->Spawn(WriteServerToClientMessage(call_initiator_),
                         on_done_action);
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
  std::cout << "ee done "
            << "\n";
  fflush(stdout);
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
