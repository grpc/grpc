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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H

#include <grpc/support/port_platform.h>

#include <stdint.h>
#include <stdio.h>

#include <initializer_list>  // IWYU pragma: keep
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/inter_activity_pipe.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"  // IWYU pragma: keep
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  ClientTransport(std::unique_ptr<PromiseEndpoint> control_endpoint,
                  std::unique_ptr<PromiseEndpoint> data_endpoint,
                  std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                      event_engine);
  ~ClientTransport() {
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
    MutexLock lock(&mu_);
    for (const auto& pair : stream_map_) {
      if (!pair.second->IsClose()) {
        pair.second->MarkClose();
      }
    }
  }
  auto AddStream(CallArgs call_args) {
    // At this point, the connection is set up.
    // Start sending data frames.
    uint32_t stream_id;
    InterActivityPipe<ServerFrame, server_frame_queue_size_> pipe_server_frames;
    {
      MutexLock lock(&mu_);
      stream_id = next_stream_id_++;
      stream_map_.insert(
          std::pair<uint32_t,
                    std::shared_ptr<InterActivityPipe<
                        ServerFrame, server_frame_queue_size_>::Sender>>(
              stream_id, std::make_shared<InterActivityPipe<
                             ServerFrame, server_frame_queue_size_>::Sender>(
                             std::move(pipe_server_frames.sender))));
    }
    return TrySeq(
        TryJoin<absl::StatusOr>(
            // Continuously send client frame with client to server messages.
            ForEach(std::move(*call_args.client_to_server_messages),
                    [stream_id, initial_frame = true,
                     client_initial_metadata =
                         std::move(call_args.client_initial_metadata),
                     outgoing_frames = outgoing_frames_.MakeSender(),
                     this](MessageHandle result) mutable {
                      ClientFragmentFrame frame;
                      // Construct frame header (flags, header_length and
                      // trailer_length will be added in serialization).
                      uint32_t message_length = result->payload()->Length();
                      frame.stream_id = stream_id;
                      frame.message_padding = message_length % aligned_bytes;
                      frame.message = std::move(result);
                      if (initial_frame) {
                        // Send initial frame with client intial metadata.
                        frame.headers = std::move(client_initial_metadata);
                        initial_frame = false;
                      }
                      return TrySeq(
                          outgoing_frames.Send(ClientFrame(std::move(frame))),
                          [](bool success) -> absl::Status {
                            if (!success) {
                              // TODO(ladynana): propagate the actual error
                              // message from EventEngine.
                              return absl::UnavailableError(
                                  "Transport closed due to endpoint write/read "
                                  "failed.");
                            }
                            return absl::OkStatus();
                          });
                    }),
            // Continuously receive server frames from endpoints and save
            // results to call_args.
            Loop([server_initial_metadata = call_args.server_initial_metadata,
                  server_to_client_messages =
                      call_args.server_to_client_messages,
                  receiver = std::move(pipe_server_frames.receiver)]() mutable {
              return TrySeq(
                  // Receive incoming server frame.
                  receiver.Next(),
                  // Save incomming frame results to call_args.
                  [server_initial_metadata, server_to_client_messages](
                      absl::optional<ServerFrame> server_frame) mutable {
                    bool transport_closed = false;
                    ServerFragmentFrame frame;
                    if (!server_frame.has_value()) {
                      // Incoming server frame pipe is closed, which only
                      // happens when transport is aborted.
                      transport_closed = true;
                    } else {
                      frame = std::move(
                          absl::get<ServerFragmentFrame>(*server_frame));
                    };
                    bool has_headers = (frame.headers != nullptr);
                    bool has_message = (frame.message != nullptr);
                    bool has_trailers = (frame.trailers != nullptr);
                    return TrySeq(
                        If((!transport_closed) && has_headers,
                           [server_initial_metadata,
                            headers = std::move(frame.headers)]() mutable {
                             return server_initial_metadata->Push(
                                 std::move(headers));
                           },
                           [] { return false; }),
                        If((!transport_closed) && has_message,
                           [server_to_client_messages,
                            message = std::move(frame.message)]() mutable {
                             return server_to_client_messages->Push(
                                 std::move(message));
                           },
                           [] { return false; }),
                        If((!transport_closed) && has_trailers,
                           [trailers = std::move(frame.trailers)]() mutable
                           -> LoopCtl<ServerMetadataHandle> {
                             return std::move(trailers);
                           },
                           [transport_closed]()
                               -> LoopCtl<ServerMetadataHandle> {
                             if (transport_closed) {
                               // TODO(ladynana): propagate the actual error
                               // message from EventEngine.
                               return ServerMetadataFromStatus(
                                   absl::UnavailableError(
                                       "Transport closed due to endpoint "
                                       "write/read failed."));
                             }
                             return Continue();
                           }));
                  });
            })),
        [](std::tuple<Empty, ServerMetadataHandle> ret) {
          return std::move(std::get<1>(ret));
        });
  }

 private:
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<ClientFrame> outgoing_frames_;
  // Queue size of each stream pipe is set to 2, so that for each stream read it
  // will queue at most 2 frames.
  static const size_t server_frame_queue_size_ = 2;
  // Assigned aligned bytes from setting frame.
  size_t aligned_bytes = 64;
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  // Map of stream incoming server frames, key is stream_id.
  std::map<uint32_t, std::shared_ptr<InterActivityPipe<
                         ServerFrame, server_frame_queue_size_>::Sender>>
      stream_map_ ABSL_GUARDED_BY(mu_);
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

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H
