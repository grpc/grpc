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

#include <algorithm>  // IWYU pragma: keep
#include <memory>
#include <string>  // IWYU pragma: keep
#include <tuple>
#include <vector>  // IWYU pragma: keep

#include "absl/functional/any_invocable.h"
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
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/event_engine_wakeup_scheduler.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
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
        arena_(MakeScopedArena(initial_arena_size, &memory_allocator_)),
        pipe_client_to_server_messages_(arena_.get()),
        pipe_server_to_client_messages_(arena_.get()),
        pipe_server_intial_metadata_(arena_.get()),
        pipe_client_to_server_messages_second_(arena_.get()),
        pipe_server_to_client_messages_second_(arena_.get()),
        pipe_server_intial_metadata_second_(arena_.get()) {}
  // Expect how client transport will read from control/data endpoints with a
  // test frame.
  void AddReadExpectations(int num_of_streams) {
    for (int i = 0; i < num_of_streams; i++) {
      EXPECT_CALL(control_endpoint_, Read)
          .InSequence(control_endpoint_sequence)
          .WillOnce(WithArgs<0, 1>(
              [this, i](absl::AnyInvocable<void(absl::Status)> on_read,
                        grpc_event_engine::experimental::SliceBuffer*
                            buffer) mutable {
                // Construct test frame for EventEngine read: headers  (15
                // bytes), message(16 bytes), message padding (48 byte),
                // trailers (15 bytes).
                const std::string frame_header = {
                    static_cast<char>(0x80),  // frame type = fragment
                    0x03,                     // flag = has header + has trailer
                    0x00,
                    0x00,
                    static_cast<char>(i + 1),  // stream id = 1
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
                    0x0f,  // trailer length = 15
                    0x00,
                    0x00,
                    0x00};
                // Schedule mock_endpoint to read buffer.
                grpc_event_engine::experimental::Slice slice(
                    grpc_slice_from_cpp_string(frame_header));
                buffer->Append(std::move(slice));
                // Execute read callback later to control when read starts.
                if (i == 0) {
                  read_callback_ = std::move(on_read);
                  // Return false to mock EventEngine read not finish.
                  return false;
                } else {
                  return true;
                }
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
                // Encoded string of trailer "grpc-status: 0".
                const std::string trailers = {0x10, 0x0b, 0x67, 0x72, 0x70,
                                              0x63, 0x2d, 0x73, 0x74, 0x61,
                                              0x74, 0x75, 0x73, 0x01, 0x30};
                // Schedule mock_endpoint to read buffer.
                grpc_event_engine::experimental::Slice slice(
                    grpc_slice_from_cpp_string(header + trailers));
                buffer->Append(std::move(slice));
                return true;
              }));
    }
    EXPECT_CALL(control_endpoint_, Read)
        .InSequence(control_endpoint_sequence)
        .WillOnce(Return(false));
    for (int i = 0; i < num_of_streams; i++) {
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
  }
  // Initial ClientTransport with read expecations
  void InitialClientTransport(int num_of_streams) {
    // Read expectaions need to be added before transport initialization since
    // reader_ activity loop is started in ClientTransport initialization,
    AddReadExpectations(num_of_streams);
    client_transport_ = std::make_unique<ClientTransport>(
        std::make_unique<PromiseEndpoint>(
            std::unique_ptr<MockEndpoint>(control_endpoint_ptr_),
            SliceBuffer()),
        std::make_unique<PromiseEndpoint>(
            std::unique_ptr<MockEndpoint>(data_endpoint_ptr_), SliceBuffer()),
        event_engine_);
  }
  // Send messages from client to server.
  auto SendClientToServerMessages(
      Pipe<MessageHandle>& pipe_client_to_server_messages,
      int num_of_messages) {
    return Loop([&pipe_client_to_server_messages, num_of_messages,
                 this]() mutable {
      bool has_message = (num_of_messages > 0);
      return If(
          has_message,
          Seq(pipe_client_to_server_messages.sender.Push(
                  arena_->MakePooled<Message>()),
              [&num_of_messages]() -> LoopCtl<absl::Status> {
                num_of_messages--;
                return Continue();
              }),
          [&pipe_client_to_server_messages]() mutable -> LoopCtl<absl::Status> {
            pipe_client_to_server_messages.sender.Close();
            return absl::OkStatus();
          });
    });
  }
  // Add stream into client transport, and expect return trailers of
  // "grpc-status:code".
  auto AddStream(CallArgs args, const grpc_status_code trailers) {
    return Seq(client_transport_->AddStream(std::move(args)),
               [trailers](ServerMetadataHandle ret) {
                 // AddStream will finish with server trailers:
                 // "grpc-status:code".
                 EXPECT_EQ(ret->get(GrpcStatusMetadata()).value(), trailers);
                 return trailers;
               });
  }
  // Start read from control endpoints.
  auto StartRead(const absl::Status& read_status) {
    return [read_status, this] {
      read_callback_(read_status);
      return read_status;
    };
  }
  // Receive messages from server to client.
  auto ReceiveServerToClientMessages(
      Pipe<ServerMetadataHandle>& pipe_server_intial_metadata,
      Pipe<MessageHandle>& pipe_server_to_client_messages) {
    return Seq(
        // Receive server initial metadata.
        Map(pipe_server_intial_metadata.receiver.Next(),
            [](NextResult<ServerMetadataHandle> r) {
              // Expect value: ":path: /demo.Service/Step"
              EXPECT_TRUE(r.has_value());
              EXPECT_EQ(
                  r.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  "/demo.Service/Step");
              return absl::OkStatus();
            }),
        // Receive server to client messages.
        Map(pipe_server_to_client_messages.receiver.Next(),
            [this](NextResult<MessageHandle> r) {
              EXPECT_TRUE(r.has_value());
              EXPECT_EQ(r.value()->payload()->JoinIntoString(), message_);
              return absl::OkStatus();
            }),
        [&pipe_server_intial_metadata,
         &pipe_server_to_client_messages]() mutable {
          // Close pipes after receive message.
          pipe_server_to_client_messages.sender.Close();
          pipe_server_intial_metadata.sender.Close();
          return absl::OkStatus();
        });
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
  std::unique_ptr<ClientTransport> client_transport_;
  ScopedArenaPtr arena_;
  Pipe<MessageHandle> pipe_client_to_server_messages_;
  Pipe<MessageHandle> pipe_server_to_client_messages_;
  Pipe<ServerMetadataHandle> pipe_server_intial_metadata_;
  // Added for mutliple streams tests.
  Pipe<MessageHandle> pipe_client_to_server_messages_second_;
  Pipe<MessageHandle> pipe_server_to_client_messages_second_;
  Pipe<ServerMetadataHandle> pipe_server_intial_metadata_second_;
  absl::AnyInvocable<void(absl::Status)> read_callback_;
  // Added to verify received message payload.
  const std::string message_ = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
};

TEST_F(ClientTransportTest, AddOneStream) {
  InitialClientTransport(1);
  ClientMetadataHandle md;
  auto args = CallArgs{std::move(md),
                       ClientInitialMetadataOutstandingToken::Empty(),
                       nullptr,
                       &pipe_server_intial_metadata_.sender,
                       &pipe_client_to_server_messages_.receiver,
                       &pipe_server_to_client_messages_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).WillOnce(Return(true));
  EXPECT_CALL(data_endpoint_, Write).WillOnce(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: write and read messages in client transport.
          Join(
              // Add first stream with call_args into client transport.
              AddStream(std::move(args), GRPC_STATUS_OK),
              // Start read from control endpoints.
              StartRead(absl::OkStatus()),
              // Send messages to call_args.client_to_server_messages pipe,
              // which will be eventually sent to control/data endpoints.
              SendClientToServerMessages(pipe_client_to_server_messages_, 1),
              // Receive messages from control/data endpoints.
              ReceiveServerToClientMessages(pipe_server_intial_metadata_,
                                            pipe_server_to_client_messages_)),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<grpc_status_code, absl::Status, absl::Status,
                              absl::Status>& ret) {
            EXPECT_EQ(std::get<0>(ret), GRPC_STATUS_OK);
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            EXPECT_TRUE(std::get<3>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(event_engine_),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddOneStreamMultipleMessages) {
  InitialClientTransport(1);
  ClientMetadataHandle md;
  auto args = CallArgs{std::move(md),
                       ClientInitialMetadataOutstandingToken::Empty(),
                       nullptr,
                       &pipe_server_intial_metadata_.sender,
                       &pipe_client_to_server_messages_.receiver,
                       &pipe_server_to_client_messages_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).Times(3).WillRepeatedly(Return(true));
  EXPECT_CALL(data_endpoint_, Write).Times(3).WillRepeatedly(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: write and read messages in client transport.
          Join(
              // Add first stream with call_args into client transport.
              AddStream(std::move(args), GRPC_STATUS_OK),
              // Start read from control endpoints.
              StartRead(absl::OkStatus()),
              // Send messages to call_args.client_to_server_messages pipe,
              // which will be eventually sent to control/data endpoints.
              SendClientToServerMessages(pipe_client_to_server_messages_, 3),
              // Receive messages from control/data endpoints.
              ReceiveServerToClientMessages(pipe_server_intial_metadata_,
                                            pipe_server_to_client_messages_)),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<grpc_status_code, absl::Status, absl::Status,
                              absl::Status>& ret) {
            EXPECT_EQ(std::get<0>(ret), GRPC_STATUS_OK);
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            EXPECT_TRUE(std::get<3>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(event_engine_),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Wait until ClientTransport's internal activities to finish.
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

TEST_F(ClientTransportTest, AddMultipleStreamsMultipleMessages) {
  InitialClientTransport(2);
  ClientMetadataHandle first_stream_md;
  ClientMetadataHandle second_stream_md;
  auto first_stream_args =
      CallArgs{std::move(first_stream_md),
               ClientInitialMetadataOutstandingToken::Empty(),
               nullptr,
               &pipe_server_intial_metadata_.sender,
               &pipe_client_to_server_messages_.receiver,
               &pipe_server_to_client_messages_.sender};
  auto second_stream_args =
      CallArgs{std::move(second_stream_md),
               ClientInitialMetadataOutstandingToken::Empty(),
               nullptr,
               &pipe_server_intial_metadata_second_.sender,
               &pipe_client_to_server_messages_second_.receiver,
               &pipe_server_to_client_messages_second_.sender};
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  EXPECT_CALL(control_endpoint_, Write).Times(6).WillRepeatedly(Return(true));
  EXPECT_CALL(data_endpoint_, Write).Times(6).WillRepeatedly(Return(true));
  auto activity = MakeActivity(
      Seq(
          // Concurrently: write and read messages from client transport.
          Join(
              // Add first stream with call_args into client transport.
              AddStream(std::move(first_stream_args), GRPC_STATUS_OK),
              // Start read from control endpoints.
              StartRead(absl::OkStatus()),
              // Send messages to first stream's
              // call_args.client_to_server_messages pipe, which will be
              // eventually sent to control/data endpoints.
              SendClientToServerMessages(pipe_client_to_server_messages_, 3),
              // Receive first stream's messages from control/data endpoints.
              ReceiveServerToClientMessages(pipe_server_intial_metadata_,
                                            pipe_server_to_client_messages_)),
          Join(
              // Add second stream with call_args into client transport.
              AddStream(std::move(second_stream_args), GRPC_STATUS_OK),
              // Send messages to second stream's
              // call_args.client_to_server_messages pipe, which will be
              // eventually sent to control/data endpoints.
              SendClientToServerMessages(pipe_client_to_server_messages_second_,
                                         3),
              // Receive second stream's messages from control/data endpoints.
              ReceiveServerToClientMessages(
                  pipe_server_intial_metadata_second_,
                  pipe_server_to_client_messages_second_)),
          // Once complete, verify successful sending and the received value.
          [](const std::tuple<grpc_status_code, absl::Status, absl::Status>&
                 ret) {
            EXPECT_EQ(std::get<0>(ret), GRPC_STATUS_OK);
            EXPECT_TRUE(std::get<1>(ret).ok());
            EXPECT_TRUE(std::get<2>(ret).ok());
            return absl::OkStatus();
          }),
      EventEngineWakeupScheduler(event_engine_),
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
