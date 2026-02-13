//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_TRANSPORT_SESSION_ENDPOINT_H
#define GRPC_SRC_CORE_TRANSPORT_SESSION_ENDPOINT_H

#include <grpc/grpc.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/support/port_platform.h>

#include <atomic>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "absl/functional/any_invocable.h"

namespace grpc_core {

struct SessionEndpointTag {
  grpc_closure closure;
  absl::AnyInvocable<void(bool)> callback;
};

class SessionEndpoint {
 public:
  static grpc_endpoint* Create(grpc_call* call, bool is_client);

  struct State {
    explicit State(grpc_call* call) : call(call) {}
    std::atomic<grpc_call*> call;
    SessionEndpointTag read_tag;
    grpc_byte_buffer* read_buffer = nullptr;
    std::atomic<bool> read_in_progress{false};
    SessionEndpointTag write_tag;
    std::atomic<bool> write_in_progress{false};
  };
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TRANSPORT_SESSION_ENDPOINT_H
