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

#include "src/cpp/ext/gcp/observability.h"

#include <stdint.h>

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "opencensus/exporters/stats/stackdriver/stackdriver_exporter.h"
#include "opencensus/exporters/trace/stackdriver/stackdriver_exporter.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/trace_config.h"

#include <grpcpp/opencensus.h>
#include <grpcpp/support/config.h>

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
}  // namespace

absl::Status GcpObservabilityInit() {
  auto config = grpc::internal::GcpObservabilityConfig::ReadFromEnv();
  if (!config.ok()) {
    return config.status();
  }
  grpc::RegisterOpenCensusPlugin();
  grpc::RegisterOpenCensusViewsForExport();
  if (config->cloud_trace.has_value()) {
    opencensus::trace::TraceConfig::SetCurrentTraceParams(
        {kMaxAttributes, kMaxAnnotations, kMaxMessageEvents, kMaxLinks,
         opencensus::trace::ProbabilitySampler(
             config->cloud_trace->sampling_rate)});
    opencensus::exporters::trace::StackdriverOptions trace_opts;
    trace_opts.project_id = config->project_id;
    opencensus::exporters::trace::StackdriverExporter::Register(
        std::move(trace_opts));
  }
  if (config->cloud_monitoring.has_value()) {
    opencensus::exporters::stats::StackdriverOptions stats_opts;
    stats_opts.project_id = config->project_id;
    opencensus::exporters::stats::StackdriverExporter::Register(
        std::move(stats_opts));
  }
  return absl::Status();
}

}  // namespace experimental
}  // namespace grpc
