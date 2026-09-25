// Copyright 2024 The gRPC Authors.
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

#ifndef GRPC_SRC_CORE_SERVER_SERVER_CALL_TRACER_FILTER_H
#define GRPC_SRC_CORE_SERVER_SERVER_CALL_TRACER_FILTER_H

#include <grpc/server_call_hook.h>
#include <grpc/support/port_platform.h>

#include <functional>
#include <string>
#include <utility>

#include "src/core/call/call_finalization.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/latent_see.h"
#include "absl/status/status.h"

namespace grpc_core {

// The server call hook installed for this process, or nullptr. Read on every
// server call, written once before serving, so a relaxed atomic load.
const grpc_server_call_hook_vtable* GetServerCallHook();

class ServerCallTracerFilter
    : public ImplementChannelFilter<ServerCallTracerFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::string_view TypeName() { return "server_call_tracer"; }

  static absl::StatusOr<std::unique_ptr<ServerCallTracerFilter>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/);

  class Call {
   public:
    // A non-null handle refuses the call, the same path HttpServerFilter uses
    // for a malformed request.
    ServerMetadataHandle OnClientInitialMetadata(
        ClientMetadata& client_initial_metadata) {
      GRPC_LATENT_SEE_SCOPE(
          "ServerCallTracerFilter::Call::OnClientInitialMetadata");
      auto* call_tracer = MaybeGetContext<ServerCallTracer>();
      if (call_tracer != nullptr) {
        call_tracer->RecordReceivedInitialMetadata(&client_initial_metadata);
      }
      const grpc_server_call_hook_vtable* hook = GetServerCallHook();
      if (hook == nullptr) return nullptr;
      started_ = true;
      if (hook->on_initial_metadata == nullptr) return nullptr;
      LookupContext ctx{&client_initial_metadata, std::string()};
      grpc_status_code status = hook->on_initial_metadata(
          hook->user_data, LookupHeader, &ctx, &call_data_);
      if (status == GRPC_STATUS_OK) return nullptr;
      auto hdl = GetContext<Arena>()->MakePooled<ServerMetadata>();
      hdl->Set(GrpcStatusMetadata(), status);
      hdl->Set(GrpcMessageMetadata(),
               Slice::FromStaticString("refused by server call hook"));
      return hdl;
    }

    void OnServerInitialMetadata(ServerMetadata& server_initial_metadata) {
      GRPC_LATENT_SEE_SCOPE(
          "ServerCallTracerFilter::Call::OnServerInitialMetadata");
      auto* call_tracer = MaybeGetContext<ServerCallTracer>();
      if (call_tracer == nullptr) return;
      call_tracer->RecordSendInitialMetadata(&server_initial_metadata);
    }

    void OnFinalize(const grpc_call_final_info* final_info) {
      GRPC_LATENT_SEE_SCOPE("ServerCallTracerFilter::Call::OnFinalize");
      auto* call_tracer = MaybeGetContext<ServerCallTracer>();
      if (call_tracer == nullptr) return;
      call_tracer->RecordEnd(final_info);
    }

    void OnServerTrailingMetadata(ServerMetadata& server_trailing_metadata) {
      GRPC_LATENT_SEE_SCOPE(
          "ServerCallTracerFilter::Call::OnServerTrailingMetadata");
      const grpc_server_call_hook_vtable* hook = GetServerCallHook();
      // Size-guarded: a hook built without `on_call_end` has a struct that stops
      // before it, so the field would be out of bounds.
      if (hook != nullptr && started_ &&
          hook->struct_size >=
              offsetof(grpc_server_call_hook_vtable, on_call_end) +
                  sizeof(grpc_server_call_hook_on_call_end_fn) &&
          hook->on_call_end != nullptr) {
        hook->on_call_end(hook->user_data, call_data_,
                          server_trailing_metadata.get(GrpcStatusMetadata())
                              .value_or(GRPC_STATUS_UNKNOWN));
        started_ = false;
      }
      auto* call_tracer = MaybeGetContext<ServerCallTracer>();
      if (call_tracer == nullptr) return;
      call_tracer->RecordSendTrailingMetadata(&server_trailing_metadata);
    }

    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;

    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }

   private:
    // Opaque token the hook uses to read headers without linking gRPC's metadata.
    struct LookupContext {
      ClientMetadata* metadata;
      std::string backing;
    };

    static const char* LookupHeader(void* lookup_context, const char* name,
                                    size_t* value_length) {
      auto* ctx = static_cast<LookupContext*>(lookup_context);
      std::optional<absl::string_view> value =
          ctx->metadata->GetStringValue(name, &ctx->backing);
      if (!value.has_value()) return nullptr;
      *value_length = value->size();
      return value->data();
    }

    void* call_data_ = nullptr;
    bool started_ = false;
  };
};

void RegisterServerCallTracerFilter(CoreConfiguration::Builder* builder);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_SERVER_SERVER_CALL_TRACER_FILTER_H
