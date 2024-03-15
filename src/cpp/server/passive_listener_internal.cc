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

#include <grpc/grpc.h>
#include <grpc/passive_listener_injection.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc {
namespace experimental {

void ServerBuilderPassiveListener::AcceptConnectedEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint) {
  GPR_DEBUG_ASSERT(server_ != nullptr);
  grpc_core::ExecCtx exec_ctx;
  if (creds_->c_creds() == nullptr) {
    auto creds = grpc_insecure_server_credentials_create();
    grpc_server_add_passive_listener_endpoint(server_->c_server(),
                                              std::move(endpoint), creds);
    grpc_server_credentials_release(creds);
    return;
  }
  grpc_server_add_passive_listener_endpoint(
      server_->c_server(), std::move(endpoint), creds_->c_creds());
}

absl::Status ServerBuilderPassiveListener::AcceptConnectedFd(int fd) {
  GPR_DEBUG_ASSERT(server_ != nullptr);
  grpc_core::ExecCtx exec_ctx;
  if (creds_->c_creds() == nullptr) {
    auto creds = grpc_insecure_server_credentials_create();
    auto result = grpc_server_add_passive_listener_connected_fd(
        server_->c_server(), fd, creds, server_args_);
    grpc_server_credentials_release(creds);
    return result;
  }
  return grpc_server_add_passive_listener_connected_fd(
      server_->c_server(), fd, creds_->c_creds(), server_args_);
}

void ServerBuilderPassiveListener::Initialize(
    grpc::Server* server, grpc::ChannelArguments& arguments) {
  GPR_DEBUG_ASSERT(server_ == nullptr);
  server_ = server;
  grpc_channel_args tmp_args;
  arguments.SetChannelArgs(&tmp_args);
  server_args_ = grpc_channel_args_copy(&tmp_args);
}

}  // namespace experimental
}  // namespace grpc
