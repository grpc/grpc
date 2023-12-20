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

static FakeClientCallTracer* g_fake_client_call_tracer = nullptr;

void InjectGlobalFakeClientCallTracer(
    FakeClientCallTracer* fake_client_call_tracer) {
  g_fake_client_call_tracer = fake_client_call_tracer;
}

const grpc_channel_filter FakeStatsClientFilter::kFilter =
    MakePromiseBasedFilter<FakeStatsClientFilter,
                           grpc_core::FilterEndpoint::kClient>(
        "fake_stats_client");

absl::StatusOr<FakeStatsClientFilter> FakeStatsClientFilter::Create(
    const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
  return FakeStatsClientFilter();
}

grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle>
FakeStatsClientFilter::MakeCallPromise(
    grpc_core::CallArgs call_args,
    grpc_core::NextPromiseFactory next_promise_factory) {
  GPR_ASSERT(g_fake_client_call_tracer != nullptr);
  auto* call_context = grpc_core::GetContext<grpc_call_context_element>();
  GPR_ASSERT(
      call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value ==
      nullptr);
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value =
      g_fake_client_call_tracer;
  call_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].destroy = nullptr;
  return next_promise_factory(std::move(call_args));
}

void RegisterFakeStatsPlugin() {
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) mutable {
        builder->channel_init()->RegisterFilter(
            GRPC_CLIENT_CHANNEL, &FakeStatsClientFilter::kFilter);
      });
}

}  // namespace grpc_core
