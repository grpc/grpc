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

}  // namespace grpc_core
