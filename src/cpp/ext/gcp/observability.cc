//
// Copyright 2022 gRPC authors.
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

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "google/devtools/cloudtrace/v2/tracing.grpc.pb.h"
#include "google/monitoring/v3/metric_service.grpc.pb.h"
#include "opencensus/exporters/stats/stackdriver/stackdriver_exporter.h"
#include "opencensus/exporters/trace/stackdriver/stackdriver_exporter.h"
#include "opencensus/stats/stats.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/trace_config.h"

#include <grpcpp/ext/gcp_observability.h>
#include <grpcpp/opencensus.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/open_census_call_tracer.h"
#include "src/cpp/ext/gcp/observability_config.h"

namespace grpc {
namespace experimental {

namespace {
// TODO(yashykt): These constants are currently derived from the example at
// https://cloud.google.com/traffic-director/docs/observability-proxyless#c++.
// We might want these to be configurable.
constexpr uint32_t kMaxAttributes = 128;
constexpr uint32_t kMaxAnnotations = 128;
constexpr uint32_t kMaxMessageEvents = 128;
constexpr uint32_t kMaxLinks = 128;

constexpr char kGoogleStackdriverTraceAddress[] = "cloudtrace.googleapis.com";
constexpr char kGoogleStackdriverStatsAddress[] = "monitoring.googleapis.com";

void RegisterOpenCensusViewsForGcpObservability() {
  // Register client default views for GCP observability
  ClientStartedRpcs().RegisterForExport();
  ClientCompletedRpcs().RegisterForExport();
  // Register server default views for GCP observability
  ServerStartedRpcs().RegisterForExport();
  ServerCompletedRpcs().RegisterForExport();
}

}  // namespace

absl::Status GcpObservabilityInit() {
  auto config = grpc::internal::GcpObservabilityConfig::ReadFromEnv();
  if (!config.ok()) {
    return config.status();
  }
  if (!config->cloud_trace.has_value() &&
      !config->cloud_monitoring.has_value()) {
    return absl::OkStatus();
  }
  grpc::RegisterOpenCensusPlugin();
  RegisterOpenCensusViewsForGcpObservability();
  ChannelArguments args;
  args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
  if (config->cloud_trace.has_value()) {
    opencensus::trace::TraceConfig::SetCurrentTraceParams(
        {kMaxAttributes, kMaxAnnotations, kMaxMessageEvents, kMaxLinks,
         opencensus::trace::ProbabilitySampler(
             config->cloud_trace->sampling_rate)});
    opencensus::exporters::trace::StackdriverOptions trace_opts;
    trace_opts.project_id = config->project_id;
    trace_opts.trace_service_stub =
        ::google::devtools::cloudtrace::v2::TraceService::NewStub(
            CreateCustomChannel(kGoogleStackdriverTraceAddress,
                                GoogleDefaultCredentials(), args));
    opencensus::exporters::trace::StackdriverExporter::Register(
        std::move(trace_opts));
  } else {
    // Disable OpenCensus tracing
    EnableOpenCensusTracing(false);
  }
  if (config->cloud_monitoring.has_value()) {
    opencensus::exporters::stats::StackdriverOptions stats_opts;
    stats_opts.project_id = config->project_id;
    stats_opts.metric_service_stub =
        google::monitoring::v3::MetricService::NewStub(CreateCustomChannel(
            kGoogleStackdriverStatsAddress, GoogleDefaultCredentials(), args));
    opencensus::exporters::stats::StackdriverExporter::Register(
        std::move(stats_opts));
  } else {
    // Disable OpenCensus stats
    EnableOpenCensusStats(false);
  }
  return absl::OkStatus();
}

}  // namespace experimental
}  // namespace grpc
