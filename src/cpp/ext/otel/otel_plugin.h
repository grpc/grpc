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

#include <bitset>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "opentelemetry/metrics/async_instruments.h"
#include "opentelemetry/metrics/meter_provider.h"
#include "opentelemetry/metrics/observer_result.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "opentelemetry/nostd/shared_ptr.h"

#include <grpcpp/ext/otel_plugin.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/metrics.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc {
namespace internal {

// An iterable container interface that can be used as a return type for the
// OpenTelemetry plugin's label injector.
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
// through the OpenTelemetry plugin.
class LabelsInjector {
 public:
  virtual ~LabelsInjector() {}
  // Read the incoming initial metadata to get the set of labels to be added to
  // metrics.
  virtual std::unique_ptr<LabelsIterable> GetLabels(
      grpc_metadata_batch* incoming_initial_metadata) const = 0;

  // Modify the outgoing initial metadata with metadata information to be sent
  // to the peer. On the server side, \a labels_from_incoming_metadata returned
  // from `GetLabels` should be provided as input here. On the client side, this
  // should be nullptr.
  virtual void AddLabels(
      grpc_metadata_batch* outgoing_initial_metadata,
      LabelsIterable* labels_from_incoming_metadata) const = 0;

  // Adds optional labels to the traced calls. Each entry in the span
  // corresponds to the CallAttemptTracer::OptionalLabelComponent enum. Returns
  // false when callback returns false.
  virtual bool AddOptionalLabels(
      bool is_client,
      absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>
          optional_labels_span,
      opentelemetry::nostd::function_ref<
          bool(opentelemetry::nostd::string_view,
               opentelemetry::common::AttributeValue)>
          callback) const = 0;

  // Gets the actual size of the optional labels that the Plugin is going to
  // produce through the AddOptionalLabels method.
  virtual size_t GetOptionalLabelsSize(
      bool is_client,
      absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>
          optional_labels_span) const = 0;
};

class InternalOpenTelemetryPluginOption
    : public grpc::OpenTelemetryPluginOption {
 public:
  ~InternalOpenTelemetryPluginOption() override = default;
  // Determines whether a plugin option is active on a given channel target
  virtual bool IsActiveOnClientChannel(absl::string_view target) const = 0;
  // Determines whether a plugin option is active on a given server
  virtual bool IsActiveOnServer(const grpc_core::ChannelArgs& args) const = 0;
  // Returns the LabelsInjector used by this plugin option, nullptr if none.
  virtual const grpc::internal::LabelsInjector* labels_injector() const = 0;
};

// Tags
absl::string_view OpenTelemetryMethodKey();
absl::string_view OpenTelemetryStatusKey();
absl::string_view OpenTelemetryTargetKey();

class OpenTelemetryPluginBuilderImpl {
 public:
  OpenTelemetryPluginBuilderImpl();
  ~OpenTelemetryPluginBuilderImpl();
  // If `SetMeterProvider()` is not called, no metrics are collected.
  OpenTelemetryPluginBuilderImpl& SetMeterProvider(
      std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider);
  // Methods to manipulate which instruments are enabled in the OpenTelemetry
  // Stats Plugin. The default set of instruments are -
  // grpc.client.attempt.started
  // grpc.client.attempt.duration
  // grpc.client.attempt.sent_total_compressed_message_size
  // grpc.client.attempt.rcvd_total_compressed_message_size
  // grpc.server.call.started
  // grpc.server.call.duration
  // grpc.server.call.sent_total_compressed_message_size
  // grpc.server.call.rcvd_total_compressed_message_size
  OpenTelemetryPluginBuilderImpl& EnableMetric(absl::string_view metric_name);
  OpenTelemetryPluginBuilderImpl& DisableMetric(absl::string_view metric_name);
  OpenTelemetryPluginBuilderImpl& DisableAllMetrics();
  // If set, \a target_selector is called per channel to decide whether to
  // collect metrics on that target or not.
  OpenTelemetryPluginBuilderImpl& SetTargetSelector(
      absl::AnyInvocable<bool(absl::string_view /*target*/) const>
          target_selector);
  // If set, \a server_selector is called per incoming call on the server
  // to decide whether to collect metrics on that call or not.
  // TODO(yashkt): We should only need to do this per server connection or even
  // per server. Change this when we have a ServerTracer.
  OpenTelemetryPluginBuilderImpl& SetServerSelector(
      absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*args*/) const>
          server_selector);
  // If set, \a target_attribute_filter is called per channel to decide whether
  // to record the target attribute on client or to replace it with "other".
  // This helps reduce the cardinality on metrics in cases where many channels
  // are created with different targets in the same binary (which might happen
  // for example, if the channel target string uses IP addresses directly).
  OpenTelemetryPluginBuilderImpl& SetTargetAttributeFilter(
      absl::AnyInvocable<bool(absl::string_view /*target*/) const>
          target_attribute_filter);
  // If set, \a generic_method_attribute_filter is called per call with a
  // generic method type to decide whether to record the method name or to
  // replace it with "other". Non-generic or pre-registered methods remain
  // unaffected. If not set, by default, generic method names are replaced with
  // "other" when recording metrics.
  OpenTelemetryPluginBuilderImpl& SetGenericMethodAttributeFilter(
      absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
          generic_method_attribute_filter);
  OpenTelemetryPluginBuilderImpl& AddPluginOption(
      std::unique_ptr<InternalOpenTelemetryPluginOption> option);
  // Records \a optional_label_key on all metrics that provide it.
  OpenTelemetryPluginBuilderImpl& AddOptionalLabel(
      absl::string_view optional_label_key);
  absl::Status BuildAndRegisterGlobal();

