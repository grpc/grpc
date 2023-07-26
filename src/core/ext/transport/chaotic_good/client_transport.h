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

#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/variant.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  ClientTransport(const ChannelArgs& channel_args,
                  std::unique_ptr<PromiseEndpoint> control_endpoint_,
                  std::unique_ptr<PromiseEndpoint> data_endpoint_);
  auto AddStream(CallArgs call_args) {
    // At this point, the connection is set up.
    // Start sending data frames.
    uint64_t stream_id;
    {
      MutexLock lock(&mu_);
      stream_id = next_stream_id_++;
    }
    return Seq(
        [stream_id, outgoing_frames = outgoing_frames_.MakeSender(),
         client_initial_metadata =
             std::move(call_args.client_initial_metadata)]() mutable {
          // TODO(): consider getting the first message here if it's available.
          ClientFragmentFrame frame;
          frame.stream_id = stream_id;
          frame.headers = std::move(client_initial_metadata);
          frame.end_of_stream = false;
          return outgoing_frames.Send(ClientFrame(std::move(frame)));
        },
        ForEach(std::move(*call_args.client_to_server_messages),
                [stream_id, outgoing_frames = outgoing_frames_.MakeSender()](
                    MessageHandle result) mutable {
                  ClientFragmentFrame frame;
                  frame.stream_id = stream_id;
                  frame.message = std::move(result);
                  return Seq(
                      outgoing_frames.Send(ClientFrame(std::move(frame))), [] {
                        // TODO(ladynana): remove this sleep after figure out
                        // how to synchronize writer_ with outside activity.
                        absl::SleepFor(absl::Seconds(5));
                        return absl::OkStatus();
                      });
                }));
  }

 private:
  // Max buffer is set to 4, so that for stream writes each time it will queue
  // at most 2 frames.
  MpscReceiver<ClientFrame> outgoing_frames_ = MpscReceiver<ClientFrame>(4);
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  ActivityPtr writer_;
  ActivityPtr reader_;
  std::unique_ptr<PromiseEndpoint> control_endpoint_;
  std::unique_ptr<PromiseEndpoint> data_endpoint_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_CLIENT_TRANSPORT_H