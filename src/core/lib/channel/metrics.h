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

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#include <grpc/support/log.h>
#include <grpc/support/metrics.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"

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
    kCallbackGauge,
  };
  using InstrumentID = uint32_t;
  struct GlobalInstrumentDescriptor {
    ValueType value_type;
    InstrumentType instrument_type;
    InstrumentID index;
    bool enable_by_default;
    absl::string_view name;
    absl::string_view description;
    absl::string_view unit;
    std::vector<absl::string_view> label_keys;
    std::vector<absl::string_view> optional_label_keys;
  };
  struct GlobalInstrumentHandle {
    // This is the index for the corresponding registered instrument that
    // StatsPlugins can use to uniquely identify an instrument in the current
    // process. Though this is not guaranteed to be stable between different
    // runs or between different versions.
    InstrumentID index;
  };
  struct GlobalUInt64CounterHandle : public GlobalInstrumentHandle {};
  struct GlobalDoubleCounterHandle : public GlobalInstrumentHandle {};
  struct GlobalUInt64HistogramHandle : public GlobalInstrumentHandle {};
  struct GlobalDoubleHistogramHandle : public GlobalInstrumentHandle {};
  struct GlobalCallbackInt64GaugeHandle : public GlobalInstrumentHandle {};
  struct GlobalCallbackDoubleGaugeHandle : public GlobalInstrumentHandle {};
  using GlobalCallbackHandle = absl::variant<GlobalCallbackInt64GaugeHandle,
                                             GlobalCallbackDoubleGaugeHandle>;

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
  static const GlobalInstrumentDescriptor& GetInstrumentDescriptor(
      GlobalInstrumentHandle handle);

 private:
  friend class GlobalInstrumentsRegistryTestPeer;

  GlobalInstrumentsRegistry() = delete;

  static std::vector<GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>&
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
  // A general-purpose way for stats plugin to store per-channel or per-server
  // state.
  class ScopeConfig {
   public:
    virtual ~ScopeConfig() = default;
  };

  virtual ~StatsPlugin() = default;

  // Whether this stats plugin is enabled for the channel specified by \a scope.
  // Returns true and a channel-specific ScopeConfig which may then be used to
  // configure the ClientCallTracer in GetClientCallTracer().
  virtual std::pair<bool, std::shared_ptr<ScopeConfig>> IsEnabledForChannel(
      const experimental::StatsPluginChannelScope& scope) const = 0;
  // Whether this stats plugin is enabled for the server specified by \a args.
  // Returns true and a server-specific ScopeConfig which may then be used to
  // configure the ServerCallTracer in GetServerCallTracer().
  virtual std::pair<bool, std::shared_ptr<ScopeConfig>> IsEnabledForServer(
      const ChannelArgs& args) const = 0;

  // Adds \a value to the uint64 counter specified by \a handle. \a label_values
  // and \a optional_label_values specify attributes that are associated with
  // this measurement and must match with their corresponding keys in
  // GlobalInstrumentsRegistry::RegisterUInt64Counter().
  virtual void AddCounter(
      GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_label_values) = 0;
  // Adds \a value to the double counter specified by \a handle. \a label_values
  // and \a optional_label_values specify attributes that are associated with
  // this measurement and must match with their corresponding keys in
  // GlobalInstrumentsRegistry::RegisterDoubleCounter().
  virtual void AddCounter(
      GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle, double value,
      absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_label_values) = 0;
  // Records a uint64 \a value to the histogram specified by \a handle. \a
  // label_values and \a optional_label_values specify attributes that are
  // associated with this measurement and must match with their corresponding
  // keys in GlobalInstrumentsRegistry::RegisterUInt64Histogram().
  virtual void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
      uint64_t value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_label_values) = 0;
  // Records a double \a value to the histogram specified by \a handle. \a
  // label_values and \a optional_label_values specify attributes that are
  // associated with this measurement and must match with their corresponding
  // keys in GlobalInstrumentsRegistry::RegisterDoubleHistogram().
  virtual void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
      double value, absl::Span<const absl::string_view> label_values,
      absl::Span<const absl::string_view> optional_label_values) = 0;
  // Adds a callback to be invoked when the stats plugin wants to
  // populate the corresponding metrics (see callback->metrics() for list).
  virtual void AddCallback(RegisteredMetricCallback* callback) = 0;
  // Removes a callback previously added via AddCallback().  The stats
  // plugin may not use the callback after this method returns.
  virtual void RemoveCallback(RegisteredMetricCallback* callback) = 0;

  // Gets a ClientCallTracer associated with this stats plugin which can be used
  // in a call.
  virtual ClientCallTracer* GetClientCallTracer(
      const Slice& path, bool registered_method,
      std::shared_ptr<ScopeConfig> scope_config) = 0;
  // Gets a ServerCallTracer associated with this stats plugin which can be used
  // in a call.
  virtual ServerCallTracer* GetServerCallTracer(
      std::shared_ptr<ScopeConfig> scope_config) = 0;

  // TODO(yijiem): This is an optimization for the StatsPlugin to create its own
  // representation of the label_values and use it multiple times. We would
  // change AddCounter and RecordHistogram to take RefCountedPtr<LabelValueSet>
  // and also change the StatsPluginsGroup to support this.
  // Use the StatsPlugin to get a representation of label values that can be
  // saved for multiple uses later.
  // virtual RefCountedPtr<LabelValueSet> MakeLabelValueSet(
  //     absl::Span<absl::string_view> label_values) = 0;
};

