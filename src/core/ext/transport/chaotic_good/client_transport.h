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

#include <cstdint>
#include <initializer_list>  // IWYU pragma: keep
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>

#include "src/core/ext/transport/chaotic_good/chaotic_good_transport.h"
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

class ChaoticGoodClientTransport final : public Transport,
                                         public ClientTransport {
 public:
  ChaoticGoodClientTransport(
      PromiseEndpoint control_endpoint, PromiseEndpoint data_endpoint,
      const ChannelArgs& channel_args,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      HPackParser hpack_parser, HPackCompressor hpack_encoder);
  ~ChaoticGoodClientTransport() override;

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return this; }
  ServerTransport* server_transport() override { return nullptr; }
  absl::string_view GetTransportName() const override { return "chaotic_good"; }
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
  void PerformOp(grpc_transport_op*) override;
  grpc_endpoint* GetEndpoint() override { return nullptr; }
  void Orphan() override {
    AbortWithError();
    delete this;
  }

  void StartCall(CallHandler call_handler) override;
  void AbortWithError();

 private:
  // Queue size of each stream pipe is set to 2, so that for each stream read it
  // will queue at most 2 frames.
  static const size_t kServerFrameQueueSize = 2;
  using StreamMap = absl::flat_hash_map<uint32_t, CallHandler>;

  uint32_t MakeStream(CallHandler call_handler);
  absl::optional<CallHandler> LookupStream(uint32_t stream_id);
  auto CallOutboundLoop(uint32_t stream_id, CallHandler call_handler);
  auto OnTransportActivityDone();
  auto TransportWriteLoop(RefCountedPtr<ChaoticGoodTransport> transport);
  auto TransportReadLoop(RefCountedPtr<ChaoticGoodTransport> transport);
  // Push one frame into a call
  auto PushFrameIntoCall(ServerFragmentFrame frame, CallHandler call_handler);

  grpc_event_engine::experimental::MemoryAllocator allocator_;
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<ClientFrame> outgoing_frames_;
  // Assigned aligned bytes from setting frame.
  size_t aligned_bytes_ = 64;
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  // Map of stream incoming server frames, key is stream_id.
  StreamMap stream_map_ ABSL_GUARDED_BY(mu_);
  ActivityPtr writer_;
  ActivityPtr reader_;
  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(mu_){
      "chaotic_good_client", GRPC_CHANNEL_READY};
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H