 private:
  std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider_;
  std::unique_ptr<LabelsInjector> labels_injector_;
  absl::AnyInvocable<bool(absl::string_view /*target*/) const>
      target_attribute_filter_;
  absl::flat_hash_set<std::string> metrics_;
  absl::AnyInvocable<bool(absl::string_view /*target*/) const> target_selector_;
  absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
      generic_method_attribute_filter_;
  absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*args*/) const>
      server_selector_;
  std::vector<std::unique_ptr<InternalOpenTelemetryPluginOption>>
      plugin_options_;
  std::shared_ptr<std::set<absl::string_view>> optional_label_keys_;
};

class OpenTelemetryCallTracer;

class OpenTelemetryPlugin : public grpc_core::StatsPlugin {
 public:
  struct Client {
    struct Attempt {
      std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> started;
      std::unique_ptr<opentelemetry::metrics::Histogram<double>> duration;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          sent_total_compressed_message_size;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          rcvd_total_compressed_message_size;
    } attempt;
  };
  struct Server {
    struct Call {
      std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> started;
      std::unique_ptr<opentelemetry::metrics::Histogram<double>> duration;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          sent_total_compressed_message_size;
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>
          rcvd_total_compressed_message_size;
    } call;
  };

  OpenTelemetryPlugin(
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
      std::shared_ptr<std::set<absl::string_view>> optional_label_keys);

