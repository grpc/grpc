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

#include <type_traits>
#include <utility>

#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"

#include <grpc/support/log.h>
#include <grpcpp/version_info.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
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

namespace {
absl::flat_hash_set<std::string> BaseMetrics() {
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
}  // namespace

//
// OpenTelemetryPluginBuilder
//

OpenTelemetryPluginBuilder::OpenTelemetryPluginBuilder()
    : metrics_(BaseMetrics()) {}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::SetMeterProvider(
    std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider) {
  meter_provider_ = std::move(meter_provider);
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::EnableMetric(
    absl::string_view metric_name) {
  metrics_.emplace(metric_name);
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::DisableMetric(
    absl::string_view metric_name) {
  metrics_.erase(metric_name);
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::DisableAllMetrics() {
  metrics_.clear();
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::SetLabelsInjector(
    std::unique_ptr<LabelsInjector> labels_injector) {
  labels_injector_ = std::move(labels_injector);
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::SetTargetSelector(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_selector) {
  target_selector_ = std::move(target_selector);
  return *this;
}

OpenTelemetryPluginBuilder&
OpenTelemetryPluginBuilder::SetTargetAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter) {
  target_attribute_filter_ = std::move(target_attribute_filter);
  return *this;
}

OpenTelemetryPluginBuilder&
OpenTelemetryPluginBuilder::SetGenericMethodAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
        generic_method_attribute_filter) {
  generic_method_attribute_filter_ = std::move(generic_method_attribute_filter);
  return *this;
}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::SetServerSelector(
    absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*args*/) const>
        server_selector) {
  server_selector_ = std::move(server_selector);
  return *this;
}

void OpenTelemetryPluginBuilder::BuildAndRegisterGlobal() {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>
      meter_provider = meter_provider_;
  delete g_otel_plugin_state_;
  g_otel_plugin_state_ = new struct OTelPluginState;
  if (meter_provider == nullptr) {
    return;
  }
  auto meter = meter_provider->GetMeter("grpc-c++", GRPC_CPP_VERSION_STRING);
  if (metrics_.contains(OTelClientAttemptStartedInstrumentName())) {
    g_otel_plugin_state_->client.attempt.started = meter->CreateUInt64Counter(
        std::string(OTelClientAttemptStartedInstrumentName()),
        "Number of client call attempts started", "{attempt}");
  }
  if (metrics_.contains(OTelClientAttemptDurationInstrumentName())) {
    g_otel_plugin_state_->client.attempt.duration =
        meter->CreateDoubleHistogram(
            std::string(OTelClientAttemptDurationInstrumentName()),
            "End-to-end time taken to complete a client call attempt", "s");
  }
  if (metrics_.contains(
          OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->client.attempt
        .sent_total_compressed_message_size = meter->CreateUInt64Histogram(
        std::string(
            OTelClientAttemptSentTotalCompressedMessageSizeInstrumentName()),
        "Compressed message bytes sent per client call attempt", "By");
  }
  if (metrics_.contains(
          OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->client.attempt
        .rcvd_total_compressed_message_size = meter->CreateUInt64Histogram(
        std::string(
            OTelClientAttemptRcvdTotalCompressedMessageSizeInstrumentName()),
        "Compressed message bytes received per call attempt", "By");
  }
  if (metrics_.contains(OTelServerCallStartedInstrumentName())) {
    g_otel_plugin_state_->server.call.started = meter->CreateUInt64Counter(
        std::string(OTelServerCallStartedInstrumentName()),
        "Number of server calls started", "{call}");
  }
  if (metrics_.contains(OTelServerCallDurationInstrumentName())) {
    g_otel_plugin_state_->server.call.duration = meter->CreateDoubleHistogram(
        std::string(OTelServerCallDurationInstrumentName()),
        "End-to-end time taken to complete a call from server transport's "
        "perspective",
        "s");
  }
  if (metrics_.contains(
          OTelServerCallSentTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->server.call.sent_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                OTelServerCallSentTotalCompressedMessageSizeInstrumentName()),
            "Compressed message bytes sent per server call", "By");
  }
  if (metrics_.contains(
          OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName())) {
    g_otel_plugin_state_->server.call.rcvd_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                OTelServerCallRcvdTotalCompressedMessageSizeInstrumentName()),
            "Compressed message bytes received per server call", "By");
  }
  g_otel_plugin_state_->labels_injector = std::move(labels_injector_);
  g_otel_plugin_state_->target_attribute_filter =
      std::move(target_attribute_filter_);
  g_otel_plugin_state_->generic_method_attribute_filter =
      std::move(generic_method_attribute_filter_);
  g_otel_plugin_state_->meter_provider = std::move(meter_provider);
  grpc_core::ServerCallTracerFactory::RegisterGlobal(
      new grpc::internal::OpenTelemetryServerCallTracerFactory());
  grpc_core::CoreConfiguration::RegisterBuilder(
      [target_selector = std::move(target_selector_)](
          grpc_core::CoreConfiguration::Builder* builder) mutable {
        builder->channel_init()->RegisterStage(
            GRPC_CLIENT_CHANNEL, /*priority=*/INT_MAX,
            [target_selector = std::move(target_selector)](
                grpc_core::ChannelStackBuilder* builder) {
              // Only register the filter if no channel selector has been set or
              // the target selector returns true for the target.
              if (target_selector == nullptr ||
                  target_selector(builder->channel_args()
                                      .GetString(GRPC_ARG_SERVER_URI)
                                      .value_or(""))) {
                builder->PrependFilter(
                    &grpc::internal::OpenTelemetryClientFilter::kFilter);
              }
              return true;
            });
      });
}

}  // namespace internal
}  // namespace grpc
