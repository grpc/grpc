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

#ifndef GRPC_SRC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_PAIR_H
#define GRPC_SRC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_PAIR_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"

namespace grpc_event_engine {
namespace experimental {

struct EndpointPair {
  EndpointPair() = default;
  EndpointPair(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          client,
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          server) {
    client_ep = std::move(client);
    server_ep = std::move(server);
  }
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      client_ep;
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      server_ep;
};

// Creates a pair of connected EventEngine::Endpoint endpoints.
EndpointPair CreateEndpointPair(grpc_core::ChannelArgs& args,
                                ThreadPool* thread_pool);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_PAIR_H
