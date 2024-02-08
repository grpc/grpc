//
// Copyright 2023 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <functional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

namespace {

class ServerCallTracerFilter
    : public ImplementChannelFilter<ServerCallTracerFilter> {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ServerCallTracerFilter> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/);

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& client_initial_metadata) {
      auto* call_tracer = CallTracer();
      if (call_tracer == nullptr) return;
      call_tracer->RecordReceivedInitialMetadata(&client_initial_metadata);
    }

    void OnServerInitialMetadata(ServerMetadata& server_initial_metadata) {
      auto* call_tracer = CallTracer();
      if (call_tracer == nullptr) return;
      call_tracer->RecordSendInitialMetadata(&server_initial_metadata);
    }

    void OnFinalize(const grpc_call_final_info* final_info) {
      auto* call_tracer = CallTracer();
      if (call_tracer == nullptr) return;
      call_tracer->RecordEnd(final_info);
    }

    void OnServerTrailingMetadata(ServerMetadata& server_trailing_metadata) {
      auto* call_tracer = CallTracer();
      if (call_tracer == nullptr) return;
      call_tracer->RecordSendTrailingMetadata(&server_trailing_metadata);
    }

    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;

   private:
    static ServerCallTracer* CallTracer() {
      auto* call_context = GetContext<grpc_call_context_element>();
      return static_cast<ServerCallTracer*>(
          call_context[GRPC_CONTEXT_CALL_TRACER].value);
    }
  };
};

const NoInterceptor ServerCallTracerFilter::Call::OnClientToServerMessage;
const NoInterceptor ServerCallTracerFilter::Call::OnServerToClientMessage;

const grpc_channel_filter ServerCallTracerFilter::kFilter =
    MakePromiseBasedFilter<ServerCallTracerFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata>(
        "server_call_tracer");

absl::StatusOr<ServerCallTracerFilter> ServerCallTracerFilter::Create(
    const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
  return ServerCallTracerFilter();
}

}  // namespace

void RegisterServerCallTracerFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterFilter<ServerCallTracerFilter>(
      GRPC_SERVER_CHANNEL);
}

}  // namespace grpc_core
