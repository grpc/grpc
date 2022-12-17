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

#ifndef CHAOTIC_GOOD_TRANSPORT_H
#define CHAOTIC_GOOD_TRANSPORT_H

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace chaotic_good {

class ClientTransport {
 public:
  explicit ClientTransport(PromiseEndpoint endpoint);
  ArenaPromise<ServerMetadataHandle> CreateStream(CallArgs call_args);

 private:
  MultiProducerSingleConsumerPipe<ClientFrame> outgoing_frames_;
  Mutex mu_;
  uint32_t next_stream_id_ ABSL_GUARDED_BY(mu_) = 1;
  ActivityPtr writer_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
