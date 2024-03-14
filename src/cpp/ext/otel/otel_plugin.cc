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
#include "opentelemetry/nostd/variant.h"

#include <grpc/support/log.h>
#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/version_info.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/cpp/ext/otel/key_value_iterable.h"
#include "src/cpp/ext/otel/otel_call_tracer.h"
#include "src/cpp/ext/otel/otel_server_call_tracer.h"

namespace grpc {
namespace internal {

absl::string_view OpenTelemetryMethodKey() { return "grpc.method"; }

absl::string_view OpenTelemetryStatusKey() { return "grpc.status"; }

absl::string_view OpenTelemetryTargetKey() { return "grpc.target"; }

namespace {
absl::flat_hash_set<std::string> BaseMetrics() {
  absl::flat_hash_set<std::string> base_metrics{
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
  grpc_core::GlobalInstrumentsRegistry::ForEach(
      [&](const grpc_core::GlobalInstrumentsRegistry::
              GlobalInstrumentDescriptor& descriptor) {
        if (descriptor.enable_by_default) {
          base_metrics.emplace(descriptor.name);
        }
      });
  return base_metrics;
}

class NPCMetricsKeyValueIterable
    : public opentelemetry::common::KeyValueIterable {
 public:
  NPCMetricsKeyValueIterable(
      absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_label_keys,
      absl::Span<const absl::string_view> optional_label_values,
      std::shared_ptr<std::set<absl::string_view>> enabled_optional_label_keys)
      : label_keys_(label_keys),
        label_values_(label_values),
        optional_label_keys_(optional_label_keys),
        optional_label_values_(optional_label_values),
        enabled_optional_label_keys_(std::move(enabled_optional_label_keys)) {}

  bool ForEachKeyValue(opentelemetry::nostd::function_ref<
                       bool(opentelemetry::nostd::string_view,
                            opentelemetry::common::AttributeValue)>
                           callback) const noexcept override {
    for (int i = 0; i < label_keys_.size(); i++) {
      if (!callback(AbslStrViewToOpenTelemetryStrView(label_keys_[i]),
                    AbslStrViewToOpenTelemetryStrView(label_values_[i]))) {
        return false;
      }
    }
    if (enabled_optional_label_keys_ == nullptr) {
      return true;
    }
    // Note that if there is duplicated enabled keys we will send them multiple
    // times.
    for (int i = 0; i < optional_label_keys_.size(); i++) {
      if (enabled_optional_label_keys_->find(optional_label_keys_[i]) ==
          enabled_optional_label_keys_->end()) {
        continue;
      }
      if (!callback(
              AbslStrViewToOpenTelemetryStrView(optional_label_keys_[i]),
              AbslStrViewToOpenTelemetryStrView(optional_label_values_[i]))) {
        return false;
      }
    }
    return true;
  }

  size_t size() const noexcept override {
    return label_keys_.size() + enabled_optional_label_keys_->size();
  }

 private:
  absl::Span<const absl::string_view> label_keys_;
  absl::Span<const absl::string_view> label_values_;
  absl::Span<const absl::string_view> optional_label_keys_;
  absl::Span<const absl::string_view> optional_label_values_;
  std::shared_ptr<std::set<absl::string_view>> enabled_optional_label_keys_;
};
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

OpenTelemetryPluginBuilderImpl&
OpenTelemetryPluginBuilderImpl::AddOptionalLabel(
    absl::string_view optional_label_key) {
  if (optional_label_keys_ == nullptr) {
    optional_label_keys_ = std::make_shared<std::set<absl::string_view>>();
  }
  optional_label_keys_->emplace(optional_label_key);
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
          std::move(server_selector_), std::move(plugin_options_),
          std::move(optional_label_keys_)));
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
        plugin_options,
    std::shared_ptr<std::set<absl::string_view>> optional_label_keys)
    : registered_metric_callback_state_map_(
          std::make_shared<
              absl::flat_hash_map<grpc_core::RegisteredMetricCallback*,
                                  RegisteredMetricCallbackState>>()),
      meter_provider_(std::move(meter_provider)),
      target_selector_(std::move(target_selector)),
      server_selector_(std::move(server_selector)),
      target_attribute_filter_(std::move(target_attribute_filter)),
      generic_method_attribute_filter_(
          std::move(generic_method_attribute_filter)),
      plugin_options_(std::move(plugin_options)),
      label_keys_map_(
          std::make_shared<
              absl::flat_hash_map<grpc_core::GlobalInstrumentsRegistry::UID,
                                  std::pair<LabelKeys, OptionalLabelKeys>>>()),
      optional_label_keys_(std::move(optional_label_keys)) {
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
      [&, this](const grpc_core::GlobalInstrumentsRegistry::
                    GlobalInstrumentDescriptor& descriptor) {
        if (!metrics.contains(descriptor.name)) {
          return;
        }
        label_keys_map_->emplace(
            std::piecewise_construct, std::forward_as_tuple(descriptor.index),
            std::forward_as_tuple(descriptor.label_keys,
                                  descriptor.optional_label_keys));
        switch (descriptor.instrument_type) {
          case grpc_core::GlobalInstrumentsRegistry::InstrumentType::kCounter:
            switch (descriptor.value_type) {
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kUInt64:
                uint64_counters_.emplace(
                    descriptor.index, meter->CreateUInt64Counter(
                                          std::string(descriptor.name),
                                          std::string(descriptor.description),
                                          std::string(descriptor.unit)));
                break;
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kDouble:
                double_counters_.emplace(
                    descriptor.index, meter->CreateDoubleCounter(
                                          std::string(descriptor.name),
                                          std::string(descriptor.description),
                                          std::string(descriptor.unit)));
                break;
              default:
                grpc_core::Crash(
                    absl::StrFormat("Unknown or unsupported value type: %d",
                                    descriptor.value_type));
            }
            break;
          case grpc_core::GlobalInstrumentsRegistry::InstrumentType::kHistogram:
            switch (descriptor.value_type) {
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kUInt64:
                uint64_histograms_.emplace(
                    descriptor.index, meter->CreateUInt64Histogram(
                                          std::string(descriptor.name),
                                          std::string(descriptor.description),
                                          std::string(descriptor.unit)));
                break;
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kDouble:
                double_histograms_.emplace(
                    descriptor.index, meter->CreateDoubleHistogram(
                                          std::string(descriptor.name),
                                          std::string(descriptor.description),
                                          std::string(descriptor.unit)));
                break;
              default:
                grpc_core::Crash(
                    absl::StrFormat("Unknown or unsupported value type: %d",
                                    descriptor.value_type));
            }
            break;
          case grpc_core::GlobalInstrumentsRegistry::InstrumentType::kGauge:
            switch (descriptor.value_type) {
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kInt64: {
                auto observable_state =
                    std::make_unique<ObservableState<int64_t>>();
                observable_state->id = descriptor.index;
                observable_state->instrument =
                    meter->CreateInt64ObservableGauge(
                        std::string(descriptor.name),
                        std::string(descriptor.description),
                        std::string(descriptor.unit));
                observable_state->label_keys_map = label_keys_map_;
                observable_state->optional_label_keys = optional_label_keys_;
                int64_observable_instruments_.emplace(
                    descriptor.index, std::move(observable_state));
                break;
              }
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kDouble: {
                auto observable_state =
                    std::make_unique<ObservableState<double>>();
                observable_state->id = descriptor.index;
                observable_state->instrument =
                    meter->CreateDoubleObservableGauge(
                        std::string(descriptor.name),
                        std::string(descriptor.description),
                        std::string(descriptor.unit));
                observable_state->label_keys_map = label_keys_map_;
                observable_state->optional_label_keys = optional_label_keys_;
                double_observable_instruments_.emplace(
                    descriptor.index, std::move(observable_state));
                break;
              }
              default:
                grpc_core::Crash(
                    absl::StrFormat("Unknown or unsupported value type: %d",
                                    descriptor.value_type));
            }
            break;
          case grpc_core::GlobalInstrumentsRegistry::InstrumentType::
              kCallbackGauge:
            switch (descriptor.value_type) {
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kInt64: {
                auto observable_state =
                    std::make_unique<ObservableState<int64_t>>();
                observable_state->id = descriptor.index;
                observable_state->instrument =
                    meter->CreateInt64ObservableGauge(
                        std::string(descriptor.name),
                        std::string(descriptor.description),
                        std::string(descriptor.unit));
                observable_state->label_keys_map = label_keys_map_;
                observable_state->optional_label_keys = optional_label_keys_;
                observable_state->registered_metric_callback_state_map =
                    registered_metric_callback_state_map_;
                int64_observable_instruments_.emplace(
                    descriptor.index, std::move(observable_state));
                break;
              }
              case grpc_core::GlobalInstrumentsRegistry::ValueType::kDouble: {
                auto observable_state =
                    std::make_unique<ObservableState<double>>();
                observable_state->id = descriptor.index;
                observable_state->instrument =
                    meter->CreateDoubleObservableGauge(
                        std::string(descriptor.name),
                        std::string(descriptor.description),
                        std::string(descriptor.unit));
                observable_state->label_keys_map = label_keys_map_;
                observable_state->optional_label_keys = optional_label_keys_;
                observable_state->registered_metric_callback_state_map =
                    registered_metric_callback_state_map_;
                double_observable_instruments_.emplace(
                    descriptor.index, std::move(observable_state));
                break;
              }
              default:
                grpc_core::Crash(
                    absl::StrFormat("Unknown or unsupported value type: %d",
                                    descriptor.value_type));
            }
            break;
          default:
            grpc_core::Crash(absl::StrFormat("Unknown instrument_type: %d",
                                             descriptor.instrument_type));
        }
      });
}

bool OpenTelemetryPlugin::IsEnabledForChannel(const ChannelScope& scope) const {
  if (target_selector_ == nullptr) {
    return true;
  }
  return target_selector_(scope.target());
}
bool OpenTelemetryPlugin::IsEnabledForServer(
    const grpc_core::ChannelArgs& args) const {
  if (server_selector_ == nullptr) {
    return true;
  }
  return server_selector_(args);
}
void OpenTelemetryPlugin::AddCounter(
    grpc_core::GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
    uint64_t value, absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_values) {
  auto instrument = uint64_counters_.find(handle.index);
  auto label_keys = label_keys_map_->find(handle.index);
  if (instrument == uint64_counters_.end() &&
      label_keys == label_keys_map_->end()) {
    // This instrument is disabled.
    return;
  }
  GPR_ASSERT(instrument != uint64_counters_.end() &&
             "instrument != uint64_counters_.end()");
  GPR_ASSERT(label_keys != label_keys_map_->end() &&
             "label_keys != label_keys_map_.end()");
  GPR_ASSERT(label_keys->second.first.size() == label_values.size() &&
             "label_keys->second.first.size() == label_values.size()");
  GPR_ASSERT(label_keys->second.second.size() == optional_values.size() &&
             "label_keys->second.second.size() == optional_values.size()");
  instrument->second->Add(
      value, NPCMetricsKeyValueIterable(label_keys->second.first, label_values,
                                        label_keys->second.second,
                                        optional_values, optional_label_keys_));
}
void OpenTelemetryPlugin::AddCounter(
    grpc_core::GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle,
    double value, absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_values) {
  auto instrument = double_counters_.find(handle.index);
  auto label_keys = label_keys_map_->find(handle.index);
  if (instrument == double_counters_.end() &&
      label_keys == label_keys_map_->end()) {
    // This instrument is disabled.
    return;
  }
  GPR_ASSERT(instrument != double_counters_.end() &&
             "instrument != double_counters_.end()");
  GPR_ASSERT(label_keys != label_keys_map_->end() &&
             "label_keys != label_keys_map_.end()");
  GPR_ASSERT(label_keys->second.first.size() == label_values.size() &&
             "label_keys->second.first.size() == label_values.size()");
  GPR_ASSERT(label_keys->second.second.size() == optional_values.size() &&
             "label_keys->second.second.size() == optional_values.size()");
  instrument->second->Add(
      value, NPCMetricsKeyValueIterable(label_keys->second.first, label_values,
                                        label_keys->second.second,
                                        optional_values, optional_label_keys_));
}
void OpenTelemetryPlugin::RecordHistogram(
    grpc_core::GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
    uint64_t value, absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_values) {
  auto instrument = uint64_histograms_.find(handle.index);
  auto label_keys = label_keys_map_->find(handle.index);
  if (instrument == uint64_histograms_.end() &&
      label_keys == label_keys_map_->end()) {
    // This instrument is disabled.
    return;
  }
  GPR_ASSERT(instrument != uint64_histograms_.end() &&
             "instrument != uint64_histograms_.end()");
  GPR_ASSERT(label_keys != label_keys_map_->end() &&
             "label_keys != label_keys_map_.end()");
  GPR_ASSERT(label_keys->second.first.size() == label_values.size() &&
             "label_keys->second.first.size() == label_values.size()");
  GPR_ASSERT(label_keys->second.second.size() == optional_values.size() &&
             "label_keys->second.second.size() == optional_values.size()");
  instrument->second->Record(
      value,
      NPCMetricsKeyValueIterable(label_keys->second.first, label_values,
                                 label_keys->second.second, optional_values,
                                 optional_label_keys_),
      opentelemetry::context::Context{});
}
void OpenTelemetryPlugin::RecordHistogram(
    grpc_core::GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
    double value, absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_values) {
  auto instrument = double_histograms_.find(handle.index);
  auto label_keys = label_keys_map_->find(handle.index);
  if (instrument == double_histograms_.end() &&
      label_keys == label_keys_map_->end()) {
    // This instrument is disabled.
    return;
  }
  GPR_ASSERT(instrument != double_histograms_.end() &&
             "instrument != double_histograms_.end()");
  GPR_ASSERT(label_keys != label_keys_map_->end() &&
             "label_keys != label_keys_map_.end()");
  GPR_ASSERT(label_keys->second.first.size() == label_values.size() &&
             "label_keys->second.first.size() == label_values.size()");
  GPR_ASSERT(label_keys->second.second.size() == optional_values.size() &&
             "label_keys->second.second.size() == optional_values.size()");
  instrument->second->Record(
      value,
      NPCMetricsKeyValueIterable(label_keys->second.first, label_values,
                                 label_keys->second.second, optional_values,
                                 optional_label_keys_),
      opentelemetry::context::Context{});
}
void OpenTelemetryPlugin::SetGauge(
    grpc_core::GlobalInstrumentsRegistry::GlobalInt64GaugeHandle handle,
    int64_t value, absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_values) {
  auto instrument = int64_observable_instruments_.find(handle.index);
  auto label_keys = label_keys_map_->find(handle.index);
  if (instrument == int64_observable_instruments_.end() &&
      label_keys == label_keys_map_->end()) {
    // This instrument is disabled.
    return;
  }
  GPR_ASSERT(instrument != int64_observable_instruments_.end() &&
             "instrument != int64_observable_instruments_.end()");
  GPR_ASSERT(label_keys != label_keys_map_->end() &&
             "label_keys != label_keys_map_.end()");
  GPR_ASSERT(label_keys->second.first.size() == label_values.size() &&
             "label_keys->second.first.size() == label_values.size()");
  GPR_ASSERT(label_keys->second.second.size() == optional_values.size() &&
             "label_keys->second.second.size() == optional_values.size()");
  absl::MutexLock l{&instrument->second->mu};
  instrument->second->value = value;
  instrument->second->label_values =
      std::make_unique<std::vector<absl::string_view>>(label_values.begin(),
                                                       label_values.end());
  instrument->second->optional_label_values =
      std::make_unique<std::vector<absl::string_view>>(optional_values.begin(),
                                                       optional_values.end());
  if (!std::exchange(instrument->second->callback_registered, true)) {
    instrument->second->instrument->AddCallback(&ObservableCallback<int64_t>,
                                                instrument->second.get());
  }
}
void OpenTelemetryPlugin::SetGauge(
    grpc_core::GlobalInstrumentsRegistry::GlobalDoubleGaugeHandle handle,
    double value, absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_values) {
  auto instrument = double_observable_instruments_.find(handle.index);
  auto label_keys = label_keys_map_->find(handle.index);
  if (instrument == double_observable_instruments_.end() &&
      label_keys == label_keys_map_->end()) {
    // This instrument is disabled.
    return;
  }
  GPR_ASSERT(instrument != double_observable_instruments_.end() &&
             "instrument != double_observable_instruments_.end()");
  GPR_ASSERT(label_keys != label_keys_map_->end() &&
             "label_keys != label_keys_map_.end()");
  GPR_ASSERT(label_keys->second.first.size() == label_values.size() &&
             "label_keys->second.first.size() == label_values.size()");
  GPR_ASSERT(label_keys->second.second.size() == optional_values.size() &&
             "label_keys->second.second.size() == optional_values.size()");
  absl::MutexLock l{&instrument->second->mu};
  instrument->second->value = value;
  instrument->second->label_values =
      std::make_unique<std::vector<absl::string_view>>(label_values.begin(),
                                                       label_values.end());
  instrument->second->optional_label_values =
      std::make_unique<std::vector<absl::string_view>>(optional_values.begin(),
                                                       optional_values.end());
  if (!std::exchange(instrument->second->callback_registered, true)) {
    instrument->second->instrument->AddCallback(&ObservableCallback<double>,
                                                instrument->second.get());
  }
}
// TODO(yijiem): implement this.
void OpenTelemetryPlugin::AddCallback(
    grpc_core::RegisteredMetricCallback* callback) {
  for (const auto& handle : callback->metrics()) {
    grpc_core::Match(
        handle,
        [&, this](const grpc_core::GlobalInstrumentsRegistry::
                      GlobalCallbackInt64GaugeHandle& handle) {
          auto instrument = int64_observable_instruments_.find(handle.index);
          auto label_keys = label_keys_map_->find(handle.index);
          if (instrument == int64_observable_instruments_.end() &&
              label_keys == label_keys_map_->end()) {
            // This instrument is disabled.
            return;
          }
          GPR_ASSERT(instrument != int64_observable_instruments_.end() &&
                     "instrument != int64_observable_instruments_.end()");
          GPR_ASSERT(label_keys != label_keys_map_->end() &&
                     "label_keys != label_keys_map_.end()");
          GPR_ASSERT(
              !std::exchange(instrument->second->callback_registered, true));
          instrument->second->registered_metric_callback = callback;
          instrument->second->instrument->AddCallback(
              &ObservableCallback<int64_t>, instrument->second.get());
        },
        [&, this](const grpc_core::GlobalInstrumentsRegistry::
                      GlobalCallbackDoubleGaugeHandle& handle) {
          auto instrument = double_observable_instruments_.find(handle.index);
          auto label_keys = label_keys_map_->find(handle.index);
          if (instrument == double_observable_instruments_.end() &&
              label_keys == label_keys_map_->end()) {
            // This instrument is disabled.
            return;
          }
          GPR_ASSERT(instrument != double_observable_instruments_.end() &&
                     "instrument != double_observable_instruments_.end()");
          GPR_ASSERT(label_keys != label_keys_map_->end() &&
                     "label_keys != label_keys_map_.end()");
          GPR_ASSERT(
              !std::exchange(instrument->second->callback_registered, true));
          instrument->second->registered_metric_callback = callback;
          instrument->second->instrument->AddCallback(
              &ObservableCallback<int64_t>, instrument->second.get());
        });
  }
}
void OpenTelemetryPlugin::RemoveCallback(
    grpc_core::RegisteredMetricCallback* callback) {}

grpc_core::ClientCallTracer* OpenTelemetryPlugin::GetClientCallTracer(
    absl::string_view canonical_target, grpc_core::Slice path,
    grpc_core::Arena* arena, bool registered_method) {
  return arena->ManagedNew<OpenTelemetryCallTracer>(
      canonical_target, std::move(path), arena, registered_method, this);
}
grpc_core::ServerCallTracerFactory*
OpenTelemetryPlugin::GetServerCallTracerFactory(grpc_core::Arena* arena) {
  return arena
      ->ManagedNew<grpc::internal::OpenTelemetryServerCallTracerFactory>(this);
}

template <typename T>
void OpenTelemetryPlugin::ObservableCallback(
    opentelemetry::metrics::ObserverResult result, void* arg) {
  auto* state = static_cast<ObservableState<T>*>(arg);
  if (state->registered_metric_callback == nullptr) {
    absl::MutexLock l{&state->mu};
    auto label_keys = state->label_keys_map->find(state->id);
    GPR_ASSERT(label_keys != state->label_keys_map->end() &&
               "label_keys != label_keys_map_.end()");
    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
        opentelemetry::metrics::ObserverResultT<T>>>(result)
        ->Observe(state->value,
                  NPCMetricsKeyValueIterable(
                      label_keys->second.first, *state->label_values,
                      label_keys->second.second, *state->optional_label_values,
                      state->optional_label_keys));
  }
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

OpenTelemetryPluginBuilder& OpenTelemetryPluginBuilder::AddOptionalLabel(
    absl::string_view optional_label_key) {
  impl_->AddOptionalLabel(optional_label_key);
  return *this;
}

absl::Status OpenTelemetryPluginBuilder::BuildAndRegisterGlobal() {
  return impl_->BuildAndRegisterGlobal();
}

}  // namespace grpc
