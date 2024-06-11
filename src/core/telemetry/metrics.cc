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

#include "src/core/telemetry/metrics.h"

#include <memory>

#include "absl/log/check.h"
#include "absl/types/optional.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/crash.h"

namespace grpc_core {

// Uses the Construct-on-First-Use idiom to avoid the static initialization
// order fiasco.
std::vector<GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>&
GlobalInstrumentsRegistry::GetInstrumentList() {
  static NoDestruct<
      std::vector<GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>>
      instruments;
  return *instruments;
}

GlobalInstrumentsRegistry::InstrumentID
GlobalInstrumentsRegistry::RegisterInstrument(
    GlobalInstrumentsRegistry::ValueType value_type,
    GlobalInstrumentsRegistry::InstrumentType instrument_type,
    absl::string_view name, absl::string_view description,
    absl::string_view unit, bool enable_by_default,
    absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> optional_label_keys) {
  auto& instruments = GetInstrumentList();
  for (const auto& descriptor : instruments) {
    if (descriptor.name == name) {
      Crash(
          absl::StrFormat("Metric name %s has already been registered.", name));
    }
  }
  InstrumentID index = instruments.size();
  CHECK_LT(index, std::numeric_limits<uint32_t>::max());
  GlobalInstrumentDescriptor descriptor;
  descriptor.value_type = value_type;
  descriptor.instrument_type = instrument_type;
  descriptor.index = index;
  descriptor.enable_by_default = enable_by_default;
  descriptor.name = name;
  descriptor.description = description;
  descriptor.unit = unit;
  descriptor.label_keys = {label_keys.begin(), label_keys.end()};
  descriptor.optional_label_keys = {optional_label_keys.begin(),
                                    optional_label_keys.end()};
  instruments.push_back(std::move(descriptor));
  return index;
}

void GlobalInstrumentsRegistry::ForEach(
    absl::FunctionRef<void(const GlobalInstrumentDescriptor&)> f) {
  for (const auto& instrument : GetInstrumentList()) {
    f(instrument);
  }
}

const GlobalInstrumentsRegistry::GlobalInstrumentDescriptor&
GlobalInstrumentsRegistry::GetInstrumentDescriptor(
    GlobalInstrumentHandle handle) {
  return GetInstrumentList().at(handle.index);
}

RegisteredMetricCallback::RegisteredMetricCallback(
    GlobalStatsPluginRegistry::StatsPluginGroup& stats_plugin_group,
    absl::AnyInvocable<void(CallbackMetricReporter&)> callback,
    std::vector<GlobalInstrumentsRegistry::GlobalInstrumentHandle> metrics,
    Duration min_interval)
    : stats_plugin_group_(stats_plugin_group),
      callback_(std::move(callback)),
      metrics_(std::move(metrics)),
      min_interval_(min_interval) {
  for (auto& state : stats_plugin_group_.plugins_state_) {
    state.plugin->AddCallback(this);
  }
}

RegisteredMetricCallback::~RegisteredMetricCallback() {
  for (auto& state : stats_plugin_group_.plugins_state_) {
    state.plugin->RemoveCallback(this);
  }
}

void GlobalStatsPluginRegistry::StatsPluginGroup::AddClientCallTracers(
    const Slice& path, bool registered_method, Arena* arena) {
  for (auto& state : plugins_state_) {
    auto* call_tracer = state.plugin->GetClientCallTracer(
        path, registered_method, state.scope_config);
    if (call_tracer != nullptr) {
      AddClientCallTracerToContext(arena, call_tracer);
    }
  }
}

void GlobalStatsPluginRegistry::StatsPluginGroup::AddServerCallTracers(
    Arena* arena) {
  for (auto& state : plugins_state_) {
    auto* call_tracer = state.plugin->GetServerCallTracer(state.scope_config);
    if (call_tracer != nullptr) {
      AddServerCallTracerToContext(arena, call_tracer);
    }
  }
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
    const experimental::StatsPluginChannelScope& scope) {
  MutexLock lock(&*mutex_);
  StatsPluginGroup group;
  for (const auto& plugin : *plugins_) {
    bool is_enabled = false;
    std::shared_ptr<StatsPlugin::ScopeConfig> config;
    std::tie(is_enabled, config) = plugin->IsEnabledForChannel(scope);
    if (is_enabled) {
      group.AddStatsPlugin(plugin, std::move(config));
    }
  }
  return group;
}

GlobalStatsPluginRegistry::StatsPluginGroup
GlobalStatsPluginRegistry::GetStatsPluginsForServer(const ChannelArgs& args) {
  MutexLock lock(&*mutex_);
  StatsPluginGroup group;
  for (const auto& plugin : *plugins_) {
    bool is_enabled = false;
    std::shared_ptr<StatsPlugin::ScopeConfig> config;
    std::tie(is_enabled, config) = plugin->IsEnabledForServer(args);
    if (is_enabled) {
      group.AddStatsPlugin(plugin, std::move(config));
    }
  }
  return group;
}

absl::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle>
GlobalInstrumentsRegistry::FindInstrumentByName(absl::string_view name) {
  const auto& instruments = GetInstrumentList();
  for (const auto& descriptor : instruments) {
    if (descriptor.name == name) {
      GlobalInstrumentsRegistry::GlobalInstrumentHandle handle;
      handle.index = descriptor.index;
      return handle;
    }
  }
  return absl::nullopt;
}

}  // namespace grpc_core
