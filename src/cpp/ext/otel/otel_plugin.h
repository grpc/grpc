//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H
#define GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <memory>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/shared_ptr.h"

namespace grpc {
namespace internal {

struct OTelPluginState {
  struct Client {
    struct Attempt {
      std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> started;
      std::unique_ptr<opentelemetry::metrics::Histogram<double>> duration;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          sent_total_compressed_message_size;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          rcvd_total_compressed_message_size;
    } attempt;
  } client;
  struct Server {
    struct Call {
      std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> started;
      std::unique_ptr<opentelemetry::metrics::Histogram<double>> duration;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          sent_total_compressed_message_size;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          rcvd_total_compressed_message_size;
    } call;
  } server;
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>
      meter_provider;
};

const struct OTelPluginState& OTelPluginState();

// Tags
absl::string_view OTelMethodKey();
absl::string_view OTelStatusKey();

// Metrics
absl::string_view OTelClientAttemptStartedInstrumentName();
absl::string_view OTelClientAttemptDurationInstrumentName();
absl::string_view
OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName();
absl::string_view
OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName();
absl::string_view OTelServerCallStartedInstrumentName();
absl::string_view OTelServerCallDurationInstrumentName();
absl::string_view OTelServerCallSentTotalCompressedMessageSizeInstrumentName();
absl::string_view OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName();

class OpenTelemetryPluginBuilder {
 public:
  OpenTelemetryPluginBuilder& SetMeterProvider(
      std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider);
  // Enable metrics in \a metric_names
  OpenTelemetryPluginBuilder& EnableMetrics(
      const absl::flat_hash_set<absl::string_view>& metric_names);
  // Disable metrics in \a metric_names
  OpenTelemetryPluginBuilder& DisableMetrics(
      const absl::flat_hash_set<absl::string_view>& metric_names);
  // Builds and registers the OTel Plugin
  void BuildAndRegisterGlobal();

  // The base set of metrics -
  // grpc.client.attempt.started
  // grpc.client.attempt.duration
  // grpc.client.attempt.sent_total_compressed_message_size
  // grpc.client.attempt.rcvd_total_compressed_message_size
  // grpc.server.call.started
  // grpc.server.call.duration
  // grpc.server.call.sent_total_compressed_message_size
  // grpc.server.call.rcvd_total_compressed_message_size
  static absl::flat_hash_set<std::string> BaseMetrics();

 private:
  std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider_;
  absl::flat_hash_set<std::string> metrics_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H
