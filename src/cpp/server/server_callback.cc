//
// Copyright 2019 gRPC authors.
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

#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/support/server_callback.h>

#include "grpcpp/server.h"
#include "src/core/call/server_call.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/server/server.h"
#include "src/core/transport/session_endpoint.h"
#include "absl/log/log.h"

namespace grpc {
namespace internal {

void BindSessionToInnerServer(grpc_call* call, grpc::Server* inner_server) {
  grpc_core::Server* core_inner_server =
      grpc_core::Server::FromC(inner_server->c_server());

  // Create ServerSessionEndpoint
  grpc_endpoint* endpoint =
      grpc_core::SessionEndpoint::Create(call, /*is_client=*/false);

  // TODO(snohria): Pass in the correct channel args.
  grpc_core::ChannelArgs args =
      core_inner_server->channel_args()
          .SetObject(grpc_core::ResourceQuota::Default())
          .Set(GRPC_ARG_MINIMAL_STACK, 1);

  // Create old-style CHTTP2 Transport
  grpc_core::Transport* transport_ptr = grpc_create_chttp2_transport(
      args, grpc_core::OrphanablePtr<grpc_endpoint>(endpoint),
      /*is_client=*/false);

  // TODO(snohria): This should create a different call stack.
  auto status = core_inner_server->SetupTransport(transport_ptr,
                                                  /*pollset=*/nullptr, args);
  if (!status.ok()) {
    LOG(ERROR) << "SetupTransport failed: " << status;
  } else {
    // The transport is set up, but we need to start reading from it.
    grpc_chttp2_transport_start_reading(transport_ptr, nullptr, nullptr,
                                        nullptr, nullptr);
  }
}

void ServerCallbackCall::ScheduleOnDone(bool inline_ondone) {
  if (inline_ondone) {
    CallOnDone();
    return;
  }
  RunAsync([this]() { CallOnDone(); });
}

void ServerCallbackCall::CallOnCancel(ServerReactor* reactor) {
  if (reactor->InternalInlineable()) {
    reactor->OnCancel();
    return;
  }
  Ref();
  RunAsync([this, reactor]() {
    reactor->OnCancel();
    MaybeDone();
  });
}

}  // namespace internal
}  // namespace grpc
