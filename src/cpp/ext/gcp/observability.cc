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

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "google/api/monitored_resource.pb.h"
#include "google/devtools/cloudtrace/v2/tracing.grpc.pb.h"
#include "google/monitoring/v3/metric_service.grpc.pb.h"
#include "opencensus/exporters/stats/stackdriver/stackdriver_exporter.h"
#include "opencensus/exporters/trace/stackdriver/stackdriver_exporter.h"
#include "opencensus/stats/stats.h"
#include "opencensus/trace/sampler.h"
#include "opencensus/trace/trace_config.h"

#include <grpc/grpc.h>
#include <grpcpp/ext/gcp_observability.h>
#include <grpcpp/opencensus.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/ext/filters/logging/logging_filter.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/cpp/client/client_stats_interceptor.h"
#include "src/cpp/ext/filters/census/client_filter.h"
#include "src/cpp/ext/filters/census/grpc_plugin.h"
#include "src/cpp/ext/filters/census/open_census_call_tracer.h"
#include "src/cpp/ext/gcp/environment_autodetect.h"
#include "src/cpp/ext/gcp/observability_config.h"
#include "src/cpp/ext/gcp/observability_logging_sink.h"

namespace grpc {

namespace internal {
namespace {

grpc::internal::ObservabilityLoggingSink* g_logging_sink = nullptr;

bool g_gcp_observability_initialized = false;

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
  experimental::ClientStartedRpcs().RegisterForExport();
  experimental::ClientCompletedRpcs().RegisterForExport();
  experimental::ClientRoundtripLatency().RegisterForExport();
  internal::ClientApiLatency().RegisterForExport();
  experimental::ClientSentCompressedMessageBytesPerRpc().RegisterForExport();
  experimental::ClientReceivedCompressedMessageBytesPerRpc()
      .RegisterForExport();
  // Register server default views for GCP observability
  experimental::ServerStartedRpcs().RegisterForExport();
  experimental::ServerCompletedRpcs().RegisterForExport();
  experimental::ServerSentCompressedMessageBytesPerRpc().RegisterForExport();
  experimental::ServerReceivedCompressedMessageBytesPerRpc()
      .RegisterForExport();
  experimental::ServerServerLatency().RegisterForExport();
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
  if (g_gcp_observability_initialized) {
    grpc_core::Crash("GCP Observability for gRPC was already initialized.");
  }
  g_gcp_observability_initialized = true;
  grpc::internal::EnvironmentAutoDetect::Create(config->project_id);
  if (!config->cloud_trace.has_value()) {
    // Disable OpenCensus tracing
    grpc::internal::EnableOpenCensusTracing(false);
  }
  if (!config->cloud_monitoring.has_value()) {
    // Disable OpenCensus stats
    grpc::internal::EnableOpenCensusStats(false);
  } else {
    // Register the OpenCensus client stats interceptor factory if stats are
    // enabled. Note that this is currently separate from the OpenCensus Plugin
    // to avoid changing the behavior of the currently available OpenCensus
    // plugin.
    grpc::internal::RegisterGlobalClientStatsInterceptorFactory(
        new grpc::internal::OpenCensusClientInterceptorFactory);
  }
  if (config->cloud_logging.has_value()) {
    g_logging_sink = new grpc::internal::ObservabilityLoggingSink(
        config->cloud_logging.value(), config->project_id, config->labels);
    grpc_core::RegisterLoggingFilter(g_logging_sink);
  }
  // If tracing or monitoring is enabled, we need to register the OpenCensus
  // plugin as well.
  if (config->cloud_trace.has_value() || config->cloud_monitoring.has_value()) {
    grpc::RegisterOpenCensusPlugin();
  }
  // If tracing or monitoring is enabled, we need to detect the environment for
  // OpenCensus, set the labels and attributes and prepare the StackDriver
  // exporter.
  // Note that this should be the last step of GcpObservabilityInit() since we
  // can't register any more filters after grpc_init.
  if (config->cloud_trace.has_value() || config->cloud_monitoring.has_value()) {
    grpc_init();
    grpc_core::Notification notification;
    grpc::internal::EnvironmentAutoDetect::Get().NotifyOnDone(
        [&]() { notification.Notify(); });
    notification.WaitForNotification();
    auto* resource = grpc::internal::EnvironmentAutoDetect::Get().resource();
    if (config->cloud_trace.has_value()) {
      // Set up attributes for constant tracing
      std::vector<internal::OpenCensusRegistry::Attribute> attributes;
      attributes.reserve(resource->labels.size() + config->labels.size());
      // First insert in environment labels
      for (const auto& resource_label : resource->labels) {
        attributes.push_back(internal::OpenCensusRegistry::Attribute{
            absl::StrCat(resource->resource_type, ".", resource_label.first),
            resource_label.second});
      }
      // Then insert in labels from the GCP Observability config.
      for (const auto& constant_label : config->labels) {
        attributes.push_back(internal::OpenCensusRegistry::Attribute{
            constant_label.first, constant_label.second});
      }
      grpc::internal::OpenCensusRegistry::Get().RegisterConstantAttributes(
          std::move(attributes));
    }
    if (config->cloud_monitoring.has_value()) {
      grpc::internal::OpenCensusRegistry::Get().RegisterConstantLabels(
          config->labels);
      RegisterOpenCensusViewsForGcpObservability();
    }
    // Note that we are setting up the exporters after registering the
    // attributes and labels to avoid a case where the exporters start an RPC
    // before we are ready.
    if (config->cloud_trace.has_value()) {
      // Set up the StackDriver Exporter for tracing.
      opencensus::trace::TraceConfig::SetCurrentTraceParams(
          {kMaxAttributes, kMaxAnnotations, kMaxMessageEvents, kMaxLinks,
           opencensus::trace::ProbabilitySampler(
               config->cloud_trace->sampling_rate)});
      opencensus::exporters::trace::StackdriverOptions trace_opts;
      trace_opts.project_id = config->project_id;
      ChannelArguments args;
      args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
      trace_opts.trace_service_stub =
          ::google::devtools::cloudtrace::v2::TraceService::NewStub(
              CreateCustomChannel(kGoogleStackdriverTraceAddress,
                                  GoogleDefaultCredentials(), args));
      opencensus::exporters::trace::StackdriverExporter::Register(
          std::move(trace_opts));
    }
    if (config->cloud_monitoring.has_value()) {
      // Set up the StackDriver Exporter for monitoring.
      opencensus::exporters::stats::StackdriverOptions stats_opts;
      stats_opts.project_id = config->project_id;
      stats_opts.monitored_resource.set_type(resource->resource_type);
      stats_opts.monitored_resource.mutable_labels()->insert(
          resource->labels.begin(), resource->labels.end());
      ChannelArguments args;
      args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
      stats_opts.metric_service_stub =
          google::monitoring::v3::MetricService::NewStub(
              CreateCustomChannel(kGoogleStackdriverStatsAddress,
                                  GoogleDefaultCredentials(), args));
      opencensus::exporters::stats::StackdriverExporter::Register(
          std::move(stats_opts));
    }
    grpc_shutdown();
  }
  return absl::OkStatus();
}

void GcpObservabilityClose() {
  if (g_logging_sink != nullptr) {
    g_logging_sink->FlushAndClose();
  }
  // Currently, GcpObservabilityClose() only supports flushing logs. Stats and
  // tracing get automatically flushed at a regular interval, so sleep for an
  // interval to make sure that those are flushed too.
  absl::SleepFor(absl::Seconds(25));
}

}  // namespace internal

namespace experimental {

absl::Status GcpObservabilityInit() {
  return grpc::internal::GcpObservabilityInit();
}

void GcpObservabilityClose() { return grpc::internal::GcpObservabilityClose(); }

}  // namespace experimental

//
// GcpObservability
//

absl::StatusOr<GcpObservability> GcpObservability::Init() {
  absl::Status status = grpc::internal::GcpObservabilityInit();
  if (!status.ok()) {
    return status;
  }
  GcpObservability obj;
  obj.impl_ = std::make_unique<GcpObservabilityImpl>();
  return obj;
}

GcpObservability::GcpObservability(GcpObservability&& other) noexcept
    : impl_(std::move(other.impl_)) {}

GcpObservability& GcpObservability::operator=(
    GcpObservability&& other) noexcept {
  if (this != &other) {
    impl_ = std::move(other.impl_);
  }
  return *this;
}

//
// GcpObservability::GcpObservabilityImpl
//

GcpObservability::GcpObservabilityImpl::~GcpObservabilityImpl() {
  grpc::internal::GcpObservabilityClose();
}

}  // namespace grpc
