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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/metrics.h"

namespace grpc_core {

GlobalInstrumentsRegistry::GlobalUInt64CounterHandle
GlobalInstrumentsRegistry::RegisterUInt64Counter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  auto& instruments = gInstruments();
  uint32_t index = instruments.size();
  instruments.push_back(
      {.value_type = ValueType::kUInt64,
       .instrument_type = InstrumentType::kCounter,
       .index = index,
       .name = name,
       .description = description,
       .unit = unit,
       .label_keys = std::move(label_keys),
       .optional_label_keys = std::move(optional_label_keys)});
  GlobalUInt64CounterHandle handle;
  handle.index = index;
  return handle;
}

GlobalInstrumentsRegistry::GlobalDoubleCounterHandle
GlobalInstrumentsRegistry::RegisterDoubleCounter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  auto& instruments = gInstruments();
  uint32_t index = instruments.size();
  instruments.push_back(
      {.value_type = ValueType::kDouble,
       .instrument_type = InstrumentType::kCounter,
       .index = index,
       .name = name,
       .description = description,
       .unit = unit,
       .label_keys = std::move(label_keys),
       .optional_label_keys = std::move(optional_label_keys)});
  GlobalDoubleCounterHandle handle;
  handle.index = index;
  return handle;
}

GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle
GlobalInstrumentsRegistry::RegisterUInt64Histogram(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  auto& instruments = gInstruments();
  uint32_t index = instruments.size();
  instruments.push_back(
      {.value_type = ValueType::kUInt64,
       .instrument_type = InstrumentType::kHistogram,
       .index = index,
       .name = name,
       .description = description,
       .unit = unit,
       .label_keys = std::move(label_keys),
       .optional_label_keys = std::move(optional_label_keys)});
  GlobalUInt64HistogramHandle handle;
  handle.index = index;
  return handle;
}

GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle
GlobalInstrumentsRegistry::RegisterDoubleHistogram(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  auto& instruments = gInstruments();
  uint32_t index = instruments.size();
  instruments.push_back(
      {.value_type = ValueType::kDouble,
       .instrument_type = InstrumentType::kHistogram,
       .index = index,
       .name = name,
       .description = description,
       .unit = unit,
       .label_keys = std::move(label_keys),
       .optional_label_keys = std::move(optional_label_keys)});
  GlobalDoubleHistogramHandle handle;
  handle.index = index;
  return handle;
}

void GlobalInstrumentsRegistry::ForEach(
    absl::AnyInvocable<void(const GlobalInstrumentDescriptor&)> f) {
  for (const auto& instrument : gInstruments()) {
    f(instrument);
  }
}

std::atomic<GlobalStatsPluginRegistry*> GlobalStatsPluginRegistry::self_{
    nullptr};

void GlobalStatsPluginRegistry::RegisterStatsPlugin(
    std::shared_ptr<StatsPlugin> plugin) {
  MutexLock lock(&mutex_);
  plugins_.push_back(std::move(plugin));
}

GlobalStatsPluginRegistry::StatsPluginGroup
GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
    const StatsPlugin::Scope& scope) {
  MutexLock lock(&mutex_);
  StatsPluginGroup group;
  for (const auto& plugin : plugins_) {
    if (plugin->IsEnabledForChannel(scope)) {
      group.push_back(plugin);
    }
  }
  return group;
}

}  // namespace grpc_core
