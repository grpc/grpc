// Copyright 2024 The gRPC Authors.
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

#ifndef GRPC_SRC_CORE_LIB_CHANNEL_METRICS_H
#define GRPC_SRC_CORE_LIB_CHANNEL_METRICS_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_core {

constexpr absl::string_view kMetricLabelTarget = "grpc.target";

// A global registry of instruments(metrics). This API is designed to be used
// to register instruments (Counter, Histogram, and Gauge) as part of program
// startup, before the execution of the main function (during dynamic
// initialization time). Using this API after the main function begins may
// result into missing instruments. This API is thread-unsafe.
class GlobalInstrumentsRegistry {
 public:
  enum class ValueType {
    kUndefined,
    kInt64,
    kUInt64,
    kDouble,
  };
  enum class InstrumentType {
    kUndefined,
    kCounter,
    kHistogram,
    kGauge,
    kCallbackGauge,
  };
  struct GlobalInstrumentDescriptor {
    ValueType value_type;
    InstrumentType instrument_type;
    uint32_t index;
    bool enable_by_default;
    absl::string_view name;
    absl::string_view description;
    absl::string_view unit;
    std::vector<absl::string_view> label_keys;
    std::vector<absl::string_view> optional_label_keys;
  };
  struct GlobalHandle {
    // This is the index for the corresponding registered instrument that
    // StatsPlugins can use to uniquely identify an instrument in the current
    // process. Though this is not guaranteed to be stable between different
    // runs or between different versions.
    uint32_t index;
  };
  struct GlobalUInt64CounterHandle : public GlobalHandle {};
  struct GlobalDoubleCounterHandle : public GlobalHandle {};
  struct GlobalUInt64HistogramHandle : public GlobalHandle {};
  struct GlobalDoubleHistogramHandle : public GlobalHandle {};
  struct GlobalInt64GaugeHandle : public GlobalHandle {};
  struct GlobalDoubleGaugeHandle : public GlobalHandle {};
  struct GlobalCallbackHandle : public GlobalHandle {};
  struct GlobalCallbackInt64GaugeHandle : public GlobalCallbackHandle {};
  struct GlobalCallbackDoubleGaugeHandle : public GlobalCallbackHandle {};

  // Creates instrument in the GlobalInstrumentsRegistry.
  static GlobalUInt64CounterHandle RegisterUInt64Counter(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);
  static GlobalDoubleCounterHandle RegisterDoubleCounter(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);
  static GlobalUInt64HistogramHandle RegisterUInt64Histogram(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);
  static GlobalDoubleHistogramHandle RegisterDoubleHistogram(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);
  static GlobalInt64GaugeHandle RegisterInt64Gauge(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);
  static GlobalDoubleGaugeHandle RegisterDoubleGauge(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);
  static GlobalCallbackInt64GaugeHandle RegisterCallbackInt64Gauge(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);
  static GlobalCallbackDoubleGaugeHandle RegisterCallbackDoubleGauge(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, absl::Span<const absl::string_view> label_keys,
      absl::Span<const absl::string_view> optional_label_keys,
      bool enable_by_default);

  static void ForEach(
      absl::FunctionRef<void(const GlobalInstrumentDescriptor&)> f);

 private:
  friend class GlobalInstrumentsRegistryTestPeer;

  GlobalInstrumentsRegistry() = delete;

  static absl::flat_hash_map<
      absl::string_view, GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>&
  GetInstrumentList();
};

// An interface for implementing callback-style metrics.
// To be implemented by stats plugins.
class CallbackMetricReporter {
 public:
  virtual ~CallbackMetricReporter() = default;

