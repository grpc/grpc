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
#include "absl/log/check.h"
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

// Whether gRPC parses `key` into a trait rather than keeping it as unknown
// metadata.
struct IsTraitKey {
  template <typename Trait>
  bool Found(Trait) {
    return true;
  }
  bool NotFound(absl::string_view) { return false; }
};

template <typename Derived, typename... Traits>
bool ParsesKey(const MetadataMap<Derived, Traits...>&, absl::string_view key) {
  IsTraitKey op;
  return metadata_detail::NameLookup<Traits...>::Lookup(key, &op);
}

// The values of an unparsed key, each kept as its own entry in arrival order:
// counted and sized, or, given `out` and its `room`, written there joined with
// ','.
class UnknownValues {
 public:
  explicit UnknownValues(absl::string_view key, char* out = nullptr,
                         size_t room = 0)
      : key_(key), out_(out), room_(room) {}

  template <typename Which, typename Value>
  void Encode(const Which&, const Value&) {}
  void Encode(const Slice& key, const Slice& value) {
    if (key.as_string_view() != key_) return;
    absl::string_view v = value.as_string_view();
    if (count_ == 0) first_ = v;
    if (out_ != nullptr) {
      // The first pass sized the block, and the batch doesn't change between.
      const size_t n = (count_ != 0 ? 1 : 0) + v.size();
      DCHECK_LE(n, room_);
      room_ -= n;
      if (count_ != 0) *out_++ = ',';
      if (!v.empty()) memcpy(out_, v.data(), v.size());
      out_ += v.size();
    }
    ++count_;
    size_ += v.size();
  }

  size_t count() const { return count_; }
  absl::string_view first() const { return first_; }
  // The values and a ',' between each two.
  size_t joined_size() const {
    DCHECK_GE(count_, 1u);
    return size_ + count_ - 1;
  }

 private:
  absl::string_view key_;
  char* out_;
  size_t room_;
  size_t count_ = 0;
  size_t size_ = 0;
  absl::string_view first_;
};

int HookGet(void* call, const char* key, size_t key_len, const char** value,
            size_t* value_len) {
  const ClientMetadata& metadata = *static_cast<HookCall*>(call)->metadata;
  absl::string_view name(key, key_len);
  if (!ParsesKey(metadata, name)) {
    // Joined here, in one pass into the call's arena once its size is known:
    // GetStringValue would copy everything joined so far again on every
    // repeat, so k values would cost k^2.
    UnknownValues values(name);
    metadata.Encode(&values);
    if (values.count() == 0) return 0;
    if (values.count() == 1) {
      *value = values.first().empty() ? "" : values.first().data();
      *value_len = values.first().size();
      return 1;
    }
    char* joined =
        static_cast<char*>(GetContext<Arena>()->Alloc(values.joined_size()));
    UnknownValues copy(name, joined, values.joined_size());
    metadata.Encode(&copy);
    DCHECK_EQ(copy.count(), values.count());
    *value = joined;
    *value_len = values.joined_size();
    return 1;
  }
  // A key gRPC parses: in place, or appended or encoded once into `joined`.
  std::string joined;
  std::optional<absl::string_view> found =
      metadata.GetStringValue(name, &joined);
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
