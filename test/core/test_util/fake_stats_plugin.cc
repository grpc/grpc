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

#include "absl/log/check.h"
#include "src/core/config/core_configuration.h"

namespace grpc_core {

class FakeStatsClientFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::string_view TypeName() { return "fake_stats_client"; }

  explicit FakeStatsClientFilter(
      FakeClientCallTracerFactory* fake_client_call_tracer_factory);

  static absl::StatusOr<std::unique_ptr<FakeStatsClientFilter>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/);

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  FakeClientCallTracerFactory* const fake_client_call_tracer_factory_;
};

const grpc_channel_filter FakeStatsClientFilter::kFilter =
    MakePromiseBasedFilter<FakeStatsClientFilter, FilterEndpoint::kClient>();

absl::StatusOr<std::unique_ptr<FakeStatsClientFilter>>
FakeStatsClientFilter::Create(const ChannelArgs& args,
                              ChannelFilter::Args /*filter_args*/) {
  auto* fake_client_call_tracer_factory =
      args.GetPointer<FakeClientCallTracerFactory>(
          GRPC_ARG_INJECT_FAKE_CLIENT_CALL_TRACER_FACTORY);
  CHECK_NE(fake_client_call_tracer_factory, nullptr);
  return std::make_unique<FakeStatsClientFilter>(
      fake_client_call_tracer_factory);
}

ArenaPromise<ServerMetadataHandle> FakeStatsClientFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  FakeClientCallTracer* client_call_tracer =
      fake_client_call_tracer_factory_->CreateFakeClientCallTracer();
  if (client_call_tracer != nullptr) {
    SetContext<CallTracerAnnotationInterface>(client_call_tracer);
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
  CHECK(keys.size() == values.size());
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

}  // namespace grpc_core
