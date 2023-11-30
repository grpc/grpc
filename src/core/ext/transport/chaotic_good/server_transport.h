// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <stdint.h>
#include <stdio.h>

#include <initializer_list>  // IWYU pragma: keep
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/event_engine/default_event_engine.h"  // IWYU pragma: keep
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/inter_activity_pipe.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

// Prototype based on gRPC Call 3.0.
// TODO(ladynana): convert to the true Call/CallInitiator once available.
class Call : public Party {
 public:
  explicit Call(std::shared_ptr<Arena> arena, uint32_t stream_id,
                std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                    event_engine)
      : Party(arena.get(), 1),
        ee_(event_engine),
        arena_(arena),
        stream_id_(stream_id),
        pipe_client_to_server_messages_(
            std::make_unique<Pipe<MessageHandle>>(arena_.get())),
        pipe_server_to_client_messages_(
            std::make_unique<Pipe<MessageHandle>>(arena_.get())),
        pipe_server_initial_metadata_(
            std::make_unique<Pipe<ServerMetadataHandle>>(arena_.get())){};
  ~Call() override {}
  std::string DebugTag() const override { return "TestCall"; }
  bool RunParty() override {
    promise_detail::Context<grpc_event_engine::experimental::EventEngine>
        ee_ctx(ee_.get());
    std::cout << "run party "
              << "\n";
    fflush(stdout);
    return Party::RunParty();
  }
  void PartyOver() override {
    {
      promise_detail::Context<grpc_event_engine::experimental::EventEngine>
          ee_ctx(ee_.get());
      std::cout << "party over "
                << "\n";
      fflush(stdout);
      CancelRemainingParticipants();
    }
    delete this;
  }
  void SetStreamId(uint32_t stream_id) { stream_id_ = stream_id; }

  auto PushServerInitialMetadata(ServerMetadataHandle metadata) {
    GPR_ASSERT(pipe_server_initial_metadata_ != nullptr);
    GPR_ASSERT(Activity::current() == this);
    return pipe_server_initial_metadata_->sender.Push(std::move(metadata));
  }
  auto PushServerToClientMessage(MessageHandle message) {
    GPR_ASSERT(pipe_server_to_client_messages_ != nullptr);
    GPR_ASSERT(Activity::current() == this);
    return pipe_server_to_client_messages_->sender.Push(std::move(message));
  }
  auto PushClientToServerMessage(MessageHandle message) {
    GPR_ASSERT(pipe_client_to_server_messages_ != nullptr);
    GPR_ASSERT(Activity::current() == this);
    std::cout << "push client to server message "
              << "\n";
    fflush(stdout);
    return pipe_client_to_server_messages_->sender.Push(std::move(message));
  }
  auto PullServerInitialMetadata() {
    GPR_ASSERT(pipe_server_initial_metadata_ != nullptr);
    GPR_ASSERT(Activity::current() == this);
    return pipe_server_initial_metadata_->receiver.Next();
  }
  auto PullServerToClientMessage() {
    GPR_ASSERT(pipe_server_to_client_messages_ != nullptr);
    GPR_ASSERT(Activity::current() == this);
    std::cout << "pull server to client message "
              << "\n";
    fflush(stdout);
    return pipe_server_to_client_messages_->receiver.Next();
  }
  auto PullClientToServerMessage() {
    GPR_ASSERT(pipe_client_to_server_messages_ != nullptr);
    GPR_ASSERT(Activity::current() == this);
    return pipe_client_to_server_messages_->receiver.Next();
  }
  uint32_t GetStreamId() { return stream_id_; }
  void CloseClientToServerPipe() {
    GPR_ASSERT(Activity::current() == this);
    pipe_client_to_server_messages_->sender.Close();
    pipe_client_to_server_messages_->receiver.AwaitClosed();
    std::cout << "close client to server pipe "
              << "\n";
    fflush(stdout);
  }
  void CloseServerToClientPipe() {
    GPR_ASSERT(Activity::current() == this);
    pipe_server_to_client_messages_->sender.Close();
    pipe_server_to_client_messages_->receiver.AwaitClosed();
    std::cout << "close server to client pipe "
              << "\n";
    fflush(stdout);
  }
  void CloseServerInitialMetadataPipe() {
    GPR_ASSERT(Activity::current() == this);
    pipe_server_initial_metadata_->sender.Close();
    pipe_server_initial_metadata_->receiver.AwaitClosed();
    std::cout << "close server to client pipe "
              << "\n";
    fflush(stdout);
  }

