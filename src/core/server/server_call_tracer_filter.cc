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
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

// Written before serving, read on every server call.
std::atomic<const grpc_server_call_hook*> g_server_call_hook{nullptr};

// The `call` handed to on_call_start.
struct HookCall {
  ClientMetadata* metadata;
  ServerMetadataHandle refusal;
};

int HookGet(void* call, const char* key, size_t key_len, const char** value,
            size_t* value_len) {
  std::string joined;
  std::optional<absl::string_view> found =
      static_cast<HookCall*>(call)->metadata->GetStringValue(
          absl::string_view(key, key_len), &joined);
  if (!found.has_value()) return 0;
  *value = found->empty() ? "" : found->data();
  if (!joined.empty()) {
    // The joined value lives only in this lookup, so copy it to the arena.
    char* copy = static_cast<char*>(GetContext<Arena>()->Alloc(joined.size()));
    memcpy(copy, joined.data(), joined.size());
    *value = copy;
  }
  *value_len = found->size();
  return 1;
}

bool AddableKey(absl::string_view key) {
  if (key.empty()) return false;
  for (char c : key) {
    if (!absl::ascii_islower(c) && !absl::ascii_isdigit(c) && c != '-' &&
        c != '_' && c != '.') {
      return false;
    }
  }
  return key != "grpc-status" && key != "grpc-message" &&
         key != "content-type" && key != "te";
}

int HookAdd(void* call, const char* key, size_t key_len, const char* value,
            size_t value_len) {
  absl::string_view k(key, key_len);
  absl::string_view v(value, value_len);
  if (!AddableKey(k)) return 0;
  if (!absl::EndsWith(k, "-bin")) {
    for (char c : v) {
      if (c < 0x20 || c > 0x7e) return 0;
    }
  }
  auto* c = static_cast<HookCall*>(call);
  if (c->refusal == nullptr) c->refusal = Arena::MakePooled<ServerMetadata>();
  bool parsed = true;
  // A known key (grpc-retry-pushback-ms) is parsed by its own trait.
  c->refusal->Append(k, Slice::FromCopiedBuffer(v.data(), v.size()),
                     [&parsed](absl::string_view, const Slice&) {
                       parsed = false;
                     });
  if (!parsed) c->refusal->Remove(k);
  return parsed ? 1 : 0;
}

const grpc_server_call_hook_ops kHookOps = {HookGet, HookAdd};

}  // namespace

const grpc_server_call_hook* GetServerCallHook() {
  return g_server_call_hook.load(std::memory_order_relaxed);
}

ServerMetadataHandle StartServerCallHook(const grpc_server_call_hook* hook,
                                         ClientMetadata& md,
                                         ServerCallHookState* state) {
  // gpr_now needs no ExecCtx, so this runs wherever metadata is parsed.
  state->hook = hook;
  state->start = gpr_now(GPR_CLOCK_MONOTONIC);
  HookCall call{&md, nullptr};
  const char* message = nullptr;
  size_t message_len = 0;
  grpc_status_code status =
      hook->on_call_start(hook->user_data, &kHookOps, &call, &state->call_word,
                          &message, &message_len);
  if (status == GRPC_STATUS_OK) return nullptr;
  ServerMetadataHandle refusal = std::move(call.refusal);
  if (refusal == nullptr) refusal = Arena::MakePooled<ServerMetadata>();
  refusal->Set(GrpcStatusMetadata(), status);
  refusal->Set(GrpcMessageMetadata(),
               message != nullptr
                   ? Slice::FromCopiedBuffer(message, message_len)
                   : Slice::FromStaticString("call refused"));
  return refusal;
}

void EndServerCallHook(ServerCallHookState* state, grpc_status_code status) {
  const grpc_server_call_hook* hook = state->hook;
  state->hook = nullptr;
  gpr_timespec d = gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), state->start);
  hook->on_call_end(hook->user_data, state->call_word, status,
                    int64_t{d.tv_sec} * 1000000 + d.tv_nsec / 1000);
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

void grpc_server_call_hook_register(const grpc_server_call_hook* hook) {
  if (hook != nullptr && hook->struct_size < sizeof(grpc_server_call_hook)) {
    LOG(ERROR) << "ignoring a server call hook built against an older header";
    return;
  }
  grpc_core::g_server_call_hook.store(hook, std::memory_order_relaxed);
}
