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

#include "src/core/client_channel/virtual_client_call_tracer_filter.h"

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/call/metadata.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/telemetry/call_tracer.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {

const grpc_channel_filter VirtualClientCallTracerFilter::kFilter =
    MakePromiseBasedFilter<
        VirtualClientCallTracerFilter, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesOutboundMessages |
            kFilterExaminesInboundMessages>();

absl::StatusOr<std::unique_ptr<VirtualClientCallTracerFilter>>
VirtualClientCallTracerFilter::Create(const ChannelArgs& /*args*/,
                                      ChannelFilter::Args /*filter_args*/) {
  return std::make_unique<VirtualClientCallTracerFilter>();
}

void VirtualClientCallTracerFilter::Call::OnClientInitialMetadata(
    ClientMetadata& client_initial_metadata) {
  auto* arena = GetContext<Arena>();
  auto* client_call_tracer = arena->GetContext<ClientCallTracer>();
  if (client_call_tracer != nullptr) {
    auto* attempt_tracer = WrapCallAttemptTracer(
        client_call_tracer->StartNewAttempt(/*is_transparent_retry=*/false),
        arena);
    if (attempt_tracer != nullptr) {
      arena->SetContext<CallTracer>(attempt_tracer);
      attempt_tracer->RecordSendInitialMetadata(&client_initial_metadata);
    }
  }
}

void VirtualClientCallTracerFilter::Call::OnServerInitialMetadata(
    ServerMetadata& server_initial_metadata) {
  auto* call_tracer = MaybeGetContext<CallTracer>();
  if (call_tracer == nullptr) return;
  call_tracer->RecordReceivedInitialMetadata(&server_initial_metadata);
}

void VirtualClientCallTracerFilter::Call::OnClientToServerMessage(
    const grpc_core::Message& msg) {
  outgoing_bytes_.fetch_add(msg.payload()->Length(), std::memory_order_relaxed);
  auto* call_tracer = MaybeGetContext<CallAttemptTracer>();
  if (call_tracer != nullptr) {
    call_tracer->RecordSendMessage(msg);
  }
}

void VirtualClientCallTracerFilter::Call::OnServerToClientMessage(
    const grpc_core::Message& msg) {
  incoming_bytes_.fetch_add(msg.payload()->Length(), std::memory_order_relaxed);
  auto* call_tracer = MaybeGetContext<CallAttemptTracer>();
  if (call_tracer != nullptr) {
    call_tracer->RecordReceivedMessage(msg);
  }
}

void VirtualClientCallTracerFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& server_trailing_metadata) {
  auto* call_tracer = MaybeGetContext<CallAttemptTracer>();
  if (call_tracer == nullptr) return;
  absl::Status status = ServerMetadataToStatus(server_trailing_metadata);

  // Synthesize transport stats
  grpc_transport_stream_stats stats{};
  stats.outgoing.data_bytes = outgoing_bytes_.load(std::memory_order_relaxed);
  stats.incoming.data_bytes = incoming_bytes_.load(std::memory_order_relaxed);

  call_tracer->RecordReceivedTrailingMetadata(status, &server_trailing_metadata,
                                              &stats);
}

void VirtualClientCallTracerFilter::Call::OnFinalize(
    const grpc_call_final_info* final_info) {
  auto* call_tracer = MaybeGetContext<CallAttemptTracer>();
  if (call_tracer != nullptr) {
    call_tracer->RecordEnd();
  }
}

void RegisterVirtualClientCallTracerFilter(
    CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterV2Filter<VirtualClientCallTracerFilter>(
      GRPC_CLIENT_VIRTUAL_CHANNEL);
}

}  // namespace grpc_core
