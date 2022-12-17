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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/client_transport.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_concurrently.h"

namespace grpc_core {
namespace chaotic_good {

ClientTransport::ClientTransport(PromiseEndpoint endpoint) {
  auto hpack_compressor = std::make_shared<HPackCompressor>();
  auto ep = std::make_shared<PromiseEndpoint>(std::move(endpoint));
  writer_ = MakeActivity(
      ForEach(outgoing_frames_,
              [hpack_compressor, ep](ClientFrame frame) {
                return ep->Write(
                    Match(frame, [hpack_compressor = hpack_compressor.get()](
                                     const FrameInterface& f) {
                      return f.Serialize(hpack_compressor);
                    }))
              }),
      EventEngineWakeupScheduler(), [] {
        // TODO(ctiller): Figure this out
        abort();
      });
}

ArenaPromise<ServerMetadataHandle> ClientTransport::CreateStream(
    CallArgs call_args) {
  ClientFragmentFrame initial_frame;
  NextResult<MessageHandle> next_message;
  initial_frame.headers = std::move(call_args.client_initial_metadata);
  initial_frame.end_of_stream = false;

  // Poll once to see if we can send a message with the first frame.
  auto first_message = call_args.outgoing_messages->Next()();
  if (auto* message = absl::get_if<NextResult<MessageHandle>>(&first_message)) {
    if (message->has_value()) {
      initial_frame.message = std::move(**message);
      initial_frame.end_of_stream = call_args.outgoing_messages->Closed();
    } else {
      initial_frame.end_of_stream = true;
    }
  }

  {
    MutexLock lock(&mu_);
    initial_frame.stream_id = next_stream_id_++;
  }

  bool reached_end_of_stream = initial_frame.end_of_stream;
  uint32_t stream_id = initial_frame.stream_id;
  return TryConcurrently(
             // TODO(ctiller): Read path should go here!
             Never<ServerMetadataHandle>())
      .Push(
          Seq(outgoing_frames_.Push(std::move(initial_frame)),
              If(reached_end_of_stream, ImmediateOkStatus(),
                 ForEach(std::move(*call_args.outgoing_messages),
                         [stream_id, this](NextResult<MessageHandle> message) {
                           ClientFragmentFrame frame;
                           frame.stream_id = stream_id;
                           if (message.has_value()) {
                             frame.message = std::move(*message);
                             frame.end_of_stream = false;
                           } else {
                             frame.end_of_stream = true;
                           }
                           return outgoing_frames_.Push(std::move(frame));
                         }))));
}

}  // namespace chaotic_good
}  // namespace grpc_core
