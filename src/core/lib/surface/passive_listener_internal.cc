// Copyright 2024 The gRPC Authors
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

#include "src/core/lib/surface/passive_listener_internal.h"

#include <grpc/grpc.h>
#include <grpc/passive_listener_injection.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {
namespace experimental {

absl::Status PassiveListenerImpl::AcceptConnectedEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint) {
  GPR_ASSERT(server_ != nullptr);
  return grpc_server_accept_connected_endpoint(server_.get(), listener_.get(),
                                               std::move(endpoint));
}

absl::Status PassiveListenerImpl::AcceptConnectedFd(int fd) {
  GPR_ASSERT(server_ != nullptr);
  grpc_core::ExecCtx exec_ctx;
  auto& args = server_->channel_args();
  auto engine =
      args.GetObjectRef<grpc_event_engine::experimental::EventEngine>();
  auto* supports_fd = grpc_event_engine::experimental::QueryExtension<
      grpc_event_engine::experimental::EventEngineSupportsFdExtension>(
      engine.get());
  if (supports_fd == nullptr) {
    return absl::UnimplementedError(
        "The server's EventEngine does not support adding endpoints from "
        "connected file descriptors.");
  }
  auto endpoint = supports_fd->CreateEndpointFromFd(
      fd, grpc_event_engine::experimental::ChannelArgsEndpointConfig(args));
  return AcceptConnectedEndpoint(std::move(endpoint));
}

}  // namespace experimental
}  // namespace grpc_core
