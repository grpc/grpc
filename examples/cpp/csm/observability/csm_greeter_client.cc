/*
 *
 * Copyright 2023 gRPC authors.
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

#include <grpcpp/ext/csm_observability.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/string_ref.h>
#include <sys/types.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "examples/cpp/otel/util.h"
#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

ABSL_FLAG(std::string, target, "xds:///helloworld:50051", "Target string");
ABSL_FLAG(std::string, prometheus_endpoint, "localhost:9464",
          "Prometheus exporter endpoint");

namespace {

absl::StatusOr<grpc::CsmObservability> InitializeObservability() {
  opentelemetry::exporter::metrics::PrometheusExporterOptions opts;
  // default was "localhost:9464" which causes connection issue across GKE pods
  opts.url = "0.0.0.0:9464";
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
  return grpc::CsmObservabilityBuilder()
      .SetMeterProvider(std::move(meter_provider))
      .BuildAndRegister();
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Setup CSM observability
  auto observability = InitializeObservability();
  if (!observability.ok()) {
    std::cerr << "CsmObservability::Init() failed: "
              << observability.status().ToString() << std::endl;
    return static_cast<int>(observability.status().code());
  }

  // Continuously send RPCs every second.
  RunClient(absl::GetFlag(FLAGS_target));

  return 0;
}
