#include "src/core/ext/filters/backend_metrics/backend_metric_filter.h"

#include <grpc/support/port_platform.h>

#include "upb/upb.h"
#include "upb/upb.hpp"
#include "xds/data/orca/v3/orca_load_report.upb.h"

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

TraceFlag grpc_backend_metric_filter_trace(false, "backend_metric_filter");

absl::optional<std::string> BackendMetricFilter::MaybeSerializeBackendMetrics(
    BackendMetricProvider* provider) const {
  if (provider == nullptr) return absl::nullopt;
  BackendMetricData d = provider->GetBackendMetricData();
  upb::Arena arena;
  xds_data_orca_v3_OrcaLoadReport* response =
      xds_data_orca_v3_OrcaLoadReport_new(arena.ptr());

  if (d.cpu_utilization != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_cpu_utilization(response,
                                                        d.cpu_utilization);
  }
  if (d.mem_utilization != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_mem_utilization(response,
                                                        d.mem_utilization);
  }
  if (d.qps != -1) {
    xds_data_orca_v3_OrcaLoadReport_set_rps_fractional(response, d.qps);
  }
  for (const auto& p : d.request_cost) {
    xds_data_orca_v3_OrcaLoadReport_request_cost_set(
        response,
        upb_StringView_FromDataAndSize(p.first.data(), p.first.size()),
        p.second, arena.ptr());
  }
  for (const auto& p : d.utilization) {
    xds_data_orca_v3_OrcaLoadReport_utilization_set(
        response,
        upb_StringView_FromDataAndSize(p.first.data(), p.first.size()),
        p.second, arena.ptr());
  }
  size_t len;
  char* buf =
      xds_data_orca_v3_OrcaLoadReport_serialize(response, arena.ptr(), &len);
  return std::string(buf, len);
}

const grpc_channel_filter BackendMetricFilter::kFilter =
    MakePromiseBasedFilter<BackendMetricFilter, FilterEndpoint::kServer>(
        "backend_metric");

absl::StatusOr<BackendMetricFilter> BackendMetricFilter::Create(
    const ChannelArgs& channel_args, ChannelFilter::Args) {
  return BackendMetricFilter();
}

ArenaPromise<ServerMetadataHandle> BackendMetricFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  return ArenaPromise<ServerMetadataHandle>(Map(
      next_promise_factory(std::move(call_args)),
      [this](ServerMetadataHandle trailing_metadata) {
        auto* ctx = &GetContext<
            grpc_call_context_element>()[GRPC_CONTEXT_BACKEND_METRIC_PROVIDER];
        if (ctx == nullptr) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_filter_trace)) {
            gpr_log(GPR_INFO, "[%p] No BackendMetricProvider.", this);
          }
          return trailing_metadata;
        }
        absl::optional<std::string> serialized = MaybeSerializeBackendMetrics(
            reinterpret_cast<BackendMetricProvider*>(ctx->value));
        if (serialized.has_value() && !serialized->empty()) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_filter_trace)) {
            gpr_log(GPR_INFO,
                    "[%p] Backend metrics serialized. size: %" PRIuPTR, this,
                    serialized->size());
          }
          trailing_metadata->Set(
              EndpointLoadMetricsBinMetadata(),
              Slice::FromCopiedString(std::move(*serialized)));
        } else if (GRPC_TRACE_FLAG_ENABLED(grpc_backend_metric_filter_trace)) {
          gpr_log(GPR_INFO, "[%p] No backend metrics.", this);
        }
        return trailing_metadata;
      }));
}

}  // namespace grpc_core
