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

#include <grpc/compression.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/server.h>
#include <grpcpp/support/server_callback.h>

#include "src/core/call/server_call.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/server/server.h"
#include "src/core/transport/session_endpoint.h"
#include "absl/log/log.h"

namespace grpc {
namespace internal {

void BindSessionToInnerServer(grpc_call* call, grpc::Server* inner_server) {
  grpc_core::ExecCtx exec_ctx;

  grpc_core::Server* core_inner_server =
      grpc_core::Server::FromC(inner_server->c_server());

  // Create ServerSessionEndpoint
  grpc_endpoint* endpoint =
      grpc_core::SessionEndpoint::Create(call, /*is_client=*/false);

  grpc_core::ChannelArgs args = core_inner_server->channel_args();
  if (args.GetObject<grpc_core::ResourceQuota>() == nullptr) {
    args = args.SetObject(grpc_core::ResourceQuota::Default());
  }

  // Disable all compression on the inner transport, to avoid double
  // compression. Compression should be done on the outer transport.
  args = args.Set(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
                  1 << GRPC_COMPRESS_NONE);

  // Disable keepalive to avoid sending keepalive pings on the inner transport.
  args = args.Set(GRPC_ARG_KEEPALIVE_TIME_MS, std::numeric_limits<int>::max());

  // Create old-style CHTTP2 Transport
  grpc_core::Transport* transport_ptr = grpc_create_chttp2_transport(
      args, grpc_core::OrphanablePtr<grpc_endpoint>(endpoint),
      /*is_client=*/false);

  auto status = core_inner_server->SetupTransport(
      transport_ptr, /*accepting_pollset=*/nullptr, args,
      GRPC_SERVER_VIRTUAL_CHANNEL);
  if (!status.ok()) {
    LOG(ERROR) << "SetupTransport failed: " << status;
    grpc_core::Call::FromC(call)->CancelWithError(status);
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
