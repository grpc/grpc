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

#include "src/core/ext/filters/backend_metrics/backend_metric_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <stddef.h>

#include <functional>
#include <map>
#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/load_balancing/backend_metric_data.h"
#include "src/core/util/latent_see.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"
#include "xds/data/orca/v3/orca_load_report.upb.h"

namespace grpc_core {

namespace {
std::optional<std::string> MaybeSerializeBackendMetrics(
    BackendMetricProvider* provider) {
  if (provider == nullptr) return std::nullopt;
  BackendMetricData data = provider->GetBackendMetricData();
  upb::Arena arena;
  xds_data_orca_v3_OrcaLoadReport* response =
      xds_data_orca_v3_OrcaLoadReport_new(arena.ptr());
  bool has_data = false;
  if (data.cpu_utilization != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_cpu_utilization(response,
                                                        data.cpu_utilization);
    has_data = true;
  }
  if (data.mem_utilization != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_mem_utilization(response,
                                                        data.mem_utilization);
    has_data = true;
  }
  if (data.application_utilization != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_application_utilization(
        response, data.application_utilization);
    has_data = true;
  }
  if (data.qps != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_rps_fractional(response, data.qps);
    has_data = true;
  }
  if (data.eps != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_eps(response, data.eps);
    has_data = true;
  }
  for (const auto& p : data.request_cost) {
    xds_data_orca_v3_OrcaLoadReport_request_cost_set(
        response,
        upb_StringView_FromDataAndSize(p.first.data(), p.first.size()),
        p.second, arena.ptr());
    has_data = true;
  }
  for (const auto& p : data.utilization) {
    xds_data_orca_v3_OrcaLoadReport_utilization_set(
        response,
        upb_StringView_FromDataAndSize(p.first.data(), p.first.size()),
        p.second, arena.ptr());
    has_data = true;
  }
  for (const auto& p : data.named_metrics) {
    xds_data_orca_v3_OrcaLoadReport_named_metrics_set(
        response,
        upb_StringView_FromDataAndSize(p.first.data(), p.first.size()),
        p.second, arena.ptr());
    has_data = true;
  }
  if (!has_data) {
    return std::nullopt;
  }
  size_t len;
  char* buf =
      xds_data_orca_v3_OrcaLoadReport_serialize(response, arena.ptr(), &len);
  return std::string(buf, len);
}
}  // namespace

const grpc_channel_filter BackendMetricFilter::kFilter =
    MakePromiseBasedFilter<BackendMetricFilter, FilterEndpoint::kServer>();

absl::StatusOr<std::unique_ptr<BackendMetricFilter>>
BackendMetricFilter::Create(const ChannelArgs&, ChannelFilter::Args) {
  return std::make_unique<BackendMetricFilter>();
}

void BackendMetricFilter::Call::OnServerTrailingMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "BackendMetricFilter::Call::OnServerTrailingMetadata");
  if (md.get(GrpcCallWasCancelled()).value_or(false)) return;
  auto* ctx = MaybeGetContext<BackendMetricProvider>();
  if (ctx == nullptr) {
    GRPC_TRACE_LOG(backend_metric_filter, INFO)
        << "[" << this << "] No BackendMetricProvider.";
    return;
  }
  std::optional<std::string> serialized = MaybeSerializeBackendMetrics(ctx);
  if (serialized.has_value() && !serialized->empty()) {
    GRPC_TRACE_LOG(backend_metric_filter, INFO)
        << "[" << this
        << "] Backend metrics serialized. size: " << serialized->size();
    md.Set(EndpointLoadMetricsBinMetadata(),
           Slice::FromCopiedString(std::move(*serialized)));
  } else {
    GRPC_TRACE_LOG(backend_metric_filter, INFO)
        << "[" << this << "] No backend metrics.";
  }
}

void RegisterBackendMetricFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()
      ->RegisterFilter<BackendMetricFilter>(GRPC_SERVER_CHANNEL)
      .IfHasChannelArg(GRPC_ARG_SERVER_CALL_METRIC_RECORDING);
}

}  // namespace grpc_core
