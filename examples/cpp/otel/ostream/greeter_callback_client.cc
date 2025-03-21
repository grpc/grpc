/*
 *
 * Copyright 2021 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

// Explicitly define HAVE_ABSEIL to avoid conflict with OTel's Abseil
// version. Refer
// https://github.com/open-telemetry/opentelemetry-cpp/issues/1042.
#ifndef HAVE_ABSEIL
#define HAVE_ABSEIL
#endif

#include <grpcpp/ext/otel_plugin.h>

#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "opentelemetry/exporters/ostream/metric_exporter.h"
#include "opentelemetry/exporters/ostream/metric_exporter_factory.h"
#include "opentelemetry/exporters/ostream/span_exporter_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"

#ifdef BAZEL_BUILD
#include "examples/cpp/otel/util.h"
#else
#include "../util.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Register a global gRPC OpenTelemetry plugin configured with an ostream
  // exporter.
  auto ostream_metrics_exporter =
      opentelemetry::exporter::metrics::OStreamMetricExporterFactory::Create();
  opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions
      reader_options;
  reader_options.export_interval_millis = std::chrono::milliseconds(1000);
  reader_options.export_timeout_millis = std::chrono::milliseconds(500);
  auto reader =
      opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
          std::move(ostream_metrics_exporter), reader_options);
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  // The default histogram boundaries are not granular enough for RPCs. Override
  // the "grpc.client.attempt.duration" view as recommended by
  // https://github.com/grpc/proposal/blob/master/A66-otel-stats.md.
  AddLatencyView(meter_provider.get(), "grpc.client.attempt.duration", "s");
  meter_provider->AddMetricReader(std::move(reader));
  auto tracer_provider =
      std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
          opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
              opentelemetry::exporter::trace::OStreamSpanExporterFactory::
                  Create()));
  auto status =
      grpc::OpenTelemetryPluginBuilder()
          .SetMeterProvider(std::move(meter_provider))
          .SetTracerProvider(std::move(tracer_provider))
          .SetTextMapPropagator(
              std::make_unique<
                  opentelemetry::trace::propagation::HttpTraceContext>())
          .BuildAndRegisterGlobal();
  if (!status.ok()) {
    std::cerr << "Failed to register gRPC OpenTelemetry Plugin: "
              << status.ToString() << std::endl;
    return static_cast<int>(status.code());
  }

  // Continuously send RPCs every second.
  RunClient(absl::GetFlag(FLAGS_target));

  return 0;
}
