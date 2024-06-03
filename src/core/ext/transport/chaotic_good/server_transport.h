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

#include <stdint.h>
#include <stdio.h>

#include <cstdint>
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
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/chaotic_good_transport.h"
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
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/inter_activity_pipe.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/mpsc.h"
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

class ChaoticGoodServerTransport final : public ServerTransport {
 public:
  ChaoticGoodServerTransport(
      const ChannelArgs& args, PromiseEndpoint control_endpoint,
      PromiseEndpoint data_endpoint,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      HPackParser hpack_parser, HPackCompressor hpack_encoder);

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return nullptr; }
  ServerTransport* server_transport() override { return this; }
  absl::string_view GetTransportName() const override { return "chaotic_good"; }
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
  void PerformOp(grpc_transport_op*) override;
  void Orphan() override;

  void SetCallDestination(
      RefCountedPtr<UnstartedCallDestination> call_destination) override;
  void AbortWithError();

 private:
  using StreamMap = absl::flat_hash_map<uint32_t, CallInitiator>;

  absl::Status NewStream(uint32_t stream_id, CallInitiator call_initiator);
  absl::optional<CallInitiator> LookupStream(uint32_t stream_id);
  absl::optional<CallInitiator> ExtractStream(uint32_t stream_id);
  auto SendCallInitialMetadataAndBody(uint32_t stream_id,
                                      MpscSender<ServerFrame> outgoing_frames,
                                      CallInitiator call_initiator);
  auto SendCallBody(uint32_t stream_id, MpscSender<ServerFrame> outgoing_frames,
                    CallInitiator call_initiator);
  static auto SendFragment(ServerFragmentFrame frame,
                           MpscSender<ServerFrame> outgoing_frames,
                           CallInitiator call_initiator);
  auto CallOutboundLoop(uint32_t stream_id, CallInitiator call_initiator);
  auto OnTransportActivityDone(absl::string_view activity);
  auto TransportReadLoop(RefCountedPtr<ChaoticGoodTransport> transport);
  auto ReadOneFrame(ChaoticGoodTransport& transport);
  auto TransportWriteLoop(RefCountedPtr<ChaoticGoodTransport> transport);
  // Read different parts of the server frame from control/data endpoints
  // based on frame header.
  // Resolves to a StatusOr<tuple<SliceBuffer, SliceBuffer>>
  auto ReadFrameBody(Slice read_buffer);
  void SendCancel(uint32_t stream_id, absl::Status why);
  auto DeserializeAndPushFragmentToNewCall(FrameHeader frame_header,
                                           BufferPair buffers,
                                           ChaoticGoodTransport& transport);
  auto DeserializeAndPushFragmentToExistingCall(
      FrameHeader frame_header, BufferPair buffers,
      ChaoticGoodTransport& transport);
  auto MaybePushFragmentIntoCall(absl::optional<CallInitiator> call_initiator,
                                 absl::Status error, ClientFragmentFrame frame,
                                 uint32_t stream_id);
  auto PushFragmentIntoCall(CallInitiator call_initiator,
                            ClientFragmentFrame frame, uint32_t stream_id);

  RefCountedPtr<UnstartedCallDestination> call_destination_;
  const RefCountedPtr<CallArenaAllocator> call_arena_allocator_;
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;
  InterActivityLatch<void> got_acceptor_;
  MpscReceiver<ServerFrame> outgoing_frames_;
  // Assigned aligned bytes from setting frame.
  size_t aligned_bytes_ = 64;
  Mutex mu_;
  // Map of stream incoming server frames, key is stream_id.
  StreamMap stream_map_ ABSL_GUARDED_BY(mu_);
  uint32_t last_seen_new_stream_id_ = 0;
  ActivityPtr writer_ ABSL_GUARDED_BY(mu_);
  ActivityPtr reader_ ABSL_GUARDED_BY(mu_);
  ConnectivityStateTracker state_tracker_ ABSL_GUARDED_BY(mu_){
      "chaotic_good_server", GRPC_CHANNEL_READY};
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SERVER_TRANSPORT_H
