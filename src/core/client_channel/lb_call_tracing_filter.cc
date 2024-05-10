// Copyright 2024 gRPC authors.
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

#include "src/core/client_channel/lb_call_tracing_filter.h"

#include "lb_call_tracing_filter.h"

namespace grpc_core {

const NoInterceptor LbCallTracingFilter::Call::OnClientToServerMessage;
const NoInterceptor LbCallTracingFilter::Call::OnServerToClientMessage;

void LbCallTracingFilter::Call::OnClientInitialMetadata(
    ClientMetadata& metadata) {
  auto* tracer = GetCallAttemptTracerFromContext();
  if (tracer == nullptr) return;
  tracer->RecordSendInitialMetadata(&metadata);
}

void LbCallTracingFilter::Call::OnServerInitialMetadata(
    ServerMetadata& metadata) {
  auto* tracer = GetCallAttemptTracerFromContext();
  if (tracer == nullptr) return;
  tracer->RecordReceivedInitialMetadata(&metadata);
  // Save peer string for later use.
  Slice* peer_string = metadata.get_pointer(PeerString());
  if (peer_string != nullptr) peer_string_ = peer_string->Ref();
}

static const NoInterceptor OnClientToServerMessage;
static const NoInterceptor OnServerToClientMessage;

void LbCallTracingFilter::Call::OnClientToServerHalfClose() {
  auto* tracer = GetCallAttemptTracerFromContext();
  if (tracer == nullptr) return;
  // TODO(roth): Change CallTracer API to not pass metadata
  // batch to this method, since the batch is always empty.
  grpc_metadata_batch metadata;
  tracer->RecordSendTrailingMetadata(&metadata);
}

void LbCallTracingFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& metadata) {
  auto* tracer = GetCallAttemptTracerFromContext();
  auto* call_tracker =
      GetContext<LoadBalancingPolicy::SubchannelCallTrackerInterface*>();
  absl::Status status;
  if (tracer != nullptr ||
      (call_tracker != nullptr && *call_tracker != nullptr)) {
    grpc_status_code code =
        metadata.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN);
    if (code != GRPC_STATUS_OK) {
      absl::string_view message;
      if (const auto* grpc_message =
              metadata.get_pointer(GrpcMessageMetadata())) {
        message = grpc_message->as_string_view();
      }
      status = absl::Status(static_cast<absl::StatusCode>(code), message);
    }
  }
  if (tracer != nullptr) {
    tracer->RecordReceivedTrailingMetadata(
        status, &metadata,
        &GetContext<CallContext>()->call_stats()->transport_stream_stats);
  }
  if (call_tracker != nullptr && *call_tracker != nullptr) {
    LbMetadata lb_metadata(&metadata);
    BackendMetricAccessor backend_metric_accessor(&metadata);
    LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
        peer_string_.as_string_view(), status, &lb_metadata,
        &backend_metric_accessor};
    (*call_tracker)->Finish(args);
    delete *call_tracker;
  }
}

void LbCallTracingFilter::Call::OnFinalize(const grpc_call_final_info*) {
  auto* tracer = GetCallAttemptTracerFromContext();
  if (tracer == nullptr) return;
  gpr_timespec latency =
      gpr_cycle_counter_sub(gpr_get_cycle_counter(), lb_call_start_time_);
  tracer->RecordEnd(latency);
}

}  // namespace grpc_core
