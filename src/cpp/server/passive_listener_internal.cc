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

#include "src/cpp/server/passive_listener_internal.h"

#include <grpc/event_engine/passive_listener_injection.h>
#include <grpc/grpc.h>

#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc {
namespace experimental {

void ServerBuilderPassiveListener::AcceptConnectedEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint) {
  grpc_core::ExecCtx exec_ctx;
  grpc_server_add_passive_listener_endpoint(
      server_->c_server(), std::move(endpoint), creds_->c_creds());
}

absl::Status ServerBuilderPassiveListener::AcceptConnectedFd(
    GRPC_UNUSED int fd) {
  GPR_ASSERT(server_ != nullptr);
  grpc_core::ExecCtx exec_ctx;
  return grpc_server_add_passive_listener_connected_fd(
      server_->c_server(), fd, creds_->c_creds(), &server_args_);
}

void ServerBuilderPassiveListener::Initialize(
    grpc::Server* server, grpc::ChannelArguments& arguments) {
  GPR_DEBUG_ASSERT(server_ == nullptr);
  server_ = server;
  arguments.SetChannelArgs(&server_args_);
}

}  // namespace experimental
}  // namespace grpc
