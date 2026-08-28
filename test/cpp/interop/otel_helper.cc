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

#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#endif

ABSL_FLAG(bool, enable_opentelemetry, false,
          "Whether to enable OpenTelemetry Tracing");
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
static std::once_flag g_otel_init_once;
#endif

void MaybeRegisterOpenTelemetry() {
#ifdef GRPC_HAS_OTEL_TRACING
  std::call_once(g_otel_init_once, []() {
    bool enabled = absl::GetFlag(FLAGS_enable_opentelemetry) ||
                   absl::GetFlag(FLAGS_otel_exporter) == "otlp";
    if (!enabled || absl::GetFlag(FLAGS_otel_exporter) == "none") {
      return;
    }

    // Create OTLP Grpc Exporter
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions trace_opts;
    if (!absl::GetFlag(FLAGS_otel_collector_address).empty()) {
      trace_opts.endpoint = absl::GetFlag(FLAGS_otel_collector_address);
    }

    auto trace_exporter =
        opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(
            trace_opts);
    auto processor =
        opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
            std::move(trace_exporter));
    g_tracer_provider =
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            std::move(processor));

    grpc::OpenTelemetryPluginBuilder builder;
    builder.SetTracerProvider(g_tracer_provider);
    builder.SetTextMapPropagator(
        std::make_unique<
            opentelemetry::trace::propagation::HttpTraceContext>());

    auto status = builder.BuildAndRegisterGlobal();

    if (!status.ok()) {
      LOG(ERROR) << "Failed to register gRPC OpenTelemetry Plugin: "
                 << status.ToString();
    } else {
      LOG(INFO) << "Successfully registered gRPC OpenTelemetry Plugin for "
                   "tracing.";
    }
  });
#endif
}

void ForceFlushOpenTelemetry() {
#ifdef GRPC_HAS_OTEL_TRACING
  if (g_tracer_provider != nullptr) {
    g_tracer_provider->ForceFlush();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
}

}  // namespace interop
}  // namespace testing
}  // namespace grpc
