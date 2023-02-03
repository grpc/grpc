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
#include "src/cpp/ext/filters/logging/logging_filter.h"
#include "src/cpp/ext/gcp/observability_config.h"
#include "src/cpp/ext/gcp/observability_logging_sink.h"

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
  ClientRoundtripLatency().RegisterForExport();
  ClientSentCompressedMessageBytesPerRpc().RegisterForExport();
  ClientReceivedCompressedMessageBytesPerRpc().RegisterForExport();
  // Register server default views for GCP observability
  ServerStartedRpcs().RegisterForExport();
  ServerCompletedRpcs().RegisterForExport();
  ServerSentCompressedMessageBytesPerRpc().RegisterForExport();
  ServerReceivedCompressedMessageBytesPerRpc().RegisterForExport();
  ServerServerLatency().RegisterForExport();
}

}  // namespace

absl::Status GcpObservabilityInit() {
  auto config = grpc::internal::GcpObservabilityConfig::ReadFromEnv();
  if (!config.ok()) {
    return config.status();
  }
  if (!config->cloud_trace.has_value() &&
      !config->cloud_monitoring.has_value() &&
      !config->cloud_logging.has_value()) {
    return absl::OkStatus();
  }
  grpc::internal::OpenCensusRegistry::Get().RegisterConstantLabels(
      config->labels);
  grpc::RegisterOpenCensusPlugin();
  RegisterOpenCensusViewsForGcpObservability();
  if (config->cloud_trace.has_value()) {
    grpc::internal::OpenCensusRegistry::Get().RegisterFunctions(
        [cloud_trace = config->cloud_trace.value(),
         project_id = config->project_id]() mutable {
          opencensus::trace::TraceConfig::SetCurrentTraceParams(
              {kMaxAttributes, kMaxAnnotations, kMaxMessageEvents, kMaxLinks,
               opencensus::trace::ProbabilitySampler(
                   cloud_trace.sampling_rate)});
          opencensus::exporters::trace::StackdriverOptions trace_opts;
          trace_opts.project_id = std::move(project_id);
          ChannelArguments args;
          args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
          trace_opts.trace_service_stub =
              ::google::devtools::cloudtrace::v2::TraceService::NewStub(
                  CreateCustomChannel(kGoogleStackdriverTraceAddress,
                                      GoogleDefaultCredentials(), args));
          opencensus::exporters::trace::StackdriverExporter::Register(
              std::move(trace_opts));
        });
  } else {
    // Disable OpenCensus tracing
    grpc::internal::EnableOpenCensusTracing(false);
  }
  if (config->cloud_monitoring.has_value()) {
    grpc::internal::OpenCensusRegistry::Get().RegisterFunctions(
        [project_id = config->project_id]() mutable {
          opencensus::exporters::stats::StackdriverOptions stats_opts;
          stats_opts.project_id = std::move(project_id);
          ChannelArguments args;
          args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
          stats_opts.metric_service_stub =
              google::monitoring::v3::MetricService::NewStub(
                  CreateCustomChannel(kGoogleStackdriverStatsAddress,
                                      GoogleDefaultCredentials(), args));
          opencensus::exporters::stats::StackdriverExporter::Register(
              std::move(stats_opts));
        });
  } else {
    // Disable OpenCensus stats
    grpc::internal::EnableOpenCensusStats(false);
  }
  if (config->cloud_logging.has_value()) {
    grpc::internal::RegisterLoggingFilter(
        new grpc::internal::ObservabilityLoggingSink(
            config->cloud_logging.value(), config->project_id, config->labels));
  }
  return absl::OkStatus();
}

}  // namespace experimental
}  // namespace grpc
