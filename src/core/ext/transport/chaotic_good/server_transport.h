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

#include <cstddef>
#include <initializer_list>  // IWYU pragma: keep
#include <memory>
#include <utility>
#include <variant>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

// Prototype based on gRPC Call 3.0.
// TODO(ladynana): convert to the true Call/CallInitiator once available.
struct CallData {
  uint32_t stream_id;
  Pipe<MessageHandle> pipe_client_to_server_messages_;
  Pipe<MessageHandle> pipe_server_to_client_messages_;
  Pipe<ServerMetadataHandle> pipe_server_intial_metadata_;
};
class CallInitiator {
 public:
  explicit CallInitiator(std::unique_ptr<CallData> call_data)
      : call_(std::move(call_data)) {}
  // Returns a promise that push/pull message/metadata from corresponding pipe.
  auto PushServerInitialMetadata(ServerMetadataHandle metadata) {
    return call_->pipe_server_intial_metadata_.sender.Push(std::move(metadata));
  }
  auto PushServerToClientMessage(MessageHandle message) {
    return call_->pipe_server_to_client_messages_.sender.Push(
        std::move(message));
  }
  auto PushClientToServerMessage(MessageHandle message) {
    return call_->pipe_client_to_server_messages_.sender.Push(
        std::move(message));
  }
  auto PullServerInitialMetadata() {
    return call_->pipe_server_intial_metadata_.receiver.Next();
  }
  auto PullServerToClientMessage() {
    return call_->pipe_server_to_client_messages_.receiver.Next();
  }
  auto PullClientToServerMessage() {
    return call_->pipe_client_to_server_messages_.receiver.Next();
  }
  uint32_t GetStreamId() { return call_->stream_id; }
  void SetCall(std::unique_ptr<CallData> call_data) {
    call_ = std::move(call_data);
  }
  template <typename Promise>
  void Spawn(Promise p) {
    // TODO(ladynana): call/implement Spawn method such as party->Spawn.
    // party_->Spawn("Run promise", std::move(p), [](){return
    // absl::OkStatus();});
  }

 private:
  std::unique_ptr<CallData> call_;
};

class ServerTransport {
 public:
  using AcceptFn = absl::AnyInvocable<CallInitiator(ClientMetadata&) const>;
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
    if (!outgoing_frames_.IsClosed()) {
      outgoing_frames_.MarkClosed();
    }
    // MutexLock lock(&mu_);
    // for (const auto& pair : stream_map_) {
    //   if (!pair.second->IsClose()) {
    //     pair.second->MarkClose();
    //   }
    // }
  }

 private:
  void AddCall(CallInitiator& r) {
    // Add server write promise.
    auto server_write = Loop([&r, this] {
      return TrySeq(
          // TODO(ladynana): add initial metadata in server frame.
          r.PullServerToClientMessage(),
          [this, stream_id = r.GetStreamId()](
              NextResult<MessageHandle> result) mutable {
            auto outgoing_frames = outgoing_frames_.MakeSender();
            ServerFragmentFrame frame;
            uint32_t message_length = result.value()->payload()->Length();
            uint32_t message_padding = message_length % aligned_bytes;
            frame.frame_header = FrameHeader{
                FrameType::kFragment, {}, stream_id, 0, message_length,
                message_padding,      0};
            frame.message = std::move(*result);
            return outgoing_frames.Send(ServerFrame(std::move(frame)));
          },
          [](bool success) -> LoopCtl<absl::Status> {
            if (!success) {
              // TODO(ladynana): propagate the actual error message
              // from EventEngine.
              return absl::UnavailableError(
                  "Transport closed due to endpoint write/read "
                  "failed.");
            }
            return Continue();
          });
    });
    r.Spawn(std::move(server_write));
  }
  AcceptFn accept_fn_;
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<ServerFrame> outgoing_frames_;
  static const size_t client_frame_queue_size_ = 2;
  // Assigned aligned bytes from setting frame.
  size_t aligned_bytes = 64;
  // Mutex mu_;
  // // Map of outgoing client frames, key is stream_id.
  // std::map<uint32_t, std::shared_ptr<InterActivityPipe<
  //                        ClientFrame, client_frame_queue_size_>::Receiver>>
  //                        stream_map_
  //     ABSL_GUARDED_BY(mu_);
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
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_TRANSPORT_H