  virtual void Report(
      GlobalInstrumentsRegistry::GlobalCallbackInt64GaugeHandle handle,
      int64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;
  virtual void Report(
      GlobalInstrumentsRegistry::GlobalCallbackDoubleGaugeHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;
};

class RegisteredMetricCallback;

// The StatsPlugin interface.
class StatsPlugin {
 public:
  class ChannelScope {
   public:
    ChannelScope(absl::string_view target, absl::string_view authority)
        : target_(target), authority_(authority) {}

    absl::string_view target() const { return target_; }
    absl::string_view authority() const { return authority_; }

   private:
    absl::string_view target_;
    absl::string_view authority_;
  };

  virtual ~StatsPlugin() = default;

  virtual bool IsEnabledForChannel(const ChannelScope& scope) const = 0;
  virtual bool IsEnabledForServer(const ChannelArgs& args) const = 0;

  virtual void AddCounter(
      GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;
  virtual void AddCounter(
      GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle, double value,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;
  virtual void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;
  virtual void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;
  virtual void SetGauge(
      GlobalInstrumentsRegistry::GlobalInt64GaugeHandle handle, int64_t value,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;
  virtual void SetGauge(
      GlobalInstrumentsRegistry::GlobalDoubleGaugeHandle handle, double value,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_values) = 0;

  // Adds a callback to be invoked when the stats plugin wants to
  // populate the corresponding metrics (see callback->metrics() for list).
  virtual void AddCallback(RegisteredMetricCallback* callback) = 0;
  // Removes a callback previously added via AddCallback().  The stats
  // plugin may not use the callback after this method returns.
  virtual void RemoveCallback(RegisteredMetricCallback* callback) = 0;

  // TODO(yijiem): Details pending.
  // std::unique_ptr<AsyncInstrument> GetObservableGauge(
  //     absl::string_view name, absl::string_view description,
  //     absl::string_view unit);
  // AsyncInstrument* GetObservableCounter(
  //     absl::string_view name, absl::string_view description,
  //     absl::string_view unit);

  // TODO(yijiem): This is an optimization for the StatsPlugin to create its own
  // representation of the label_values and use it multiple times. We would
  // change AddCounter and RecordHistogram to take RefCountedPtr<LabelValueSet>
  // and also change the StatsPluginsGroup to support this.
  // Use the StatsPlugin to get a representation of label values that can be
  // saved for multiple uses later.
  // virtual RefCountedPtr<LabelValueSet> MakeLabelValueSet(
  //     absl::Span<absl::string_view> label_values) = 0;
};

// A global registry of StatsPlugins. It has shared ownership to the registered
// StatsPlugins. This API is supposed to be used during runtime after the main
// function begins. This API is thread-safe.
class GlobalStatsPluginRegistry {
 public:
  class StatsPluginGroup {
   public:
    void push_back(std::shared_ptr<StatsPlugin> plugin) {
      plugins_.push_back(std::move(plugin));
    }

    void AddCounter(GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
                    uint64_t value,
                    absl::Span<const absl::string_view> label_values,
                    absl::Span<const absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->AddCounter(handle, value, label_values, optional_values);
      }
    }
    void AddCounter(GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle,
                    double value,
                    absl::Span<const absl::string_view> label_values,
                    absl::Span<const absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->AddCounter(handle, value, label_values, optional_values);
      }
    }
    void RecordHistogram(
        GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
        uint64_t value, absl::Span<const absl::string_view> label_values,
        absl::Span<const absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->RecordHistogram(handle, value, label_values, optional_values);
      }
    }
    void RecordHistogram(
        GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
        double value, absl::Span<const absl::string_view> label_values,
        absl::Span<const absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->RecordHistogram(handle, value, label_values, optional_values);
      }
    }
    void SetGauge(GlobalInstrumentsRegistry::GlobalInt64GaugeHandle handle,
                  int64_t value,
                  absl::Span<const absl::string_view> label_values,
                  absl::Span<const absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->SetGauge(handle, value, label_values, optional_values);
      }
    }
    void SetGauge(GlobalInstrumentsRegistry::GlobalDoubleGaugeHandle handle,
                  double value,
                  absl::Span<const absl::string_view> label_values,
                  absl::Span<const absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->SetGauge(handle, value, label_values, optional_values);
      }
    }

    // Registers a callback to be used to populate callback metrics.
    // The callback will update the specified metrics.  The callback
    // will be invoked no more often than min_interval.
    //
    // The returned object is a handle that allows the caller to control
    // the lifetime of the callback; when the returned object is
    // destroyed, the callback is de-registered.  The returned object
    // must not outlive the StatsPluginGroup object that created it.
    std::unique_ptr<RegisteredMetricCallback> RegisterCallback(
        absl::AnyInvocable<void(CallbackMetricReporter&)> callback,
        std::vector<GlobalInstrumentsRegistry::GlobalCallbackHandle> metrics,
        Duration min_interval = Duration::Seconds(5));

   private:
    friend class RegisteredMetricCallback;

    std::vector<std::shared_ptr<StatsPlugin>> plugins_;
  };

  static void RegisterStatsPlugin(std::shared_ptr<StatsPlugin> plugin);

  // The following functions can be invoked to get a StatsPluginGroup for
  // a specified scope.
  static StatsPluginGroup GetAllStatsPlugins();
  static StatsPluginGroup GetStatsPluginsForChannel(
      const StatsPlugin::ChannelScope& scope);
  // TODO(yijiem): Implement this.
  // StatsPluginsGroup GetStatsPluginsForServer(ChannelArgs& args);

 private:
  friend class GlobalStatsPluginRegistryTestPeer;

  GlobalStatsPluginRegistry() = default;

  static NoDestruct<Mutex> mutex_;
  static NoDestruct<std::vector<std::shared_ptr<StatsPlugin>>> plugins_
      ABSL_GUARDED_BY(mutex_);
};

// A metric callback that is registered with a stats plugin group.
class RegisteredMetricCallback {
 public:
  RegisteredMetricCallback(
      GlobalStatsPluginRegistry::StatsPluginGroup& stats_plugin_group,
      absl::AnyInvocable<void(CallbackMetricReporter&)> callback,
      std::vector<GlobalInstrumentsRegistry::GlobalCallbackHandle> metrics,
      Duration min_interval);

  ~RegisteredMetricCallback();

  // Invokes the callback.  The callback will report metric data via reporter.
  void Run(CallbackMetricReporter& reporter) { callback_(reporter); }

  // Returns the set of metrics that this callback will modify.
  const std::vector<GlobalInstrumentsRegistry::GlobalCallbackHandle>& metrics()
      const {
    return metrics_;
  }

  // Returns the minimum interval at which a stats plugin may invoke the
  // callback.
  Duration min_interval() const { return min_interval_; }

 private:
  GlobalStatsPluginRegistry::StatsPluginGroup& stats_plugin_group_;
  absl::AnyInvocable<void(CallbackMetricReporter&)> callback_;
  std::vector<GlobalInstrumentsRegistry::GlobalCallbackHandle> metrics_;
  Duration min_interval_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_METRICS_H
