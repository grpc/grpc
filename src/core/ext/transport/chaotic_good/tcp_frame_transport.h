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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_TCP_FRAME_TRANSPORT_H

#include <vector>

#include "pending_connection.h"
#include "src/core/ext/transport/chaotic_good/frame_transport.h"

namespace grpc_core {
namespace chaotic_good {

class TcpFrameTransport final : public FrameTransport {
 public:
  TcpFrameTransport(PromiseEndpoint control_endpoint,
                    std::vector<PendingConnection> pending_data_endpoints);

 private:
  Poll<StatusFlag> PollWrite(Frame& frame) override;
  Poll<ValueOrFailure<Frame>> PollRead() override;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
