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

#include "absl/algorithm/container.h"

namespace grpc_core {

std::vector<GlobalInstrumentsRegistry::GlobalCounterDescriptor>
    GlobalInstrumentsRegistry::counters_;
std::vector<GlobalInstrumentsRegistry::GlobalHistogramDescriptor>
    GlobalInstrumentsRegistry::histograms_;

GlobalInstrumentsRegistry::GlobalUInt64CounterHandle
GlobalInstrumentsRegistry::RegisterUInt64Counter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  counters_.push_back({.type = Type::kUInt64,
                       .name = name,
                       .description = description,
                       .unit = unit,
                       .label_keys = std::move(label_keys),
                       .optional_label_keys = std::move(optional_label_keys)});
  GlobalUInt64CounterHandle handle;
  handle.index = counters_.size() - 1;
  return handle;
}

GlobalInstrumentsRegistry::GlobalDoubleCounterHandle
GlobalInstrumentsRegistry::RegisterDoubleCounter(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  counters_.push_back({.type = Type::kDouble,
                       .name = name,
                       .description = description,
                       .unit = unit,
                       .label_keys = std::move(label_keys),
                       .optional_label_keys = std::move(optional_label_keys)});
  GlobalDoubleCounterHandle handle;
  handle.index = counters_.size() - 1;
  return handle;
}

GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle
GlobalInstrumentsRegistry::RegisterUInt64Histogram(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  histograms_.push_back(
      {.type = Type::kUInt64,
       .name = name,
       .description = description,
       .unit = unit,
       .label_keys = std::move(label_keys),
       .optional_label_keys = std::move(optional_label_keys)});
  GlobalUInt64HistogramHandle handle;
  handle.index = histograms_.size() - 1;
  return handle;
}

GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle
GlobalInstrumentsRegistry::RegisterDoubleHistogram(
    absl::string_view name, absl::string_view description,
    absl::string_view unit, std::vector<absl::string_view> label_keys,
    std::vector<absl::string_view> optional_label_keys) {
  histograms_.push_back(
      {.type = Type::kDouble,
       .name = name,
       .description = description,
       .unit = unit,
       .label_keys = std::move(label_keys),
       .optional_label_keys = std::move(optional_label_keys)});
  GlobalDoubleHistogramHandle handle;
  handle.index = histograms_.size() - 1;
  return handle;
}

const GlobalInstrumentsRegistry::GlobalCounterDescriptor&
GlobalInstrumentsRegistry::GetCounterDescriptor(
    GlobalUInt64CounterHandle handle) {
  GPR_ASSERT(handle.index < counters_.size());
  GPR_ASSERT(counters_[handle.index].type == Type::kUInt64);
  return counters_[handle.index];
}

const GlobalInstrumentsRegistry::GlobalCounterDescriptor&
GlobalInstrumentsRegistry::GetCounterDescriptor(
    GlobalDoubleCounterHandle handle) {
  GPR_ASSERT(handle.index < counters_.size());
  GPR_ASSERT(counters_[handle.index].type == Type::kDouble);
  return counters_[handle.index];
}

const GlobalInstrumentsRegistry::GlobalHistogramDescriptor&
GlobalInstrumentsRegistry::GetHistogramDescriptor(
    GlobalUInt64HistogramHandle handle) {
  GPR_ASSERT(handle.index < histograms_.size());
  GPR_ASSERT(counters_[handle.index].type == Type::kUInt64);
  return histograms_[handle.index];
}

const GlobalInstrumentsRegistry::GlobalHistogramDescriptor&
GlobalInstrumentsRegistry::GetHistogramDescriptor(
    GlobalDoubleHistogramHandle handle) {
  GPR_ASSERT(handle.index < histograms_.size());
  GPR_ASSERT(counters_[handle.index].type == Type::kDouble);
  return histograms_[handle.index];
}

const std::vector<GlobalInstrumentsRegistry::GlobalCounterDescriptor>&
GlobalInstrumentsRegistry::counters() {
  return counters_;
}

const std::vector<GlobalInstrumentsRegistry::GlobalHistogramDescriptor>&
GlobalInstrumentsRegistry::histograms() {
  return histograms_;
}

std::atomic<GlobalStatsPluginRegistry*> GlobalStatsPluginRegistry::self_{
    nullptr};

void GlobalStatsPluginRegistry::RegisterStatsPlugin(
    std::shared_ptr<StatsPlugin> plugin) {
  MutexLock lock(&mutex_);
  plugins_.push_back(std::move(plugin));
}

GlobalStatsPluginRegistry::StatsPluginsGroup
GlobalStatsPluginRegistry::GetStatsPluginsForTarget(absl::string_view target) {
  MutexLock lock(&mutex_);
  StatsPluginsGroup group;
  absl::c_for_each(plugins_, [&group, target](const auto& plugin) {
    if (plugin->IsEnabledForTarget(target)) {
      group.push_back(plugin);
    }
  });
  return group;
}

}  // namespace grpc_core
