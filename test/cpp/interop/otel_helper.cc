//
//
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
//
//

#include "test/cpp/interop/otel_helper.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/log/log.h"

#if __has_include(<grpcpp/ext/otel_plugin.h>) && \
    __has_include("opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h")
#define GRPC_HAS_OTEL_TRACING 1
#endif

#ifdef GRPC_HAS_OTEL_TRACING
#include <grpcpp/ext/otel_plugin.h>

#include "opentelemetry/context/propagation/composite_propagator.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#endif

ABSL_FLAG(bool, enable_opentelemetry, false,
          "Whether to enable OpenTelemetry Tracing and Metrics");
ABSL_FLAG(std::string, otel_exporter, "",
          "OpenTelemetry exporter type (otlp, none)");
ABSL_FLAG(std::string, otel_collector_address, "",
          "OpenTelemetry collector address");

namespace grpc {
namespace testing {
namespace interop {

#ifdef GRPC_HAS_OTEL_TRACING
static std::shared_ptr<opentelemetry::sdk::trace::TracerProvider>
    g_tracer_provider;
static std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider>
    g_meter_provider;
static std::once_flag g_otel_init_once;
static std::mutex g_tracer_provider_mu;
#endif

void MaybeRegisterOpenTelemetry() {
#ifdef GRPC_HAS_OTEL_TRACING
  std::call_once(g_otel_init_once, []() {
    bool enabled = absl::GetFlag(FLAGS_enable_opentelemetry) ||
                   absl::GetFlag(FLAGS_otel_exporter) == "otlp";
    if (!enabled) {
      return;
    }
    const char* otel_traces_exporter = std::getenv("OTEL_TRACES_EXPORTER");
    if (otel_traces_exporter != nullptr &&
        std::string(otel_traces_exporter) == "none") {
      LOG(INFO) << "OTEL_TRACES_EXPORTER is set to none. Tracing is disabled.";
      return;
    }

    // Create OTLP Grpc Exporters
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions trace_opts;
    opentelemetry::exporter::otlp::OtlpGrpcMetricExporterOptions metric_opts;
    if (!absl::GetFlag(FLAGS_otel_collector_address).empty()) {
      trace_opts.endpoint = absl::GetFlag(FLAGS_otel_collector_address);
      metric_opts.endpoint = absl::GetFlag(FLAGS_otel_collector_address);
    }

    auto trace_exporter =
        opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(
            trace_opts);
    auto processor =
        opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
            std::move(trace_exporter));
    auto tracer_provider =
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            std::move(processor));

    auto metric_exporter =
        opentelemetry::exporter::otlp::OtlpGrpcMetricExporterFactory::Create(
            metric_opts);
    opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions
        reader_opts;
    reader_opts.export_interval_millis = std::chrono::milliseconds(100);
    auto metric_reader =
        opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::
            Create(std::move(metric_exporter), reader_opts);
    auto meter_provider =
        std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
    meter_provider->AddMetricReader(std::move(metric_reader));

    {
      std::lock_guard<std::mutex> lock(g_tracer_provider_mu);
      g_tracer_provider = tracer_provider;
      g_meter_provider = meter_provider;
    }

    grpc::OpenTelemetryPluginBuilder builder;
    builder.SetTracerProvider(tracer_provider);
    builder.SetMeterProvider(meter_provider);
    std::vector<
        std::unique_ptr<opentelemetry::context::propagation::TextMapPropagator>>
        propagators;
    propagators.push_back(
        std::make_unique<
            opentelemetry::trace::propagation::HttpTraceContext>());
    propagators.push_back(
        grpc::OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator());
    builder.SetTextMapPropagator(
        std::make_unique<
            opentelemetry::context::propagation::CompositePropagator>(
            std::move(propagators)));
    builder.EnableMetrics({
        "grpc.tcp.*",
        "grpc.client.*",
        "grpc.server.*",
    });

    auto status = builder.BuildAndRegisterGlobal();

    if (!status.ok()) {
      LOG(ERROR) << "Failed to register gRPC OpenTelemetry Plugin: "
                 << status.ToString();
    } else {
      LOG(INFO) << "Successfully registered gRPC OpenTelemetry Plugin for "
                   "tracing and metrics.";
    }
  });
#endif
}

void ForceFlushOpenTelemetry() {
#ifdef GRPC_HAS_OTEL_TRACING
  std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> tracer_provider;
  std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> meter_provider;
  {
    std::lock_guard<std::mutex> lock(g_tracer_provider_mu);
    tracer_provider = g_tracer_provider;
    meter_provider = g_meter_provider;
  }
  if (tracer_provider != nullptr) {
    tracer_provider->ForceFlush();
  }
  if (meter_provider != nullptr) {
    meter_provider->ForceFlush();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
}

}  // namespace interop
}  // namespace testing
}  // namespace grpc
