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
#include <map>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/ext/transport/chaotic_good/frame_header.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ServerTransport {
 public:
  ServerTransport(
      absl::AnyInvocable<ArenaPromise<ServerMetadataHandle>(CallArgs)>
          start_receive_callback,
      std::unique_ptr<PromiseEndpoint> control_endpoint,
      std::unique_ptr<PromiseEndpoint> data_endpoint,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine);
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
    std::map<uint32_t, std::shared_ptr<CallData>> stream_map;
    {
      MutexLock lock(&mu_);
      stream_map = stream_map_;
    }
    for (const auto& pair : stream_map) {
      auto call_data = pair.second;
      call_data->pipe_client_to_server_messages_.receiver.CloseWithError();
      call_data->pipe_server_intial_metadata_.sender.CloseWithError();
      call_data->pipe_server_to_client_messages_.sender.CloseWithError();
    }
  }

  // Prototype of what start_receive_callback will do.
  // ArenaPromise<ServerMetadataHandle> start_receive_callback (CallArgs
  // callargs){
  //   return TrySeq(
  //     ProcessClientInitialMetadata(callargs.client_initial_metadata),
  //     ForEach(callargs.client_to_server_messages,
  //     [](MessageHandle message){
  //       ProcessClientMessage();
  //     }),
  //     // Send server initial metadata to client.
  //     callargs.server_initial_metadata->Push(md),
  //     // Send server message to client.
  //     callargs.server_to_client_messages->Push(md)
  //   );
  // }

 private:
  struct CallData {
    Pipe<MessageHandle> pipe_client_to_server_messages_;
    Pipe<MessageHandle> pipe_server_to_client_messages_;
    Pipe<ServerMetadataHandle> pipe_server_intial_metadata_;
  };
  // Construct call data of each stream
  CallData* ConstructCallData(uint32_t stream_id) {
    MutexLock lock(&mu_);
    auto iter = stream_map_.find(stream_id);
    if (iter != stream_map_.end()) {
      return stream_map_[stream_id].get();
    } else {
      CallData call_data{Pipe<MessageHandle>(arena_.get()),
                         Pipe<MessageHandle>(arena_.get()),
                         Pipe<ServerMetadataHandle>(arena_.get())};
      stream_map_[stream_id] = std::make_shared<CallData>(std::move(call_data));
      return stream_map_[stream_id].get();
    }
  }
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<ServerFrame> outgoing_frames_;
  static const size_t client_frame_queue_size_ = 2;
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  // Map of stream outgoing client frames, key is stream_id.
  std::map<uint32_t, std::shared_ptr<CallData>> stream_map_
      ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<ArenaPromise<ServerMetadataHandle>(CallArgs)>
      start_receive_callback_;
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