// A global registry of stats plugins. It has shared ownership to the registered
// stats plugins. This API is supposed to be used during runtime after the main
// function begins. This API is thread-safe.
class GlobalStatsPluginRegistry {
 public:
  // A stats plugin group object is how the code in gRPC normally interacts with
  // stats plugins. They got a stats plugin group which contains all the stats
  // plugins for a specific scope and all operations on the stats plugin group
  // will be applied to all the stats plugins within the group.
  class StatsPluginGroup {
   public:
    // Adds a stats plugin and a scope config (per-channel or per-server) to the
    // group.
    void AddStatsPlugin(std::shared_ptr<StatsPlugin> plugin,
                        std::shared_ptr<StatsPlugin::ScopeConfig> config) {
      PluginState plugin_state;
      plugin_state.plugin = std::move(plugin);
      plugin_state.scope_config = std::move(config);
      plugins_state_.push_back(std::move(plugin_state));
    }
    // Adds a counter in all stats plugins within the group. See the StatsPlugin
    // interface for more documentation and valid types.
    template <class HandleType, class ValueType>
    void AddCounter(HandleType handle, ValueType value,
                    absl::Span<const absl::string_view> label_values,
                    absl::Span<const absl::string_view> optional_values) {
      for (auto& state : plugins_state_) {
        state.plugin->AddCounter(handle, value, label_values, optional_values);
      }
    }
    // Records a value to a histogram in all stats plugins within the group. See
    // the StatsPlugin interface for more documentation and valid types.
    template <class HandleType, class ValueType>
    void RecordHistogram(HandleType handle, ValueType value,
                         absl::Span<const absl::string_view> label_values,
                         absl::Span<const absl::string_view> optional_values) {
      for (auto& state : plugins_state_) {
        state.plugin->RecordHistogram(handle, value, label_values,
                                      optional_values);
      }
    }

    // Registers a callback to be used to populate callback metrics.
    // The callback will update the specified metrics.  The callback
    // will be invoked no more often than min_interval.  Multiple callbacks may
    // be registered for the same metrics, as long as no two callbacks report
    // data for the same set of labels in which case the behavior is undefined.
    //
    // The returned object is a handle that allows the caller to control
    // the lifetime of the callback; when the returned object is
    // destroyed, the callback is de-registered.  The returned object
    // must not outlive the StatsPluginGroup object that created it.
    GRPC_MUST_USE_RESULT std::unique_ptr<RegisteredMetricCallback>
    RegisterCallback(
        absl::AnyInvocable<void(CallbackMetricReporter&)> callback,
        std::vector<GlobalInstrumentsRegistry::GlobalCallbackHandle> metrics,
        Duration min_interval = Duration::Seconds(5));

    // Adds all available client call tracers associated with the stats plugins
    // within the group to \a call_context.
    void AddClientCallTracers(const Slice& path, bool registered_method,
                              grpc_call_context_element* call_context);
    // Adds all available server call tracers associated with the stats plugins
    // within the group to \a call_context.
    void AddServerCallTracers(grpc_call_context_element* call_context);

   private:
    friend class RegisteredMetricCallback;

    struct PluginState {
      std::shared_ptr<StatsPlugin::ScopeConfig> scope_config;
      std::shared_ptr<StatsPlugin> plugin;
    };

    std::vector<PluginState> plugins_state_;
  };

  // Registers a stats plugin with the global stats plugin registry.
  static void RegisterStatsPlugin(std::shared_ptr<StatsPlugin> plugin);

  // The following functions can be invoked to get a StatsPluginGroup for
  // a specified scope.
  static StatsPluginGroup GetStatsPluginsForChannel(
      const experimental::StatsPluginChannelScope& scope);
  static StatsPluginGroup GetStatsPluginsForServer(const ChannelArgs& args);

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
