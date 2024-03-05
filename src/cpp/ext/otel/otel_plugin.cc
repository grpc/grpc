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

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/cpp/ext/otel/otel_client_filter.h"
#include "src/cpp/ext/otel/otel_server_call_tracer.h"

namespace grpc {
namespace internal {

absl::string_view OpenTelemetryMethodKey() { return "grpc.method"; }

absl::string_view OpenTelemetryStatusKey() { return "grpc.status"; }

absl::string_view OpenTelemetryTargetKey() { return "grpc.target"; }

namespace {
absl::flat_hash_set<std::string> BaseMetrics() {
  return absl::flat_hash_set<std::string>{
      std::string(grpc::OpenTelemetryPluginBuilder::
                      kClientAttemptStartedInstrumentName),
      std::string(grpc::OpenTelemetryPluginBuilder::
                      kClientAttemptDurationInstrumentName),
      std::string(
          grpc::OpenTelemetryPluginBuilder::
              kClientAttemptSentTotalCompressedMessageSizeInstrumentName),
      std::string(
          grpc::OpenTelemetryPluginBuilder::
              kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName),
      std::string(
          grpc::OpenTelemetryPluginBuilder::kServerCallStartedInstrumentName),
      std::string(
          grpc::OpenTelemetryPluginBuilder::kServerCallDurationInstrumentName),
      std::string(grpc::OpenTelemetryPluginBuilder::
                      kServerCallSentTotalCompressedMessageSizeInstrumentName),
      std::string(grpc::OpenTelemetryPluginBuilder::
                      kServerCallRcvdTotalCompressedMessageSizeInstrumentName)};
}
}  // namespace

//
// OpenTelemetryPluginBuilderImpl
//

OpenTelemetryPluginBuilderImpl::OpenTelemetryPluginBuilderImpl()
    : metrics_(BaseMetrics()) {}

OpenTelemetryPluginBuilderImpl::~OpenTelemetryPluginBuilderImpl() = default;

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

OpenTelemetryPluginBuilderImpl& OpenTelemetryPluginBuilderImpl::AddPluginOption(
    std::unique_ptr<InternalOpenTelemetryPluginOption> option) {
  // We allow a limit of 64 plugin options to be registered at this time.
  GPR_ASSERT(plugin_options_.size() < 64);
  plugin_options_.push_back(std::move(option));
  return *this;
}

absl::Status OpenTelemetryPluginBuilderImpl::BuildAndRegisterGlobal() {
  if (meter_provider_ == nullptr) {
    return absl::OkStatus();
  }
  grpc_core::GlobalStatsPluginRegistry::RegisterStatsPlugin(
      std::make_shared<OpenTelemetryPlugin>(
          metrics_, meter_provider_, std::move(target_selector_),
          std::move(target_attribute_filter_),
          std::move(generic_method_attribute_filter_),
          std::move(server_selector_), std::move(plugin_options_)));
  grpc_core::ServerCallTracerFactory::RegisterGlobal(
      new grpc::internal::OpenTelemetryServerCallTracerFactory());
  grpc_core::CoreConfiguration::RegisterBuilder(
      [target_selector = std::move(target_selector_)](
          grpc_core::CoreConfiguration::Builder* builder) mutable {
        builder->channel_init()->RegisterFilter(
            GRPC_CLIENT_CHANNEL,
            &grpc::internal::OpenTelemetryClientFilter::kFilter);
      });
  return absl::OkStatus();
}

OpenTelemetryPlugin::OpenTelemetryPlugin(
    const absl::flat_hash_set<std::string>& metrics,
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>
        meter_provider,
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_selector,
    absl::AnyInvocable<bool(absl::string_view /*target*/) const>
        target_attribute_filter,
    absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
        generic_method_attribute_filter,
    absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*args*/) const>
        server_selector,
    std::vector<std::unique_ptr<InternalOpenTelemetryPluginOption>>
        plugin_options)
    : meter_provider_(std::move(meter_provider)),
      target_selector_(std::move(target_selector)),
      target_attribute_filter_(std::move(target_attribute_filter)),
      generic_method_attribute_filter_(
          std::move(generic_method_attribute_filter)),
      server_selector_(std::move(server_selector)),
      plugin_options_(std::move(plugin_options)) {
  auto meter = meter_provider_->GetMeter("grpc-c++", GRPC_CPP_VERSION_STRING);
  // Per-call metrics.
  if (metrics.contains(grpc::OpenTelemetryPluginBuilder::
                           kClientAttemptStartedInstrumentName)) {
    client_.attempt.started = meter->CreateUInt64Counter(
        std::string(grpc::OpenTelemetryPluginBuilder::
                        kClientAttemptStartedInstrumentName),
        "Number of client call attempts started", "{attempt}");
  }
  if (metrics.contains(grpc::OpenTelemetryPluginBuilder::
                           kClientAttemptDurationInstrumentName)) {
    client_.attempt.duration = meter->CreateDoubleHistogram(
        std::string(grpc::OpenTelemetryPluginBuilder::
                        kClientAttemptDurationInstrumentName),
        "End-to-end time taken to complete a client call attempt", "s");
  }
  if (metrics.contains(
          grpc::OpenTelemetryPluginBuilder::
              kClientAttemptSentTotalCompressedMessageSizeInstrumentName)) {
    client_.attempt.sent_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::OpenTelemetryPluginBuilder::
                    kClientAttemptSentTotalCompressedMessageSizeInstrumentName),
            "Compressed message bytes sent per client call attempt", "By");
  }
  if (metrics.contains(
          grpc::OpenTelemetryPluginBuilder::
              kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName)) {
    client_.attempt.rcvd_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::OpenTelemetryPluginBuilder::
                    kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName),
            "Compressed message bytes received per call attempt", "By");
  }
  if (metrics.contains(
          grpc::OpenTelemetryPluginBuilder::kServerCallStartedInstrumentName)) {
    server_.call.started = meter->CreateUInt64Counter(
        std::string(
            grpc::OpenTelemetryPluginBuilder::kServerCallStartedInstrumentName),
        "Number of server calls started", "{call}");
  }
  if (metrics.contains(grpc::OpenTelemetryPluginBuilder::
                           kServerCallDurationInstrumentName)) {
    server_.call.duration = meter->CreateDoubleHistogram(
        std::string(grpc::OpenTelemetryPluginBuilder::
                        kServerCallDurationInstrumentName),
        "End-to-end time taken to complete a call from server transport's "
        "perspective",
        "s");
  }
  if (metrics.contains(
          grpc::OpenTelemetryPluginBuilder::
              kServerCallSentTotalCompressedMessageSizeInstrumentName)) {
    server_.call.sent_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::OpenTelemetryPluginBuilder::
                    kServerCallSentTotalCompressedMessageSizeInstrumentName),
            "Compressed message bytes sent per server call", "By");
  }
  if (metrics.contains(
          grpc::OpenTelemetryPluginBuilder::
              kServerCallRcvdTotalCompressedMessageSizeInstrumentName)) {
    server_.call.rcvd_total_compressed_message_size =
        meter->CreateUInt64Histogram(
            std::string(
                grpc::OpenTelemetryPluginBuilder::
                    kServerCallRcvdTotalCompressedMessageSizeInstrumentName),
            "Compressed message bytes received per server call", "By");
  }
  // Non-per-call metrics.
  grpc_core::GlobalInstrumentsRegistry::ForEach(
      [&, this](const GlobalInstrumentsRegistry::GlobalInstrumentDescriptor&
                    descriptor) {
        if (!metrics.contains(descriptor.name)) {
          return;
        }
        if (descriptor.instrument_type ==
            GlobalInstrumentsRegistry::InstrumentType::kCounter) {
          if (descriptor.value_type ==
              GlobalInstrumentsRegistry::ValueType::kUInt64) {
            uint64_counters_.emplace(
                descriptor.index,
                meter->CreateUInt64Counter(std::string(descriptor.name),
                                           std::string(descriptor.description),
                                           std::string(descriptor.unit)));
          } else {
            double_counters_.emplace(
                descriptor.index,
                meter->CreateDoubleCounter(std::string(descriptor.name),
                                           std::string(descriptor.description),
                                           std::string(descriptor.unit)));
          }
        } else {
          GPR_ASSERT(
              descriptor.instrument_type ==
              grpc_core::GlobalInstrumentsRegistry::InstrumentType::kHistogram);
          if (descriptor.value_type ==
              GlobalInstrumentsRegistry::ValueType::kUInt64) {
            uint64_histograms_.emplace(descriptor.index,
                                       meter->CreateUInt64Histogram(
                                           std::string(descriptor.name),
                                           std::string(descriptor.description),
                                           std::string(descriptor.unit)));
          } else {
            double_histograms_.emplace(descriptor.index,
                                       meter->CreateDoubleHistogram(
                                           std::string(descriptor.name),
                                           std::string(descriptor.description),
                                           std::string(descriptor.unit)));
          }
        }
      });
}

}  // namespace internal

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

OpenTelemetryPluginBuilder::~OpenTelemetryPluginBuilder() = default;

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

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::AddPluginOption(
    std::unique_ptr<OpenTelemetryPluginOption> option) {
  impl_->AddPluginOption(
      std::unique_ptr<grpc::internal::InternalOpenTelemetryPluginOption>(
          static_cast<grpc::internal::InternalOpenTelemetryPluginOption*>(
              option.release())));
  return *this;
}

absl::Status OpenTelemetryPluginBuilder::BuildAndRegisterGlobal() {
  return impl_->BuildAndRegisterGlobal();
}

}  // namespace grpc
