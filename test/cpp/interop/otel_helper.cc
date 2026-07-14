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

#ifndef HAVE_ABSEIL
#define HAVE_ABSEIL
#endif

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/log.h"

#include <grpcpp/ext/otel_plugin.h>

#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"

ABSL_FLAG(bool, enable_opentelemetry, false,
          "Whether to enable OpenTelemetry Tracing");

namespace grpc {
namespace testing {
namespace interop {

static std::shared_ptr<opentelemetry::sdk::trace::TracerProvider>
    g_tracer_provider;
static std::once_flag g_otel_init_once;

void MaybeRegisterOpenTelemetry() {
  std::call_once(g_otel_init_once, []() {
    if (!absl::GetFlag(FLAGS_enable_opentelemetry)) {
      return;
    }
    const char* otel_traces_exporter = std::getenv("OTEL_TRACES_EXPORTER");
    if (otel_traces_exporter != nullptr &&
        std::string(otel_traces_exporter) == "none") {
      LOG(INFO) << "OTEL_TRACES_EXPORTER is set to none. Tracing is disabled.";
      return;
    }

    // Create OTLP Grpc Exporter
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions opts;
    auto exporter =
        opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(opts);
    auto processor =
        opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
            std::move(exporter));
    g_tracer_provider =
        std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            std::move(processor));

    auto status =
        grpc::OpenTelemetryPluginBuilder()
            .SetTracerProvider(g_tracer_provider)
            .SetTextMapPropagator(grpc::OpenTelemetryPluginBuilder::
                                      MakeGrpcTraceBinTextMapPropagator())
            .BuildAndRegisterGlobal();

    if (!status.ok()) {
      LOG(ERROR) << "Failed to register gRPC OpenTelemetry Plugin: "
                 << status.ToString();
    } else {
      LOG(INFO)
          << "Successfully registered gRPC OpenTelemetry Plugin for tracing.";
    }
  });
}

void ForceFlushOpenTelemetry() {
  if (g_tracer_provider != nullptr) {
    g_tracer_provider->ForceFlush();
  }
}

}  // namespace interop
}  // namespace testing
}  // namespace grpc
