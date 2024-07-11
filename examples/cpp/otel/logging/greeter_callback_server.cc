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

#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_options.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/processor.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"

#include <grpcpp/ext/otel_plugin.h>

#ifdef BAZEL_BUILD
#include "examples/cpp/otel/util.h"
#else
#include "util.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(std::string, otlp_endpoint, "localhost:4317",
          "OTLP ingestion endpoint");

int main(int argc, char** argv) {
  // Register a global gRPC OpenTelemetry plugin configured with an
  // OTLP-over-gRPC log record exporter.
  opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterOptions log_opts;
  log_opts.endpoint = absl::GetFlag(FLAGS_otlp_endpoint);
  // Create OTLP exporter instance
  auto exporter =
      opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterFactory::Create(
          log_opts);
  auto processor =
      opentelemetry::sdk::logs::SimpleLogRecordProcessorFactory::Create(
          std::move(exporter));
  std::shared_ptr<opentelemetry::logs::LoggerProvider> logger_provider =
      opentelemetry::sdk::logs::LoggerProviderFactory::Create(
          std::move(processor));
  auto status = grpc::OpenTelemetryPluginBuilder()
                    .SetLoggerProvider(std::move(logger_provider))
                    .BuildAndRegisterGlobal();
  if (!status.ok()) {
    std::cerr << "Failed to register gRPC OpenTelemetry Plugin: "
              << status.ToString() << std::endl;
    return static_cast<int>(status.code());
  }
  RunServer(absl::GetFlag(FLAGS_port));
  return 0;
}
