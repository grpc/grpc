// Copyright 2023 The gRPC Authors.
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

#include "test/core/test_util/fake_stats_plugin.h"

#include "src/core/config/core_configuration.h"
#include "src/core/util/grpc_check.h"

namespace grpc_core {

namespace {

void AddKeyValuePairs(absl::Span<const absl::string_view> keys,
                      absl::Span<const absl::string_view> values,
                      std::vector<std::string>* key_value_pairs) {
  GRPC_CHECK(keys.size() == values.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    key_value_pairs->push_back(absl::StrCat(keys[i], "=", values[i]));
  }
}

}  // namespace

std::string MakeLabelString(
    absl::Span<const absl::string_view> label_keys,
    absl::Span<const absl::string_view> label_values,
    absl::Span<const absl::string_view> optional_label_keys,
    absl::Span<const absl::string_view> optional_values) {
  std::vector<std::string> key_value_pairs;
  AddKeyValuePairs(label_keys, label_values, &key_value_pairs);
  AddKeyValuePairs(optional_label_keys, optional_values, &key_value_pairs);
  return absl::StrJoin(key_value_pairs, ",");
}

std::shared_ptr<FakeStatsPlugin> MakeStatsPluginForTarget(
    absl::string_view target_suffix) {
  return FakeStatsPluginBuilder()
      .SetChannelFilter(
          [target_suffix](const experimental::StatsPluginChannelScope& scope) {
            return absl::EndsWith(scope.target(), target_suffix);
          })
      .BuildAndRegister();
}

void GlobalInstrumentsRegistryTestPeer::ResetGlobalInstrumentsRegistry() {
  auto& instruments = GlobalInstrumentsRegistry::GetInstrumentList();
  instruments.clear();
}

namespace {

std::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle> FindInstrument(
    const std::vector<GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>&
        instruments,
    absl::string_view name, GlobalInstrumentsRegistry::ValueType value_type,
    GlobalInstrumentsRegistry::InstrumentType instrument_type) {
  for (const auto& descriptor : instruments) {
    if (descriptor.name == name && descriptor.value_type == value_type &&
        descriptor.instrument_type == instrument_type) {
      GlobalInstrumentsRegistry::GlobalInstrumentHandle handle;
      handle.index = descriptor.index;
      return handle;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle>
GlobalInstrumentsRegistryTestPeer::FindUInt64CounterHandleByName(
    absl::string_view name) {
  return FindInstrument(GlobalInstrumentsRegistry::GetInstrumentList(), name,
                        GlobalInstrumentsRegistry::ValueType::kUInt64,
                        GlobalInstrumentsRegistry::InstrumentType::kCounter);
}

std::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle>
GlobalInstrumentsRegistryTestPeer::FindDoubleCounterHandleByName(
    absl::string_view name) {
  return FindInstrument(GlobalInstrumentsRegistry::GetInstrumentList(), name,
                        GlobalInstrumentsRegistry::ValueType::kDouble,
                        GlobalInstrumentsRegistry::InstrumentType::kCounter);
}

std::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle>
GlobalInstrumentsRegistryTestPeer::FindUInt64HistogramHandleByName(
    absl::string_view name) {
  return FindInstrument(GlobalInstrumentsRegistry::GetInstrumentList(), name,
                        GlobalInstrumentsRegistry::ValueType::kUInt64,
                        GlobalInstrumentsRegistry::InstrumentType::kHistogram);
}

std::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle>
GlobalInstrumentsRegistryTestPeer::FindDoubleHistogramHandleByName(
    absl::string_view name) {
  return FindInstrument(GlobalInstrumentsRegistry::GetInstrumentList(), name,
                        GlobalInstrumentsRegistry::ValueType::kDouble,
                        GlobalInstrumentsRegistry::InstrumentType::kHistogram);
}

std::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle>
GlobalInstrumentsRegistryTestPeer::FindCallbackInt64GaugeHandleByName(
    absl::string_view name) {
  return FindInstrument(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kInt64,
      GlobalInstrumentsRegistry::InstrumentType::kCallbackGauge);
}

std::optional<GlobalInstrumentsRegistry::GlobalInstrumentHandle>
GlobalInstrumentsRegistryTestPeer::FindCallbackDoubleGaugeHandleByName(
    absl::string_view name) {
  return FindInstrument(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kDouble,
      GlobalInstrumentsRegistry::InstrumentType::kCallbackGauge);
}

GlobalInstrumentsRegistry::GlobalInstrumentDescriptor*
GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
    absl::string_view name) {
  for (auto& descriptor : GlobalInstrumentsRegistry::GetInstrumentList()) {
    if (descriptor.name == name) {
      return &descriptor;
    }
  }
  return nullptr;
}

template <typename T>
std::optional<T> FakeStatsPlugin::GetMetricValueByNameImpl(
    absl::string_view name, absl::Span<const absl::string_view> labels) {
  const auto* desc = instrument_detail::InstrumentIndex::Get().Find(name);
  if (desc == nullptr) return std::nullopt;
  const auto& names = desc->domain->label_names();
  std::vector<std::string> keys;
  keys.reserve(names.size());
  for (const auto& label : names) {
    keys.emplace_back(label.label());
  }
  std::vector<std::string> values(labels.begin(), labels.end());
  DomainMetricsSink<T> sink(name, keys, values);
  MetricsQuery()
      .OnlyMetrics({std::string(name)})
      .Run(GetCollectionScope(), sink);
  return sink.captured_value();
}

std::optional<uint64_t> FakeStatsPlugin::GetUInt64MetricValueByName(
    absl::string_view name, absl::Span<const absl::string_view> labels) {
  return GetMetricValueByNameImpl<uint64_t>(name, labels);
}

std::optional<int64_t> FakeStatsPlugin::GetInt64MetricValueByName(
    absl::string_view name, absl::Span<const absl::string_view> labels) {
  return GetMetricValueByNameImpl<int64_t>(name, labels);
}

std::optional<std::vector<uint64_t>> FakeStatsPlugin::GetHistogramValueByName(
    absl::string_view name, absl::Span<const absl::string_view> labels) {
  return GetMetricValueByNameImpl<std::vector<uint64_t>>(name, labels);
}

std::optional<std::vector<uint64_t>>
FakeStatsPlugin::GetDoubleHistogramValueByName(
    absl::string_view name, absl::Span<const absl::string_view> labels) {
  return GetMetricValueByNameImpl<std::vector<uint64_t>>(name, labels);
}

}  // namespace grpc_core