 private:
  grpc_event_engine::experimental::EventEngine* event_engine() const final {
    return ee_.get();
  }
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> ee_;
  std::shared_ptr<Arena> arena_;
  uint32_t stream_id_;
  std::unique_ptr<Pipe<MessageHandle>> pipe_client_to_server_messages_;
  std::unique_ptr<Pipe<MessageHandle>> pipe_server_to_client_messages_;
  std::unique_ptr<Pipe<ServerMetadataHandle>> pipe_server_initial_metadata_;
};
class CallInitiator {
 public:
  explicit CallInitiator(RefCountedPtr<Call> call) : call_(call) {}
  // Returns a promise that push/pull message/metadata from corresponding pipe.
  auto PushServerInitialMetadata(ServerMetadataHandle metadata) {
    GPR_ASSERT(Activity::current() == call_->current());
    return call_->PushServerInitialMetadata(std::move(metadata));
  }
  auto PushServerToClientMessage(MessageHandle message) {
    GPR_ASSERT(Activity::current() == call_->current());
    return call_->PushServerToClientMessage(std::move(message));
  }
  auto PushClientToServerMessage(MessageHandle message) {
    GPR_ASSERT(Activity::current() == call_->current());
    return call_->PushClientToServerMessage(std::move(message));
  }
  auto PullServerInitialMetadata() {
    GPR_ASSERT(Activity::current() == call_->current());
    return call_->PullServerInitialMetadata();
  }
  auto PullServerToClientMessage() {
    GPR_ASSERT(Activity::current() == call_->current());
    return call_->PullServerToClientMessage();
  }
  auto PullClientToServerMessage() {
    GPR_ASSERT(Activity::current() == call_->current());
    return call_->PullClientToServerMessage();
  }
  uint32_t GetStreamId() { return call_->GetStreamId(); }
  template <typename Promise>
  void SpawnWaitable(Promise p) {
    GPR_ASSERT(Activity::current() == call_->current());
    call_->SpawnWaitable("run_transport_promise_waitable", std::move(p));
  }
  template <typename Promise, typename Complete>
  void Spawn(Promise p, Complete c) {
    GPR_ASSERT(Activity::current() == call_->current());
    call_->Spawn("run_transport_promise", std::move(p), std::move(c));
  }
  void CloseClientToServerPipe() {
    GPR_ASSERT(Activity::current() == call_->current());
    call_->CloseClientToServerPipe();
  }
  void CloseServerToClientPipe() {
    GPR_ASSERT(Activity::current() == call_->current());
    call_->CloseServerToClientPipe();
  }
  void CloseServerInitialMetadataPipe() {
    GPR_ASSERT(Activity::current() == call_->current());
    call_->CloseServerInitialMetadataPipe();
  }

 private:
  RefCountedPtr<Call> call_;
};

class ServerTransport {
 public:
  using AcceptFn =
      absl::AnyInvocable<std::shared_ptr<CallInitiator>(ClientMetadata&) const>;
  ServerTransport(std::unique_ptr<PromiseEndpoint> control_endpoint,
                  std::unique_ptr<PromiseEndpoint> data_endpoint,
                  std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                      event_engine,
                  AcceptFn accept_fn);
  ~ServerTransport() {
    if (writer_ != nullptr) {
      writer_.reset();
    }
    if (reader_ != nullptr) {
      reader_.reset();
    }
  }
  void AbortWithError() {
    // Mark transport as unavailable when the endpoint write/read failed.
    // Close all the available pipes.
    std::cout << "abort with error "
              << "\n";
    fflush(stdout);
    if (!outgoing_frames_.receiver.IsClose()) {
      outgoing_frames_.receiver.MarkClose();
    }
    MutexLock lock(&mu_);
    for (const auto& pair : stream_map_) {
      if (!pair.second->IsClose()) {
        pair.second->MarkClose();
      }
    }
  }

