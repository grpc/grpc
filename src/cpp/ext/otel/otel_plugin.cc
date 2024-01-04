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

#include <type_traits>
#include <utility>

#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/unique_ptr.h"

#include <grpc/support/log.h>
#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/version_info.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/cpp/ext/otel/otel_client_filter.h"
#include "src/cpp/ext/otel/otel_server_call_tracer.h"

namespace grpc {
namespace internal {

// TODO(yashykt): Extend this to allow multiple OpenTelemetry plugins to be
// registered in the same binary.
struct OpenTelemetryPluginState* g_otel_plugin_state_;

const struct OpenTelemetryPluginState& OpenTelemetryPluginState() {
  GPR_DEBUG_ASSERT(g_otel_plugin_state_ != nullptr);
  return *g_otel_plugin_state_;
}

absl::string_view OpenTelemetryMethodKey() { return "grpc.method"; }

absl::string_view OpenTelemetryStatusKey() { return "grpc.status"; }

absl::string_view OpenTelemetryTargetKey() { return "grpc.target"; }

namespace {
absl::flat_hash_set<std::string> BaseMetrics() {
  return absl::flat_hash_set<std::string>{
      std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                      kClientAttemptStartedInstrumentName),
      std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                      kClientAttemptDurationInstrumentName),
      std::string(
          grpc::experimental::OpenTelemetryPluginBuilder::
              kClientAttemptSentTotalCompressedMessageSizeInstrumentName),
      std::string(
          grpc::experimental::OpenTelemetryPluginBuilder::
              kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName),
      std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                      kServerCallStartedInstrumentName),
      std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                      kServerCallDurationInstrumentName),
      std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                      kServerCallSentTotalCompressedMessageSizeInstrumentName),
      std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                      kServerCallRcvdTotalCompressedMessageSizeInstrumentName)};
}
}  // namespace

//
// OpenTelemetryPluginBuilderImpl
//

OpenTelemetryPluginBuilderImpl::OpenTelemetryPluginBuilderImpl()
    : metrics_(BaseMetrics()) {}

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::SetMeterProvider(
    std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider) {
  meter_provider_ = std::move(meter_provider);
  return *this;
}

OpenTelemetryPluginBuilderImpl& OpenTelemetryPluginBuilderImpl::EnableMetric(
    absl::string_view metric_name) {
  metrics_.emplace(metric_name);
  return *this;
}

OpenTelemetryPluginBuilderImpl& OpenTelemetryPluginBuilderImpl::DisableMetric(
    absl::string_view metric_name) {
  metrics_.erase(metric_name);
  return *this;
}

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::DisableAllMetrics() {
  metrics_.clear();
  return *this;
}

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::SetLabelsInjector(
    std::unique_ptr<LabelsInjector> labels_injector) {
  labels_injector_ = std::move(labels_injector);
  return *this;
}

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::SetTargetSelector(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_selector) {
  target_selector_ = std::move(target_selector);
  return *this;
}

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::SetTargetAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter) {
  target_attribute_filter_ = std::move(target_attribute_filter);
  return *this;
}

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::SetGenericMethodAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
        generic_method_attribute_filter) {
  generic_method_attribute_filter_ = std::move(generic_method_attribute_filter);
  return *this;
}

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::SetServerSelector(
    absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*args*/) const>
        server_selector) {
  server_selector_ = std::move(server_selector);
  return *this;
}

