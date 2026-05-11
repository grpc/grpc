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
#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/server.h>
#include <grpcpp/support/server_callback.h>

#include "src/core/call/server_call.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/server/server.h"
#include "src/core/transport/session_endpoint.h"
#include "absl/log/log.h"

namespace grpc {
namespace internal {

void BindSessionToInnerServer(grpc_call* call, grpc::Server* inner_server,
                              grpc_core::Transport** out_transport,
                              grpc_endpoint** out_endpoint) {
  grpc_core::ExecCtx exec_ctx;

  grpc_core::Server* core_inner_server =
      grpc_core::Server::FromC(inner_server->c_server());

  // Create ServerSessionEndpoint
  grpc_endpoint* endpoint =
      grpc_core::SessionEndpoint::Create(call, /*is_client=*/false);
  if (out_endpoint != nullptr) {
    *out_endpoint = endpoint;
  }

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

  if (out_transport != nullptr) {
    *out_transport = transport_ptr;
  }

  auto status = core_inner_server->SetupTransport(
      transport_ptr, /*accepting_pollset=*/nullptr, args,
      GRPC_SERVER_VIRTUAL_CHANNEL);
  if (!status.ok()) {
    LOG(ERROR) << "SetupTransport failed: " << status;
    if (out_transport != nullptr) {
      *out_transport = nullptr;
    }
    if (out_endpoint != nullptr) {
      *out_endpoint = nullptr;
    }
    grpc_core::Call::FromC(call)->CancelWithError(status);
  } else {
    // The transport is set up, but we need to start reading from it.
    grpc_chttp2_transport_start_reading(transport_ptr, nullptr, nullptr,
                                        nullptr, nullptr);
  }
}

namespace {
class ShutdownWatcher : public grpc_core::Transport::StateWatcher {
 public:
  explicit ShutdownWatcher(absl::AnyInvocable<void(absl::Status)> on_shutdown)
      : on_shutdown_(std::move(on_shutdown)) {}
  void OnDisconnect(absl::Status status,
                    DisconnectInfo disconnect_info) override {
    if (on_shutdown_) {
      if (disconnect_info.reason ==
          grpc_core::Transport::StateWatcher::kGoaway) {
        on_shutdown_(absl::OkStatus());
      } else {
        on_shutdown_(std::move(status));
      }
    }
  }
  void OnPeerMaxConcurrentStreamsUpdate(
      uint32_t /*max_concurrent_streams*/,
      std::unique_ptr<MaxConcurrentStreamsUpdateDoneHandle> /*on_done*/)
      override {}
  grpc_pollset_set* interested_parties() const override { return nullptr; }

 private:
  absl::AnyInvocable<void(absl::Status)> on_shutdown_;
};
}  // namespace

void InitiateSessionGracefulShutdown(
    grpc_core::Transport* transport, grpc_endpoint* endpoint,
    absl::AnyInvocable<void(absl::Status)> on_shutdown) {
  grpc_core::ExecCtx exec_ctx;
  if (endpoint != nullptr) {
    auto* ee_endpoint =
        grpc_event_engine::experimental::grpc_get_wrapped_event_engine_endpoint(
            endpoint);
    auto* session_endpoint =
        static_cast<grpc_core::SessionEndpoint*>(ee_endpoint);
    session_endpoint->SetGracefulShutdown();
  }
  if (transport != nullptr) {
    transport->StartWatch(
        grpc_core::MakeRefCounted<ShutdownWatcher>(std::move(on_shutdown)));

    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->goaway_error = grpc_error_set_int(
        GRPC_ERROR_CREATE("Graceful shutdown"),
        grpc_core::StatusIntProperty::kHttp2Error,
        static_cast<int>(grpc_core::http2::Http2ErrorCode::kNoError));
    transport->PerformOp(op);
  } else if (on_shutdown) {
    on_shutdown(absl::UnavailableError("No transport available"));
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
