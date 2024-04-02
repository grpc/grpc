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
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_call_data.h"

namespace grpc_core {

namespace {

class ServerConfigSelectorFilter final
    : public ImplementChannelFilter<ServerConfigSelectorFilter> {
 public:
  ~ServerConfigSelectorFilter() override;

  ServerConfigSelectorFilter(const ServerConfigSelectorFilter&) = delete;
  ServerConfigSelectorFilter& operator=(const ServerConfigSelectorFilter&) =
      delete;
  ServerConfigSelectorFilter(ServerConfigSelectorFilter&&) = default;
  ServerConfigSelectorFilter& operator=(ServerConfigSelectorFilter&&) = default;

  static absl::StatusOr<ServerConfigSelectorFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args);

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& md,
                                         ServerConfigSelectorFilter* filter);
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;
  };

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

absl::Status ServerConfigSelectorFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ServerConfigSelectorFilter* filter) {
  auto sel = filter->config_selector();
  if (!sel.ok()) return sel.status();
  auto call_config = sel.value()->GetCallConfig(&md);
  if (!call_config.ok()) {
    return absl::UnavailableError(StatusToString(call_config.status()));
  }
  auto* service_config_call_data =
      GetContext<Arena>()->New<ServiceConfigCallData>(
          GetContext<Arena>(), GetContext<grpc_call_context_element>());
  service_config_call_data->SetServiceConfig(
      std::move(call_config->service_config), call_config->method_configs);
  return absl::OkStatus();
}

const NoInterceptor ServerConfigSelectorFilter::Call::OnServerInitialMetadata;
const NoInterceptor ServerConfigSelectorFilter::Call::OnServerTrailingMetadata;
const NoInterceptor ServerConfigSelectorFilter::Call::OnClientToServerMessage;
const NoInterceptor ServerConfigSelectorFilter::Call::OnServerToClientMessage;
const NoInterceptor ServerConfigSelectorFilter::Call::OnFinalize;

}  // namespace

const grpc_channel_filter kServerConfigSelectorFilter =
    MakePromiseBasedFilter<ServerConfigSelectorFilter, FilterEndpoint::kServer>(
        "server_config_selector_filter");

}  // namespace grpc_core