 private:
  void AddCall(std::shared_ptr<CallInitiator> r) {
    // Server write.
    auto write_loop = Loop([this]() mutable {
      return TrySeq(
          // Get next outgoing frame.
          outgoing_frames_.receiver.Next(),
          // Construct data buffers that will be sent to the endpoints.
          [this](absl::optional<ServerFrame> server_frame) {
            GPR_ASSERT(server_frame.has_value());
            ServerFragmentFrame frame =
                std::move(absl::get<ServerFragmentFrame>(server_frame.value()));
            control_endpoint_write_buffer_.Append(
                frame.Serialize(hpack_compressor_.get()));
            if (frame.message != nullptr) {
              auto frame_header =
                  FrameHeader::Parse(
                      reinterpret_cast<const uint8_t*>(GRPC_SLICE_START_PTR(
                          control_endpoint_write_buffer_.c_slice_buffer()
                              ->slices[0])))
                      .value();
              // TODO(ladynana): add message_padding calculation by
              // accumulating bytes sent.
              std::string message_padding(frame_header.message_padding, '0');
              Slice slice(grpc_slice_from_cpp_string(message_padding));
              // Append message payload to data_endpoint_buffer.
              data_endpoint_write_buffer_.Append(std::move(slice));
              // Append message payload to data_endpoint_buffer.
              frame.message->payload()->MoveFirstNBytesIntoSliceBuffer(
                  frame.message->payload()->Length(),
                  data_endpoint_write_buffer_);
            }
            return absl::OkStatus();
          },
          // Write buffers to corresponding endpoints concurrently.
          [this]() {
            return TryJoin(
                control_endpoint_->Write(
                    std::move(control_endpoint_write_buffer_)),
                data_endpoint_->Write(std::move(data_endpoint_write_buffer_)));
          },
          // Finish writes to difference endpoints and continue the loop.
          []() -> LoopCtl<absl::Status> {
            // The write failures will be caught in TrySeq and exit loop.
            // Therefore, only need to return Continue() in the last lambda
            // function.
            return Continue();
          });
    });
    // r->Spawn(std::move(write_loop), [](absl::Status){});
    // Add server write promise.
    auto server_write = Loop([r, this]() mutable {
      return TrySeq(
          // TODO(ladynana): add initial metadata in server frame.
          r->PullServerToClientMessage(),
          [stream_id = r->GetStreamId(), r,
           this](NextResult<MessageHandle> result) mutable {
            bool has_result = result.has_value();
            return If(
                has_result,
                [this, result = std::move(result), stream_id]() mutable {
                  std::cout << "write promise get message "
                            << "\n";
                  fflush(stdout);
                  ServerFragmentFrame frame;
                  uint32_t message_length = result.value()->payload()->Length();
                  uint32_t message_padding = message_length % aligned_bytes;
                  frame.frame_header = FrameHeader{
                      FrameType::kFragment, {}, stream_id, 0, message_length,
                      message_padding,      0};
                  frame.message = std::move(result.value());
                  return Seq(
                      outgoing_frames_.sender.Push(
                          ServerFrame(std::move(frame))),
                      [](bool success) -> LoopCtl<absl::Status> {
                        if (!success) {
                          // TODO(ladynana): propagate the actual error message
                          // from EventEngine.
                          return absl::UnavailableError(
                              "Transport closed due to endpoint write/read "
                              "failed.");
                        }
                        std::cout << "write promise continue "
                                  << "\n";
                        fflush(stdout);
                        return Continue();
                      });
                },
                []() -> LoopCtl<absl::Status> {
                  std::cout << "write promise failed "
                            << "\n";
                  fflush(stdout);
                  return absl::UnavailableError(
                      "Transport closed due to endpoint write/read "
                      "failed.");
                });
          });
    });
    // r->Spawn(std::move(server_write), [](absl::Status){});
    auto stream_id = r->GetStreamId();
    pipe_client_frames_ = std::make_shared<
        InterActivityPipe<ClientFrame, client_frame_queue_size_>>();
    {
      MutexLock lock(&mu_);
      if (stream_map_.count(stream_id) <= 0) {
        stream_map_.insert(
            std::pair<uint32_t,
                      std::shared_ptr<InterActivityPipe<
                          ClientFrame, client_frame_queue_size_>::Sender>>(
                stream_id, std::make_shared<InterActivityPipe<
                               ClientFrame, client_frame_queue_size_>::Sender>(
                               std::move(pipe_client_frames_->sender))));
      }
    }
    auto server_read = Loop([r, this]() mutable {
      return TrySeq(
          pipe_client_frames_->receiver.Next(),
          [r](absl::optional<ClientFrame> client_frame) mutable {
            bool has_frame = client_frame.has_value();
            GPR_ASSERT(r != nullptr);
            return If(
                has_frame,
                [r, client_frame = std::move(client_frame)]() mutable {
                  GPR_ASSERT(r != nullptr);
                  GPR_ASSERT(client_frame.has_value());
                  auto frame = std::move(
                      absl::get<ClientFragmentFrame>(client_frame.value()));
                  std::cout << "receive frame from read "
                            << "\n";
                  fflush(stdout);
                  return Seq(
                      r->PushClientToServerMessage(std::move(frame.message)),
                      [](bool success) -> LoopCtl<absl::Status> {
                        if (!success) {
                          // TODO(ladynana): propagate the actual error message
                          // from EventEngine.
                          return absl::UnavailableError(
                              "Transport closed due to endpoint write/read "
                              "failed.");
                        }
                        std::cout << "read promise continue "
                                  << "\n";
                        fflush(stdout);
                        return Continue();
                      });
                },
                []() -> LoopCtl<absl::Status> {
                  std::cout << "read clientframe failed "
                            << "\n";
                  fflush(stdout);
                  return absl::UnavailableError(
                      "Transport closed due to endpoint write/read "
                      "failed.");
                });
          });
    });
    auto call_promise = TrySeq(
        TryJoin(std::move(server_read), std::move(server_write),
                std::move(write_loop)),
        [](std::tuple<Empty, Empty, Empty>) { return absl::OkStatus(); });
    r->Spawn(std::move(call_promise), [](absl::Status) {});
  }
  AcceptFn accept_fn_;
  // Queue size of each stream pipe is set to 2, so that for each stream read it
  // will queue at most 2 frames.
  static const size_t server_frame_queue_size_ = 2;
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  InterActivityPipe<ServerFrame, server_frame_queue_size_> outgoing_frames_;
  // Queue size of each stream pipe is set to 2, so that for each stream read it
  // will queue at most 2 frames.
  static const size_t client_frame_queue_size_ = 2;
  Mutex mu_;
  // Map of stream incoming server frames, key is stream_id.
  std::map<uint32_t, std::shared_ptr<InterActivityPipe<
                         ClientFrame, client_frame_queue_size_>::Sender>>
      stream_map_ ABSL_GUARDED_BY(mu_);
  // Assigned aligned bytes from setting frame.
  size_t aligned_bytes = 64;
  ActivityPtr writer_;
  ActivityPtr reader_;
  std::unique_ptr<PromiseEndpoint> control_endpoint_;
  std::unique_ptr<PromiseEndpoint> data_endpoint_;
  SliceBuffer control_endpoint_write_buffer_;
  SliceBuffer data_endpoint_write_buffer_;
  SliceBuffer control_endpoint_read_buffer_;
  SliceBuffer data_endpoint_read_buffer_;
  std::unique_ptr<HPackCompressor> hpack_compressor_;
  std::unique_ptr<HPackParser> hpack_parser_;
  std::shared_ptr<FrameHeader> frame_header_;
  MemoryAllocator memory_allocator_;
  ScopedArenaPtr arena_;
  promise_detail::Context<Arena> context_;
  // Use to synchronize writer_ and reader_ activity with outside activities;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  std::shared_ptr<CallInitiator> call_initiator_;
  std::shared_ptr<InterActivityPipe<ClientFrame, client_frame_queue_size_>>
      pipe_client_frames_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_TRANSPORT_H