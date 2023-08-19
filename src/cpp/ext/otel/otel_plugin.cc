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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/otel/otel_plugin.h"

#include <limits.h>

#include <utility>

#include "absl/container/flat_hash_map.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/cpp/ext/otel/otel_client_filter.h"
#include "src/cpp/ext/otel/otel_server_call_tracer.h"

namespace grpc {
namespace internal {

// TODO(yashykt): Extend this to allow multiple OTel plugins to be registered in
// the same binary.
struct OTelPluginState* g_otel_plugin_state_;

const struct OTelPluginState& OTelPluginState() {
  GPR_DEBUG_ASSERT(g_otel_plugin_state_ != nullptr);
  return *g_otel_plugin_state_;
}

absl::string_view OTelMethodKey() { return "grpc.method"; }

absl::string_view OTelStatusKey() { return "grpc.status"; }

absl::string_view OTelTargetKey() { return "grpc.target"; }

absl::string_view OTelAuthorityKey() { return "grpc.authority"; }

absl::string_view OTelClientAttemptStartedInstrumentName() {
  return "grpc.client.attempt.started";
}
absl::string_view OTelClientAttemptDurationInstrumentName() {
  return "grpc.client.attempt.duration";
}

absl::string_view
OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName() {
  return "grpc.client.attempt.sent_total_compressed_message_size";
}

absl::string_view
OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName() {
  return "grpc.client.attempt.rcvd_total_compressed_message_size";
}

absl::string_view OTelServerCallStartedInstrumentName() {
  return "grpc.server.call.started";
}

absl::string_view OTelServerCallDurationInstrumentName() {
  return "grpc.server.call.duration";
}

absl::string_view OTelServerCallSentTotalCompressedMessageSizeInstrumentName() {
  return "grpc.server.call.sent_total_compressed_message_size";
}

absl::string_view OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName() {
  return "grpc.server.call.rcvd_total_compressed_message_size";
}
//
// OpenTelemetryPluginBuilder
//

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::SetMeterProvider(
    std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider) {
  meter_provider_ = std::move(meter_provider);
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::EnableMetrics(
    const absl::flat_hash_set<absl::string_view>& metric_names) {
  for (auto& metric_name : metric_names) {
    metrics_.emplace(metric_name);
  }
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::DisableMetrics(
    const absl::flat_hash_set<absl::string_view>& metric_names) {
  for (auto& metric_name : metric_names) {
    metrics_.erase(metric_name);
  }
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::SetLabelsInjector(
    std::unique_ptr<LabelsInjector> labels_injector) {
  labels_injector_ = std::move(labels_injector);
  return *this;
}

void OpenTelemetryPluginBuilder::BuildAndRegisterGlobal() {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>
      meter_provider = meter_provider_;
  if (meter_provider == nullptr) {
    meter_provider = opentelemetry::metrics::Provider::GetMeterProvider();
  }
  auto meter = meter_provider->GetMeter("grpc");
  delete g_otel_plugin_state_;
  g_otel_plugin_state_ = new struct OTelPluginState;
  g_otel_plugin_state_->meter_provider = std::move(meter_provider);
  if (metrics_.contains(OTelClientAttemptStartedInstrumentName())) {
    g_otel_plugin_state_->client.attempt.started = meter->CreateUInt64Counter(
        std::string(OTelClientAttemptStartedInstrumentName()));
  }
  if (metrics_.contains(OTelClientAttemptDurationInstrumentName())) {
    g_otel_plugin_state_->client.attempt.duration =
        meter->CreateDoubleHistogram(
            std::string(OTelClientAttemptDurationInstrumentName()));
  }
  if (metrics_.contains(
          OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->client.attempt.sent_total_compressed_message_size =
        meter->CreateUInt64Histogram(std::string(
            OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName()));
  }
  if (metrics_.contains(
          OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->client.attempt.rcvd_total_compressed_message_size =
        meter->CreateUInt64Histogram(std::string(
            OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName()));
  }
  if (metrics_.contains(OTelServerCallStartedInstrumentName())) {
    g_otel_plugin_state_->server.call.started = meter->CreateUInt64Counter(
        std::string(OTelServerCallStartedInstrumentName()));
  }
  if (metrics_.contains(OTelServerCallDurationInstrumentName())) {
    g_otel_plugin_state_->server.call.duration = meter->CreateDoubleHistogram(
        std::string(OTelServerCallDurationInstrumentName()));
  }
  if (metrics_.contains(
          OTelServerCallSentTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->server.call.sent_total_compressed_message_size =
        meter->CreateUInt64Histogram(std::string(
            OTelServerCallSentTotalCompressedMessageSizeInstrumentName()));
  }
  if (metrics_.contains(
          OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->server.call.rcvd_total_compressed_message_size =
        meter->CreateUInt64Histogram(std::string(
            OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName()));
  }
  g_otel_plugin_state_->labels_injector = std::move(labels_injector_);
  grpc_core::ServerCallTracerFactory::RegisterGlobal(
      new grpc::internal::OpenTelemetryServerCallTracerFactory);
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->channel_init()->RegisterStage(
            GRPC_CLIENT_CHANNEL, /*priority=*/INT_MAX,
            [](grpc_core::ChannelStackBuilder* builder) {
              builder->PrependFilter(
                  &grpc::internal::OpenTelemetryClientFilter::kFilter);
              return true;
            });
      });
}

absl::flat_hash_set<std::string> OpenTelemetryPluginBuilder::BaseMetrics() {
  return absl::flat_hash_set<std::string>{
      std::string(OTelClientAttemptStartedInstrumentName()),
      std::string(OTelClientAttemptDurationInstrumentName()),
      std::string(
          OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName()),
      std::string(
          OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName()),
      std::string(OTelServerCallStartedInstrumentName()),
      std::string(OTelServerCallDurationInstrumentName()),
      std::string(OTelServerCallSentTotalCompressedMessageSizeInstrumentName()),
      std::string(
          OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName())};
}

}  // namespace internal
}  // namespace grpc
