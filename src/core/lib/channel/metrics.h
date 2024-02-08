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

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {

// Static only.
class GlobalInstrumentsRegistry {
 public:
  enum class Type {
    kUndefined,
    kUInt64,
    kDouble,
  };
  struct GlobalCounterDescriptor {
    Type type;
    absl::string_view name;
    absl::string_view description;
    absl::string_view unit;
    std::vector<absl::string_view> label_keys;
    std::vector<absl::string_view> optional_label_keys;
  };
  struct GlobalHistogramDescriptor {
    Type type;
    absl::string_view name;
    absl::string_view description;
    absl::string_view unit;
    std::vector<absl::string_view> label_keys;
    std::vector<absl::string_view> optional_label_keys;
  };
  struct GlobalHandle {
    uint32_t index;
  };
  struct GlobalUInt64CounterHandle : GlobalHandle {};
  struct GlobalDoubleCounterHandle : GlobalHandle {};
  struct GlobalUInt64HistogramHandle : GlobalHandle {};
  struct GlobalDoubleHistogramHandle : GlobalHandle {};

  // These functions below will create a global instrument
  // The handle will most likely just be an index in the list of instruments.
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

  // Getter functions for stats plugins to query registered counters.
  static const GlobalCounterDescriptor& GetCounterDescriptor(
      GlobalUInt64CounterHandle handle);
  static const GlobalCounterDescriptor& GetCounterDescriptor(
      GlobalDoubleCounterHandle handle);
  static const GlobalHistogramDescriptor& GetHistogramDescriptor(
      GlobalUInt64HistogramHandle handle);
  static const GlobalHistogramDescriptor& GetHistogramDescriptor(
      GlobalDoubleHistogramHandle handle);
  static const std::vector<GlobalCounterDescriptor>& counters();
  static const std::vector<GlobalHistogramDescriptor>& histograms();

  GlobalInstrumentsRegistry() = delete;

 private:
  static std::vector<GlobalCounterDescriptor> counters_;
  static std::vector<GlobalHistogramDescriptor> histograms_;
};

// Interface.
class StatsPlugin {
 public:
  virtual ~StatsPlugin() = default;
  virtual bool IsEnabledForTarget(absl::string_view target) = 0;
  virtual bool IsEnabledForServer(grpc_core::ChannelArgs& args) = 0;

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
  // change the AddCounter and the RecordHistogram to take
  // RefCountedPtr<LabelValueSet> and also change the StatsPluginsGroup to
  // support this.
  // Use the stats plugin to get a representation of label values
  // that can be saved for multiple uses later. virtual
  // RefCountedPtr<LabelValueSet> MakeLabelValueSet(
  //     absl::Span<absl::string_view> label_values) = 0;
};

// Singleton.
class GlobalStatsPluginRegistry {
 public:
  class StatsPluginsGroup {
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
  // The following two functions can be invoked to get a stats plugins group for
  // a specified scope.
  StatsPluginsGroup GetStatsPluginsForTarget(absl::string_view target);
  // TODO(yijiem): Implement this.
  // StatsPluginsGroup GetStatsPluginsForServer(grpc_core::ChannelArgs& args);

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

  void TestOnlyResetStatsPlugins() { plugins_.clear(); }

 private:
  GlobalStatsPluginRegistry() = default;

  static std::atomic<GlobalStatsPluginRegistry*> self_;

  std::vector<std::shared_ptr<StatsPlugin>> plugins_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_METRICS_H
