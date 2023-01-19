// Copyright 2021 gRPC authors.
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

#include "src/core/ext/filters/server_config_selector/server_config_selector_filter.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/ext/filters/server_config_selector/server_config_selector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

namespace {

class ServerConfigSelectorFilter final : public ChannelFilter {
 public:
  ~ServerConfigSelectorFilter() override;

  ServerConfigSelectorFilter(const ServerConfigSelectorFilter&) = delete;
  ServerConfigSelectorFilter& operator=(const ServerConfigSelectorFilter&) =
      delete;
  ServerConfigSelectorFilter(ServerConfigSelectorFilter&&) = default;
  ServerConfigSelectorFilter& operator=(ServerConfigSelectorFilter&&) = default;

  static absl::StatusOr<ServerConfigSelectorFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args);

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector() {
    MutexLock lock(&state_->mu);
    return state_->config_selector.value();
  }

 private:
  struct State {
    Mutex mu;
    absl::optional<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
        config_selector ABSL_GUARDED_BY(mu);
  };
  class ServerConfigSelectorWatcher
      : public ServerConfigSelectorProvider::ServerConfigSelectorWatcher {
   public:
    explicit ServerConfigSelectorWatcher(std::shared_ptr<State> state)
        : state_(state) {}
    void OnServerConfigSelectorUpdate(
        absl::StatusOr<RefCountedPtr<ServerConfigSelector>> update) override {
      MutexLock lock(&state_->mu);
      state_->config_selector = std::move(update);
    }

   private:
    std::shared_ptr<State> state_;
  };

  explicit ServerConfigSelectorFilter(
      RefCountedPtr<ServerConfigSelectorProvider>
          server_config_selector_provider);

  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider_;
  std::shared_ptr<State> state_;
};

absl::StatusOr<ServerConfigSelectorFilter> ServerConfigSelectorFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args) {
  ServerConfigSelectorProvider* server_config_selector_provider =
      args.GetObject<ServerConfigSelectorProvider>();
  if (server_config_selector_provider == nullptr) {
    return absl::UnknownError("No ServerConfigSelectorProvider object found");
  }
  return ServerConfigSelectorFilter(server_config_selector_provider->Ref());
}

ServerConfigSelectorFilter::ServerConfigSelectorFilter(
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider)
    : server_config_selector_provider_(
          std::move(server_config_selector_provider)),
      state_(std::make_shared<State>()) {
  GPR_ASSERT(server_config_selector_provider_ != nullptr);
  auto server_config_selector_watcher =
      std::make_unique<ServerConfigSelectorWatcher>(state_);
  auto config_selector = server_config_selector_provider_->Watch(
      std::move(server_config_selector_watcher));
  MutexLock lock(&state_->mu);
  // It's possible for the watcher to have already updated config_selector_
  if (!state_->config_selector.has_value()) {
    state_->config_selector = std::move(config_selector);
  }
}

ServerConfigSelectorFilter::~ServerConfigSelectorFilter() {
  if (server_config_selector_provider_ != nullptr) {
    server_config_selector_provider_->CancelWatch();
  }
}

ArenaPromise<ServerMetadataHandle> ServerConfigSelectorFilter::MakeCallPromise(
    CallArgs call_args, NextPromiseFactory next_promise_factory) {
  auto sel = config_selector();
  if (!sel.ok()) return Immediate(ServerMetadataFromStatus(sel.status()));
  auto call_config =
      sel.value()->GetCallConfig(call_args.client_initial_metadata.get());
  if (!call_config.ok()) {
    auto r = Immediate(ServerMetadataFromStatus(
        absl::UnavailableError(StatusToString(call_config.status()))));
    return std::move(r);
  }
  auto& ctx = GetContext<
      grpc_call_context_element>()[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA];
  ctx.value = GetContext<Arena>()->New<ServiceConfigCallData>(
      std::move(call_config->service_config), call_config->method_configs,
      ServiceConfigCallData::CallAttributes{});
  ctx.destroy = [](void* p) {
    static_cast<ServiceConfigCallData*>(p)->~ServiceConfigCallData();
  };
  return next_promise_factory(std::move(call_args));
}

}  // namespace

const grpc_channel_filter kServerConfigSelectorFilter =
    MakePromiseBasedFilter<ServerConfigSelectorFilter, FilterEndpoint::kServer>(
        "server_config_selector_filter");

}  // namespace grpc_core
