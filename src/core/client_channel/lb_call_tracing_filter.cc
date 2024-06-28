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

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include "src/core/client_channel/lb_metadata.h"
#include "src/core/client_channel/load_balanced_call_destination.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/load_balancing/backend_metric_data.h"
#include "src/core/load_balancing/backend_metric_parser.h"
#include "src/core/load_balancing/lb_policy.h"

namespace grpc_core {

const grpc_channel_filter LbCallTracingFilter::kFilter =
    MakePromiseBasedFilter<LbCallTracingFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesOutboundMessages>();

const NoInterceptor LbCallTracingFilter::Call::OnServerToClientMessage;

void LbCallTracingFilter::Call::OnClientInitialMetadata(
    ClientMetadata& metadata) {
  auto* tracer = DownCast<ClientCallTracer::CallAttemptTracer*>(
      MaybeGetContext<CallTracerInterface>());
  if (tracer == nullptr) return;
  tracer->RecordSendInitialMetadata(&metadata);
}

void LbCallTracingFilter::Call::OnClientToServerHalfClose() {
  auto* tracer = DownCast<ClientCallTracer::CallAttemptTracer*>(
      MaybeGetContext<CallTracerInterface>());
  if (tracer == nullptr) return;
  // TODO(roth): Change CallTracer API to not pass metadata
  // batch to this method, since the batch is always empty.
  grpc_metadata_batch metadata;
  tracer->RecordSendTrailingMetadata(&metadata);
}

void LbCallTracingFilter::Call::OnServerInitialMetadata(
    ServerMetadata& metadata) {
  auto* tracer = DownCast<ClientCallTracer::CallAttemptTracer*>(
      MaybeGetContext<CallTracerInterface>());
  if (tracer == nullptr) return;
  tracer->RecordReceivedInitialMetadata(&metadata);
  // Save peer string for later use.
  Slice* peer_string = metadata.get_pointer(PeerString());
  if (peer_string != nullptr) peer_string_ = peer_string->Ref();
}

namespace {

// Interface for accessing backend metric data in the LB call tracker.
class BackendMetricAccessor
    : public LoadBalancingPolicy::BackendMetricAccessor {
 public:
  explicit BackendMetricAccessor(
      grpc_metadata_batch* server_trailing_metadata)
      : server_trailing_metadata_(server_trailing_metadata) {}

  ~BackendMetricAccessor() override {
    if (backend_metric_data_ != nullptr) {
      backend_metric_data_->~BackendMetricData();
    }
  }

  const BackendMetricData* GetBackendMetricData() override {
    if (backend_metric_data_ == nullptr) {
      const auto* md = server_trailing_metadata_->get_pointer(
          EndpointLoadMetricsBinMetadata());
      if (md != nullptr) {
        BackendMetricAllocator allocator;
        backend_metric_data_ =
            ParseBackendMetricData(md->as_string_view(), &allocator);
      }
    }
    return backend_metric_data_;
  }

 private:
  class BackendMetricAllocator : public BackendMetricAllocatorInterface {
   public:
    BackendMetricData* AllocateBackendMetricData() override {
      return GetContext<Arena>()->New<BackendMetricData>();
    }

    char* AllocateString(size_t size) override {
      return static_cast<char*>(GetContext<Arena>()->Alloc(size));
    }
  };

  grpc_metadata_batch* server_trailing_metadata_;
  const BackendMetricData* backend_metric_data_ = nullptr;
};

}  // namespace

void LbCallTracingFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& metadata) {
  auto* tracer = DownCast<ClientCallTracer::CallAttemptTracer*>(
      MaybeGetContext<CallTracerInterface>());
  auto* call_tracker =
      MaybeGetContext<LoadBalancingPolicy::SubchannelCallTrackerInterface>();
  absl::Status status;
  if (tracer != nullptr || call_tracker != nullptr) {
    status = absl::Status(
        static_cast<absl::StatusCode>(StatusCodeFromMetadata(metadata)),
        StatusMessageFromMetadata(metadata));
  }
  if (tracer != nullptr) {
    if (metadata.get(GrpcCallWasCancelled()).value_or(false)) {
      tracer->RecordCancel(status);
    }
    tracer->RecordReceivedTrailingMetadata(status, &metadata, nullptr);
  }
  if (call_tracker != nullptr) {
    LbMetadata lb_metadata(&metadata);
    BackendMetricAccessor backend_metric_accessor(&metadata);
    LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
        peer_string_.as_string_view(), status, &lb_metadata,
        &backend_metric_accessor};
    call_tracker->Finish(args);
    delete call_tracker;
  }
}

void LbCallTracingFilter::Call::OnFinalize(const grpc_call_final_info*) {
  auto* tracer = DownCast<ClientCallTracer::CallAttemptTracer*>(
      MaybeGetContext<CallTracerInterface>());
  if (tracer == nullptr) return;
  auto* lb_call_start_time = GetContext<LoadBalancedCallStartTime>();
  gpr_timespec latency = gpr_cycle_counter_sub(
      gpr_get_cycle_counter(), lb_call_start_time->lb_call_start_time);
  tracer->RecordEnd(latency);
}

void RegisterLbCallTracingFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()
      ->RegisterFilter<LbCallTracingFilter>(GRPC_CLIENT_SUBCHANNEL)
      // Needs to be at the top of the stack, so that we properly
      // measure call attempt latency in the CallTracer.
      .FloatToTop();
}

}  // namespace grpc_core
