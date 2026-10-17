//
// Copyright 2023 gRPC authors.
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
//

#include "src/core/server/server_call_tracer_filter.h"

#include <grpc/server_call_hook.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace {
// Written once before serving, read on every server call.
std::atomic<const grpc_server_call_hook_vtable*> g_server_call_hook{nullptr};
}  // namespace

const grpc_server_call_hook_vtable* GetServerCallHook() {
  return g_server_call_hook.load(std::memory_order_relaxed);
}

const grpc_channel_filter ServerCallTracerFilter::kFilter =
    MakePromiseBasedFilter<ServerCallTracerFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata>();

absl::StatusOr<std::unique_ptr<ServerCallTracerFilter>>
ServerCallTracerFilter::Create(const ChannelArgs& /*args*/,
                               ChannelFilter::Args /*filter_args*/) {
  return std::make_unique<ServerCallTracerFilter>();
}

void RegisterServerCallTracerFilter(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterFilter<ServerCallTracerFilter>(
      GRPC_SERVER_CHANNEL);
}

}  // namespace grpc_core

void grpc_server_call_hook_register(
    const grpc_server_call_hook_vtable* vtable) {
  // Reject rather than half-read: a smaller struct was built against an older
  // header, so the fields we read would be out of bounds.
  if (vtable != nullptr &&
      vtable->struct_size < GRPC_SERVER_CALL_HOOK_MIN_SIZE) {
    LOG(ERROR) << "server call hook vtable is " << vtable->struct_size
               << " bytes, smaller than the " << GRPC_SERVER_CALL_HOOK_MIN_SIZE
               << " this build requires; ignoring it";
    return;
  }
  grpc_core::g_server_call_hook.store(vtable, std::memory_order_relaxed);
}