  // StatsPlugin:
  bool IsEnabledForChannel(const ChannelScope& scope) const override;
  bool IsEnabledForServer(const grpc_core::ChannelArgs& args) const override;
  void AddCounter(
      grpc_core::GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override;
  void AddCounter(
      grpc_core::GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override;
  void RecordHistogram(
      grpc_core::GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override;
  void RecordHistogram(
      grpc_core::GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override;
  void SetGauge(
      grpc_core::GlobalInstrumentsRegistry::GlobalInt64GaugeHandle handle,
      int64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override;
  void SetGauge(
      grpc_core::GlobalInstrumentsRegistry::GlobalDoubleGaugeHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) override;
  void AddCallback(grpc_core::RegisteredMetricCallback* callback) override;
  void RemoveCallback(grpc_core::RegisteredMetricCallback* callback) override;
  grpc_core::ClientCallTracer* GetClientCallTracer(
      absl::string_view canonical_target, grpc_core::Slice path,
      grpc_core::Arena* arena, bool registered_method) override;
  grpc_core::ServerCallTracerFactory* GetServerCallTracerFactory(
      grpc_core::Arena* arena) override;

  const absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*args*/) const>&
  server_selector() const {
    return server_selector_;
  }
  const absl::AnyInvocable<bool(absl::string_view /*target*/) const>&
  target_attribute_filter() const {
    return target_attribute_filter_;
  }
  const absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>&
  generic_method_attribute_filter() const {
    return generic_method_attribute_filter_;
  }
  const std::vector<std::unique_ptr<InternalOpenTelemetryPluginOption>>&
  plugin_options() const {
    return plugin_options_;
  }
  Client& client() { return client_; }
  Server& server() { return server_; }

 private:
  using LabelKeys = std::vector<absl::string_view>;
  using OptionalLabelKeys = std::vector<absl::string_view>;

  struct RegisteredMetricCallbackState {
    grpc_core::Timestamp last_update_time;
  };

  template <typename ValueType>
  struct ObservableState
      : public grpc_core::RefCounted<ObservableState<ValueType>> {
    grpc_core::GlobalInstrumentsRegistry::UID id;
    // Caches.
    ValueType value;
    std::unique_ptr<std::vector<absl::string_view>> label_values;
    std::unique_ptr<std::vector<absl::string_view>> optional_label_values;
    opentelemetry::nostd::shared_ptr<
        opentelemetry::metrics::ObservableInstrument>
        instrument;
    bool callback_registered = false;
    // Views.
    std::shared_ptr<
        absl::flat_hash_map<grpc_core::GlobalInstrumentsRegistry::UID,
                            std::pair<LabelKeys, OptionalLabelKeys>>>
        label_keys_map;
    std::shared_ptr<std::set<absl::string_view>> optional_label_keys;
    // We only support one Observable to one RegisteredMetricCallback and one
    // RegisteredMetricCallback to multiple Observables relationship for now.
    grpc_core::RegisteredMetricCallback* registered_metric_callback = nullptr;
    absl::Mutex* mu;
    std::shared_ptr<absl::flat_hash_map<grpc_core::RegisteredMetricCallback*,
                                        RegisteredMetricCallbackState>>
        registered_metric_callback_state_map;
  };

  template <typename T>
  static void ObservableCallback(opentelemetry::metrics::ObserverResult result,
                                 void* arg);

  // Instruments for per-call metrics.
  Client client_;
  Server server_;
  // Instruments for non-per-call metrics.
  absl::flat_hash_map<
      grpc_core::GlobalInstrumentsRegistry::UID,
      std::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>>
      uint64_counters_;
  absl::flat_hash_map<grpc_core::GlobalInstrumentsRegistry::UID,
                      std::unique_ptr<opentelemetry::metrics::Counter<double>>>
      double_counters_;
  absl::flat_hash_map<
      grpc_core::GlobalInstrumentsRegistry::UID,
      std::unique_ptr<opentelemetry::metrics::Histogram<uint64_t>>>
      uint64_histograms_;
  absl::flat_hash_map<
      grpc_core::GlobalInstrumentsRegistry::UID,
      std::unique_ptr<opentelemetry::metrics::Histogram<double>>>
      double_histograms_;
  absl::Mutex mu_;
  absl::flat_hash_map<grpc_core::GlobalInstrumentsRegistry::UID,
                      grpc_core::RefCountedPtr<ObservableState<int64_t>>>
      int64_observable_instruments_;
  absl::flat_hash_map<grpc_core::GlobalInstrumentsRegistry::UID,
                      grpc_core::RefCountedPtr<ObservableState<double>>>
      double_observable_instruments_;
  using ObservableStatePtr =
      absl::variant<grpc_core::RefCountedPtr<ObservableState<int64_t>>,
                    grpc_core::RefCountedPtr<ObservableState<double>>>;
  std::shared_ptr<absl::flat_hash_map<grpc_core::RegisteredMetricCallback*,
                                      RegisteredMetricCallbackState>>
      registered_metric_callback_state_map_;
  opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>
      meter_provider_;
  absl::AnyInvocable<bool(absl::string_view /*target*/) const> target_selector_;
  absl::AnyInvocable<bool(const grpc_core::ChannelArgs& /*args*/) const>
      server_selector_;
  absl::AnyInvocable<bool(absl::string_view /*target*/) const>
      target_attribute_filter_;
  absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
      generic_method_attribute_filter_;
  std::vector<std::unique_ptr<InternalOpenTelemetryPluginOption>>
      plugin_options_;
  // Stores label keys and optional label keys for each instrument at
  // construction time.
  std::shared_ptr<absl::flat_hash_map<grpc_core::GlobalInstrumentsRegistry::UID,
                                      std::pair<LabelKeys, OptionalLabelKeys>>>
      label_keys_map_;
  std::shared_ptr<std::set<absl::string_view>> optional_label_keys_;
};

// Creates a convenience wrapper to help iterate over only those plugin options
// that are active over a given channel/server.
class ActivePluginOptionsView {
 public:
  static ActivePluginOptionsView MakeForClient(
      absl::string_view target, OpenTelemetryPlugin* otel_plugin) {
    return ActivePluginOptionsView(
        [target](const InternalOpenTelemetryPluginOption& plugin_option) {
          return plugin_option.IsActiveOnClientChannel(target);
        },
        otel_plugin);
  }

  static ActivePluginOptionsView MakeForServer(
      const grpc_core::ChannelArgs& args, OpenTelemetryPlugin* otel_plugin) {
    return ActivePluginOptionsView(
        [&args](const InternalOpenTelemetryPluginOption& plugin_option) {
          return plugin_option.IsActiveOnServer(args);
        },
        otel_plugin);
  }

  bool ForEach(
      absl::FunctionRef<bool(const InternalOpenTelemetryPluginOption&, size_t)>
          func) const {
    for (size_t i = 0; i < otel_plugin_->plugin_options().size(); ++i) {
      const auto& plugin_option = otel_plugin_->plugin_options()[i];
      if (active_mask_[i] && !func(*plugin_option, i)) {
        return false;
      }
    }
    return true;
  }

 private:
  explicit ActivePluginOptionsView(
      absl::FunctionRef<bool(const InternalOpenTelemetryPluginOption&)> func,
      OpenTelemetryPlugin* otel_plugin)
      : otel_plugin_(otel_plugin) {
    for (size_t i = 0; i < otel_plugin_->plugin_options().size(); ++i) {
      const auto& plugin_option = otel_plugin_->plugin_options()[i];
      if (plugin_option != nullptr && func(*plugin_option)) {
        active_mask_.set(i);
      }
    }
  }

  OpenTelemetryPlugin* otel_plugin_;
  std::bitset<64> active_mask_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_PLUGIN_H
