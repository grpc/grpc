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
#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#ifdef BAZEL_BUILD
#include "examples/cpp/otel/util.h"
#else
#include "util.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");
ABSL_FLAG(std::string, prometheus_endpoint, "localhost:9465",
          "Prometheus exporter endpoint");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Register a global gRPC OpenTelemetry plugin configured with a prometheus
  // exporter.
  opentelemetry::exporter::metrics::PrometheusExporterOptions opts;
  opts.url = absl::GetFlag(FLAGS_prometheus_endpoint);
  opts.without_otel_scope = false;
  auto prometheus_exporter =
      opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(opts);
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  // The default histogram boundaries are not granular enough for RPCs. Override
  // the "grpc.client.attempt.duration" view as recommended by
  // https://github.com/grpc/proposal/blob/master/A66-otel-stats.md.
  AddLatencyView(meter_provider.get(), "grpc.client.attempt.duration", "s");
  meter_provider->AddMetricReader(std::move(prometheus_exporter));
  auto status = grpc::OpenTelemetryPluginBuilder()
                    .SetMeterProvider(std::move(meter_provider))
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
