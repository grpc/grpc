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

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

// A global instruments registry whose API is designed to be used to register
// instruments (Counter and Histogram) as part of program startup, before the
// execution of the main function. Using this API after the main function begins
// may result into those instruments missing in StatsPlugins.
class GlobalInstrumentsRegistry {
 public:
  enum class ValueType {
    kUndefined,
    kUInt64,
    kDouble,
  };
  enum class InstrumentType {
    kUndefined,
    kCounter,
    kHistogram,
  };
  struct GlobalInstrumentDescriptor {
    ValueType value_type;
    InstrumentType instrument_type;
    uint32_t index;
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

  // Creates instrument in the GlobalInstrumentsRegistry.
  static GlobalUInt64CounterHandle RegisterUInt64Counter(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, std::vector<absl::string_view> label_keys,
      std::vector<absl::string_view> optional_label_keys);
  static GlobalDoubleCounterHandle RegisterDoubleCounter(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, std::vector<absl::string_view> label_keys,
      std::vector<absl::string_view> optional_label_keys);
  static GlobalUInt64HistogramHandle RegisterUInt64Histogram(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, std::vector<absl::string_view> label_keys,
      std::vector<absl::string_view> optional_label_keys);
  static GlobalDoubleHistogramHandle RegisterDoubleHistogram(
      absl::string_view name, absl::string_view description,
      absl::string_view unit, std::vector<absl::string_view> label_keys,
      std::vector<absl::string_view> optional_label_keys);
  static void ForEach(
      absl::AnyInvocable<void(const GlobalInstrumentDescriptor&)> f);

  GlobalInstrumentsRegistry() = delete;

 private:
  // Uses the Construct-on-First-Use idiom to avoid the static initialization
  // order fiasco.
  static std::vector<GlobalInstrumentDescriptor>& gInstruments() {
    static std::vector<GlobalInstrumentDescriptor> instruments_;
    return instruments_;
  }
};

// The StatsPlugin interface.
class StatsPlugin {
 public:
  virtual ~StatsPlugin() = default;

  struct Scope {
    absl::string_view target;
    absl::string_view authority;
  };
  virtual bool IsEnabledForChannel(const Scope& scope) const = 0;
  virtual bool IsEnabledForServer(const ChannelArgs& args) const = 0;

  virtual void AddCounter(
      GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
      uint64_t value, std::vector<absl::string_view> label_values,
      std::vector<absl::string_view> optional_values) = 0;
  virtual void AddCounter(
      GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle, double value,
      std::vector<absl::string_view> label_values,
      std::vector<absl::string_view> optional_values) = 0;
  virtual void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
      uint64_t value, std::vector<absl::string_view> label_values,
      std::vector<absl::string_view> optional_values) = 0;
  virtual void RecordHistogram(
      GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
      double value, std::vector<absl::string_view> label_values,
      std::vector<absl::string_view> optional_values) = 0;
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

// Singleton.
class GlobalStatsPluginRegistry {
 public:
  class StatsPluginGroup {
   public:
    void push_back(std::shared_ptr<StatsPlugin> plugin) {
      plugins_.push_back(std::move(plugin));
    }

    void AddCounter(GlobalInstrumentsRegistry::GlobalUInt64CounterHandle handle,
                    uint64_t value, std::vector<absl::string_view> label_values,
                    std::vector<absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->AddCounter(handle, value, label_values, optional_values);
      }
    }
    void AddCounter(GlobalInstrumentsRegistry::GlobalDoubleCounterHandle handle,
                    double value, std::vector<absl::string_view> label_values,
                    std::vector<absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->AddCounter(handle, value, label_values, optional_values);
      }
    }
    void RecordHistogram(
        GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle handle,
        uint64_t value, std::vector<absl::string_view> label_values,
        std::vector<absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->RecordHistogram(handle, value, label_values, optional_values);
      }
    }
    void RecordHistogram(
        GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle handle,
        double value, std::vector<absl::string_view> label_values,
        std::vector<absl::string_view> optional_values) {
      for (auto& plugin : plugins_) {
        plugin->RecordHistogram(handle, value, label_values, optional_values);
      }
    }

   private:
    std::vector<std::shared_ptr<StatsPlugin>> plugins_;
  };

  void RegisterStatsPlugin(std::shared_ptr<StatsPlugin> plugin);
  // The following two functions can be invoked to get a StatsPluginGroup for
  // a specified scope.
  StatsPluginGroup GetStatsPluginsForChannel(const StatsPlugin::Scope& scope);
  // TODO(yijiem): Implement this.
  // StatsPluginsGroup GetStatsPluginsForServer(ChannelArgs& args);

  static GlobalStatsPluginRegistry& Get() {
    GlobalStatsPluginRegistry* p = self_.load(std::memory_order_acquire);
    if (p != nullptr) {
      return *p;
    }
    p = new GlobalStatsPluginRegistry();
    GlobalStatsPluginRegistry* expected = nullptr;
    if (!self_.compare_exchange_strong(expected, p, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
      delete p;
      return *expected;
    }
    return *p;
  }

  void TestOnlyResetStatsPlugins() {
    MutexLock lock(&mutex_);
    plugins_.clear();
  }

 private:
  GlobalStatsPluginRegistry() = default;

  static std::atomic<GlobalStatsPluginRegistry*> self_;

  Mutex mutex_;
  std::vector<std::shared_ptr<StatsPlugin>> plugins_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_METRICS_H