void OpenTelemetryPluginBuilderImpl::BuildAndRegisterGlobal() {
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>
      meter_provider = meter_provider_;
  delete g_otel_plugin_state_;
  g_otel_plugin_state_ = new struct OpenTelemetryPluginState;
  if (meter_provider == nullptr) {
    return;
  }
  auto meter = meter_provider->GetMeter("grpc-c++", GRPC_CPP_VERSION_STRING);
  if (metrics_.contains(grpc::experimental::OpenTelemetryPluginBuilder::
                            kClientAttemptStartedInstrumentName)) {
    g_otel_plugin_state_->client.attempt.started = meter->CreateUInt64Counter(
        std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                        kClientAttemptStartedInstrumentName),
        "Number of client call attempts started", "{attempt}");
  }
  if (metrics_.contains(grpc::experimental::OpenTelemetryPluginBuilder::
                            kClientAttemptDurationInstrumentName)) {
    g_otel_plugin_state_->client.attempt.duration =
        meter->CreateDoubleHistogram(
            std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                            kClientAttemptDurationInstrumentName),
            "End-to-end time taken to complete a client call attempt", "s");
  }
  if (metrics_.contains(
          grpc::experimental::OpenTelemetryPluginBuilder::
              kClientAttemptSentTotalCompressedMessageSizeInstrumentName)) {
    g_otel_plugin_state_->client.attempt.sent_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::experimental::OpenTelemetryPluginBuilder::
                    kClientAttemptSentTotalCompressedMessageSizeInstrumentName),
            "Compressed message bytes sent per client call attempt", "By");
  }
  if (metrics_.contains(
          grpc::experimental::OpenTelemetryPluginBuilder::
              kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName)) {
    g_otel_plugin_state_->client.attempt.rcvd_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::experimental::OpenTelemetryPluginBuilder::
                    kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName),
            "Compressed message bytes received per call attempt", "By");
  }
  if (metrics_.contains(grpc::experimental::OpenTelemetryPluginBuilder::
                            kServerCallStartedInstrumentName)) {
    g_otel_plugin_state_->server.call.started = meter->CreateUInt64Counter(
        std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                        kServerCallStartedInstrumentName),
        "Number of server calls started", "{call}");
  }
  if (metrics_.contains(grpc::experimental::OpenTelemetryPluginBuilder::
                            kServerCallDurationInstrumentName)) {
    g_otel_plugin_state_->server.call.duration = meter->CreateDoubleHistogram(
        std::string(grpc::experimental::OpenTelemetryPluginBuilder::
                        kServerCallDurationInstrumentName),
        "End-to-end time taken to complete a call from server transport's "
        "perspective",
        "s");
  }
  if (metrics_.contains(
          grpc::experimental::OpenTelemetryPluginBuilder::
              kServerCallSentTotalCompressedMessageSizeInstrumentName)) {
    g_otel_plugin_state_->server.call.sent_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::experimental::OpenTelemetryPluginBuilder::
                    kServerCallSentTotalCompressedMessageSizeInstrumentName),
            "Compressed message bytes sent per server call", "By");
  }
  if (metrics_.contains(
          grpc::experimental::OpenTelemetryPluginBuilder::
              kServerCallRcvdTotalCompressedMessageSizeInstrumentName)) {
    g_otel_plugin_state_->server.call.rcvd_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::experimental::OpenTelemetryPluginBuilder::
                    kServerCallRcvdTotalCompressedMessageSizeInstrumentName),
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
        builder->channel_init()
            ->RegisterFilter(
                GRPC_CLIENT_CHANNEL,
                &grpc::internal::OpenTelemetryClientFilter::kFilter)
            .If([target_selector = std::move(target_selector)](
                    const grpc_core::ChannelArgs& args) {
              // Only register the filter if no channel selector has been set or
              // the target selector returns true for the target.
              return target_selector == nullptr ||
                     target_selector(
                         args.GetString(GRPC_ARG_SERVER_URI).value_or(""));
            });
      });
}

}  // namespace internal

namespace experimental {

constexpr absl::string_view
    OpenTelemetryPluginBuilder::kClientAttemptStartedInstrumentName;
constexpr absl::string_view
    OpenTelemetryPluginBuilder::kClientAttemptDurationInstrumentName;
constexpr absl::string_view OpenTelemetryPluginBuilder::
    kClientAttemptSentTotalCompressedMessageSizeInstrumentName;
constexpr absl::string_view OpenTelemetryPluginBuilder::
    kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName;
constexpr absl::string_view
    OpenTelemetryPluginBuilder::kServerCallStartedInstrumentName;
constexpr absl::string_view
    OpenTelemetryPluginBuilder::kServerCallDurationInstrumentName;
constexpr absl::string_view OpenTelemetryPluginBuilder::
    kServerCallSentTotalCompressedMessageSizeInstrumentName;
constexpr absl::string_view OpenTelemetryPluginBuilder::
    kServerCallRcvdTotalCompressedMessageSizeInstrumentName;

//
// OpenTelemetryPluginBuilder
//

OpenTelemetryPluginBuilder::OpenTelemetryPluginBuilder()
    : impl_(std::make_unique<internal::OpenTelemetryPluginBuilderImpl>()) {}

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::SetMeterProvider(
    std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider) {
  impl_->SetMeterProvider(std::move(meter_provider));
  return *this;
}

OpenTelemetryPluginBuilder&
OpenTelemetryPluginBuilder::SetTargetAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter) {
  impl_->SetTargetAttributeFilter(std::move(target_attribute_filter));
  return *this;
}

OpenTelemetryPluginBuilder&
OpenTelemetryPluginBuilder::SetGenericMethodAttributeFilter(
    absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
        generic_method_attribute_filter) {
  impl_->SetGenericMethodAttributeFilter(
      std::move(generic_method_attribute_filter));
  return *this;
}

void OpenTelemetryPluginBuilder::BuildAndRegisterGlobal() {
  impl_->BuildAndRegisterGlobal();
}

}  // namespace experimental
}  // namespace grpc
