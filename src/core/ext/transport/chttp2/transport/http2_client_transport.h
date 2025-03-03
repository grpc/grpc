//
//
// Copyright 2024 gRPC authors.
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
//
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H

#include <cstdint>
#include <utility>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace http2 {

// All Promise Based HTTP2 Transport TODOs have the tag
// [PH2][Pn] where n = 0 to 5.
// This helps to maintain the uniformity for quick lookup and fixing.
//
// [PH2][P0] Must be fixed before the current PR is submitted.
// [PH2][P1] Must be fixed before the current sub-project is considered
//           complete.
// [PH2][P2] Must be fixed before the current Milestone is considered
//           complete.
// [PH2][P3] Must be fixed before Milestone 3 is considered complete.
// [PH2][P4] Can be fixed after roll out begins. Evaluate these during
//           Milestone 4. Either do the TODOs or delete them.
// [PH2][P5] This must be a separate standalone project.

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Update the experimental status of the code before
// http2 rollout begins.
class Http2ClientTransport final : public ClientTransport {
  // TODO(tjagtap) : [PH2][P3] Move the definitions to the header for better
  // inlining. For now definitions are in the cc file to
  // reduce cognitive load in the header.
 public:
  Http2ClientTransport(
      PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine);

  ~Http2ClientTransport() override;

  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return this; }
  ServerTransport* server_transport() override { return nullptr; }
  absl::string_view GetTransportName() const override { return "http2"; }

  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}

  // Called at the start of a stream.
  void StartCall(CallHandler call_handler) override;

  void PerformOp(grpc_transport_op*) override;

  void Orphan() override;
  void AbortWithError();

 private:
  // Reading from the endpoint.

  // Returns a promise to keep reading in a Loop till a fail/close is received.
  auto ReadLoop();

  // Returns a promise that will read and process one HTTP2 frame.
  auto ReadAndProcessOneFrame();

  // Returns a promise that will process one HTTP2 frame.
  auto ProcessOneFrame(Http2Frame frame);

  // Returns a promise that will do the cleanup after the ReadLoop ends.
  auto OnReadLoopEnded();

  // Writing to the endpoint.

  // Read from the MPSC queue and write it.
  auto WriteFromQueue();

  // Returns a promise to keep writing in a Loop till a fail/close is received.
  auto WriteLoop();

  // Returns a promise that will do the cleanup after the WriteLoop ends.
  auto OnWriteLoopEnded();

  RefCountedPtr<Party> general_party_;
  RefCountedPtr<Party> write_party_;

  PromiseEndpoint endpoint_;
  Http2Settings settings_;

  // TODO(tjagtap) : [PH2][P3] : This is not nice. Fix by using Stapler.
  Http2FrameHeader current_frame_header_;

  struct Stream : public RefCounted<Stream> {
    explicit Stream(CallHandler call) : call(std::move(call)) {}
    // Transport holds one CallHandler object for each Stream.
    CallHandler call;
    // TODO(tjagtap) : [PH2][P2] : Add more members as necessary
  };

  Mutex stream_list_mutex_;
  // TODO(tjagtap) : [PH2][P2] : Add to map in StartCall and clean this mapping
  // up in the on_done of the CallInitiator or CallHandler
  absl::flat_hash_map<uint32_t, RefCountedPtr<Stream>> stream_list_
      ABSL_GUARDED_BY(stream_list_mutex_);
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_CLIENT_TRANSPORT_H
