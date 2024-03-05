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

#include "test/core/util/fake_stats_plugin.h"

#include "src/core/lib/config/core_configuration.h"

namespace grpc_core {

class FakeStatsClientFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<FakeStatsClientFilter> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/);

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  explicit FakeStatsClientFilter(
      FakeClientCallTracerFactory* fake_client_call_tracer_factory);
  FakeClientCallTracerFactory* const fake_client_call_tracer_factory_;
};

const grpc_channel_filter FakeStatsClientFilter::kFilter =
    MakePromiseBasedFilter<FakeStatsClientFilter, FilterEndpoint::kClient>(
        "fake_stats_client");

absl::StatusOr<FakeStatsClientFilter> FakeStatsClientFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args /*filter_args*/) {
  auto* fake_client_call_tracer_factory =
      args.GetPointer<FakeClientCallTracerFactory>(
          GRPC_ARG_INJECT_FAKE_CLIENT_CALL_TRACER_FACTORY);
  GPR_ASSERT(fake_client_call_tracer_factory != nullptr);
  return FakeStatsClientFilter(fake_client_call_tracer_factory);
}

ArenaPromise<ServerMetadataHandle> FakeStatsClientFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  FakeClientCallTracer* client_call_tracer =
      fake_client_call_tracer_factory_->CreateFakeClientCallTracer();
  if (client_call_tracer != nullptr) {
    auto* call_context = GetContext<grpc_call_context_element>();
    call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value =
        client_call_tracer;
    call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].destroy =
        nullptr;
  }
  return next_promise_factory(std::move(call_args));
}

FakeStatsClientFilter::FakeStatsClientFilter(
    FakeClientCallTracerFactory* fake_client_call_tracer_factory)
    : fake_client_call_tracer_factory_(fake_client_call_tracer_factory) {}

void RegisterFakeStatsPlugin() {
  CoreConfiguration::RegisterBuilder(
      [](CoreConfiguration::Builder* builder) mutable {
        builder->channel_init()
            ->RegisterFilter(GRPC_CLIENT_CHANNEL,
                             &FakeStatsClientFilter::kFilter)
            .If([](const ChannelArgs& args) {
              return args.GetPointer<FakeClientCallTracerFactory>(
                         GRPC_ARG_INJECT_FAKE_CLIENT_CALL_TRACER_FACTORY) !=
                     nullptr;
            });
      });
}

namespace {

void AddKeyValuePairs(absl::Span<const absl::string_view> keys,
                      absl::Span<const absl::string_view> values,
                      std::vector<std::string>* key_value_pairs) {
  GPR_ASSERT(keys.size() == values.size());
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
          [target_suffix](const StatsPlugin::ChannelScope& scope) {
            return absl::EndsWith(scope.target(), target_suffix);
          })
      .BuildAndRegister();
}

void GlobalInstrumentsRegistryTestPeer::ResetGlobalInstrumentsRegistry() {
  auto& instruments = GlobalInstrumentsRegistry::GetInstrumentList();
  instruments.clear();
}

namespace {

template <typename HandleType>
absl::optional<HandleType> FindInstrument(
    const absl::flat_hash_map<
        absl::string_view,
        GlobalInstrumentsRegistry::GlobalInstrumentDescriptor>& instruments,
    absl::string_view name, GlobalInstrumentsRegistry::ValueType value_type,
    GlobalInstrumentsRegistry::InstrumentType instrument_type) {
  auto it = instruments.find(name);
  if (it != instruments.end() && it->second.value_type == value_type &&
      it->second.instrument_type == instrument_type) {
    HandleType handle;
    handle.index = it->second.index;
    return handle;
  }
  return absl::nullopt;
}

}  // namespace

absl::optional<GlobalInstrumentsRegistry::GlobalUInt64CounterHandle>
GlobalInstrumentsRegistryTestPeer::FindUInt64CounterHandleByName(
    absl::string_view name) {
  return FindInstrument<GlobalInstrumentsRegistry::GlobalUInt64CounterHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kUInt64,
      GlobalInstrumentsRegistry::InstrumentType::kCounter);
}

absl::optional<GlobalInstrumentsRegistry::GlobalDoubleCounterHandle>
GlobalInstrumentsRegistryTestPeer::FindDoubleCounterHandleByName(
    absl::string_view name) {
  return FindInstrument<GlobalInstrumentsRegistry::GlobalDoubleCounterHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kDouble,
      GlobalInstrumentsRegistry::InstrumentType::kCounter);
}

absl::optional<GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle>
GlobalInstrumentsRegistryTestPeer::FindUInt64HistogramHandleByName(
    absl::string_view name) {
  return FindInstrument<GlobalInstrumentsRegistry::GlobalUInt64HistogramHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kUInt64,
      GlobalInstrumentsRegistry::InstrumentType::kHistogram);
}

absl::optional<GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle>
GlobalInstrumentsRegistryTestPeer::FindDoubleHistogramHandleByName(
    absl::string_view name) {
  return FindInstrument<GlobalInstrumentsRegistry::GlobalDoubleHistogramHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kDouble,
      GlobalInstrumentsRegistry::InstrumentType::kHistogram);
}

absl::optional<GlobalInstrumentsRegistry::GlobalInt64GaugeHandle>
GlobalInstrumentsRegistryTestPeer::FindInt64GaugeHandleByName(
    absl::string_view name) {
  return FindInstrument<GlobalInstrumentsRegistry::GlobalInt64GaugeHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kInt64,
      GlobalInstrumentsRegistry::InstrumentType::kGauge);
}

absl::optional<GlobalInstrumentsRegistry::GlobalDoubleGaugeHandle>
GlobalInstrumentsRegistryTestPeer::FindDoubleGaugeHandleByName(
    absl::string_view name) {
  return FindInstrument<GlobalInstrumentsRegistry::GlobalDoubleGaugeHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kDouble,
      GlobalInstrumentsRegistry::InstrumentType::kGauge);
}

absl::optional<GlobalInstrumentsRegistry::GlobalCallbackInt64GaugeHandle>
GlobalInstrumentsRegistryTestPeer::FindCallbackInt64GaugeHandleByName(
    absl::string_view name) {
  return FindInstrument<
      GlobalInstrumentsRegistry::GlobalCallbackInt64GaugeHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kInt64,
      GlobalInstrumentsRegistry::InstrumentType::kCallbackGauge);
}

absl::optional<GlobalInstrumentsRegistry::GlobalCallbackDoubleGaugeHandle>
GlobalInstrumentsRegistryTestPeer::FindCallbackDoubleGaugeHandleByName(
    absl::string_view name) {
  return FindInstrument<
      GlobalInstrumentsRegistry::GlobalCallbackDoubleGaugeHandle>(
      GlobalInstrumentsRegistry::GetInstrumentList(), name,
      GlobalInstrumentsRegistry::ValueType::kDouble,
      GlobalInstrumentsRegistry::InstrumentType::kCallbackGauge);
}

GlobalInstrumentsRegistry::GlobalInstrumentDescriptor*
GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
    absl::string_view name) {
  auto& instruments = GlobalInstrumentsRegistry::GetInstrumentList();
  auto it = instruments.find(name);
  if (it != instruments.end()) return &it->second;
  return nullptr;
}

}  // namespace grpc_core
