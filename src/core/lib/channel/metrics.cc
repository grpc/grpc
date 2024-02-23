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

#include "absl/container/flat_hash_map.h"

#include "src/core/lib/gprpp/crash.h"

namespace grpc_core {
namespace {
// Uses the Construct-on-First-Use idiom to avoid the static initialization
// order fiasco.
absl::flat_hash_map<absl::string_view,
                    GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>&
GetInstrumentList() {
  static NoDestruct<absl::flat_hash_map<
      absl::string_view, GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>>
      instruments;
  return *instruments;
}
}  // namespace

GlobalInstrumentsRegistry::GlobalUInt64CounterHandle
GlobalInstrumentsRegistry::RegisterUInt64Counter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> optional_label_keys,
    bool enable_by_default) {
  auto& instruments = GetInstrumentList();
  if (instruments.find(name) != instruments.end()) {
    Crash(absl::StrFormat("Metric name %s has already been registered.", name));
  }
  uint32_t index = instruments.size();
  GPR_ASSERT(index < std::numeric_limits<uint32_t>::max());
  GlobalInstrumentDescriptor descriptor;
  descriptor.value_type = ValueType::kUInt64;
  descriptor.instrument_type = InstrumentType::kCounter;
  descriptor.index = index;
  descriptor.enable_by_default = enable_by_default;
  descriptor.name = name;
  descriptor.description = description;
  descriptor.unit = unit;
  descriptor.label_keys = {label_keys.begin(), label_keys.end()};
  descriptor.optional_label_keys = {optional_label_keys.begin(),
                                    optional_label_keys.end()};
  instruments.emplace(name, std::move(descriptor));
  GlobalUInt64CounterHandle handle;
  handle.index = index;
  return handle;
}

GlobalInstrumentsRegistry::GlobalDoubleCounterHandle
GlobalInstrumentsRegistry::RegisterDoubleCounter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> optional_label_keys,
    bool enable_by_default) {
  auto& instruments = GetInstrumentList();
  if (instruments.find(name) != instruments.end()) {
    Crash(absl::StrFormat("Metric name %s has already been registered.", name));
  }
  uint32_t index = instruments.size();
  GPR_ASSERT(index < std::numeric_limits<uint32_t>::max());
  GlobalInstrumentDescriptor descriptor;
  descriptor.value_type = ValueType::kDouble;
  descriptor.instrument_type = InstrumentType::kCounter;
  descriptor.index = index;
  descriptor.enable_by_default = enable_by_default;
  descriptor.name = name;
  descriptor.description = description;
  descriptor.unit = unit;
  descriptor.label_keys = {label_keys.begin(), label_keys.end()};
  descriptor.optional_label_keys = {optional_label_keys.begin(),
                                    optional_label_keys.end()};
  instruments.emplace(name, std::move(descriptor));
  GlobalDoubleCounterHandle handle;
  handle.index = index;
  return handle;
}

GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle
GlobalInstrumentsRegistry::RegisterUInt64Histogram(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> optional_label_keys,
    bool enable_by_default) {
  auto& instruments = GetInstrumentList();
  if (instruments.find(name) != instruments.end()) {
    Crash(absl::StrFormat("Metric name %s has already been registered.", name));
  }
  uint32_t index = instruments.size();
  GPR_ASSERT(index < std::numeric_limits<uint32_t>::max());
  GlobalInstrumentDescriptor descriptor;
  descriptor.value_type = ValueType::kUInt64;
  descriptor.instrument_type = InstrumentType::kHistogram;
  descriptor.index = index;
  descriptor.enable_by_default = enable_by_default;
  descriptor.name = name;
  descriptor.description = description;
  descriptor.unit = unit;
  descriptor.label_keys = {label_keys.begin(), label_keys.end()};
  descriptor.optional_label_keys = {optional_label_keys.begin(),
                                    optional_label_keys.end()};
  instruments.emplace(name, std::move(descriptor));
  GlobalUInt64HistogramHandle handle;
  handle.index = index;
  return handle;
}

GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle
GlobalInstrumentsRegistry::RegisterDoubleHistogram(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> optional_label_keys,
    bool enable_by_default) {
  auto& instruments = GetInstrumentList();
  if (instruments.find(name) != instruments.end()) {
    Crash(absl::StrFormat("Metric name %s has already been registered.", name));
  }
  uint32_t index = instruments.size();
  GPR_ASSERT(index < std::numeric_limits<uint32_t>::max());
  GlobalInstrumentDescriptor descriptor;
  descriptor.value_type = ValueType::kDouble;
  descriptor.instrument_type = InstrumentType::kHistogram;
  descriptor.index = index;
  descriptor.enable_by_default = enable_by_default;
  descriptor.name = name;
  descriptor.description = description;
  descriptor.unit = unit;
  descriptor.label_keys = {label_keys.begin(), label_keys.end()};
  descriptor.optional_label_keys = {optional_label_keys.begin(),
                                    optional_label_keys.end()};
  instruments.emplace(name, std::move(descriptor));
  GlobalDoubleHistogramHandle handle;
  handle.index = index;
  return handle;
}

void GlobalInstrumentsRegistry::ForEach(
    absl::FunctionRef<void(const GlobalInstrumentDescriptor&)> f) {
  for (const auto& instrument : GetInstrumentList()) {
    f(instrument.second);
  }
}

void GlobalInstrumentsRegistry::TestOnlyResetGlobalInstrumentsRegistry() {
  auto& instruments = GetInstrumentList();
  instruments.clear();
}

NoDestruct<Mutex> GlobalStatsPluginRegistry::mutex_;
NoDestruct<std::vector<std::shared_ptr<StatsPlugin>>>
    GlobalStatsPluginRegistry::plugins_;

void GlobalStatsPluginRegistry::RegisterStatsPlugin(
    std::shared_ptr<StatsPlugin> plugin) {
  MutexLock lock(&*mutex_);
  plugins_->push_back(std::move(plugin));
}

GlobalStatsPluginRegistry::StatsPluginGroup
GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
    const StatsPlugin::ChannelScope& scope) {
  MutexLock lock(&*mutex_);
  StatsPluginGroup group;
  for (const auto& plugin : *plugins_) {
    if (plugin->IsEnabledForChannel(scope)) {
      group.push_back(plugin);
    }
  }
  return group;
}

}  // namespace grpc_core
