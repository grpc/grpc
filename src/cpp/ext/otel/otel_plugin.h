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

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/shared_ptr.h"

#include "src/core/lib/transport/metadata_batch.h"

namespace grpc {
namespace internal {

// An iterable container interface that can be used as a return type for the
// OTel plugin's label injector.
class LabelsIterable {
 public:
  virtual ~LabelsIterable() = default;

  // Returns the key-value label at the current position or absl::nullopt if the
  // iterator has reached the end.
  virtual absl::optional<std::pair<absl::string_view, absl::string_view>>
  Next() = 0;

  virtual size_t Size() const = 0;

  // Resets position of iterator to the start.
  virtual void ResetIteratorPosition() = 0;
};

// An interface that allows you to add additional labels on the calls traced
// through the OTel plugin.
class LabelsInjector {
 public:
  virtual ~LabelsInjector() {}
  // Read the incoming initial metadata to get the set of labels to be added to
  // metrics. (Does not include the local labels.)
  virtual std::unique_ptr<LabelsIterable> GetPeerLabels(
      grpc_metadata_batch* incoming_initial_metadata) = 0;

  // Get the local labels to be added to metrics. To be used when the peer
  // metadata is not available, for example, for started RPCs metric.
  // It is the responsibility of the implementation to make sure that the
  // backing store for the absl::string_view remains valid for the lifetime of
  // gRPC.
  virtual std::unique_ptr<LabelsIterable> GetLocalLabels() = 0;

  // Modify the outgoing initial metadata with metadata information to be sent
  // to the peer.
  virtual void AddLabels(grpc_metadata_batch* outgoing_initial_metadata) = 0;
};

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
  std::unique_ptr<LabelsInjector> labels_injector;
};

const struct OTelPluginState& OTelPluginState();

// Tags
absl::string_view OTelMethodKey();
absl::string_view OTelStatusKey();
absl::string_view OTelTargetKey();

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

  OpenTelemetryPluginBuilder& SetLabelsInjector(
      std::unique_ptr<LabelsInjector> labels_injector);

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
  std::unique_ptr<LabelsInjector> labels_injector_;
  absl::flat_hash_set<std::string> metrics_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H
