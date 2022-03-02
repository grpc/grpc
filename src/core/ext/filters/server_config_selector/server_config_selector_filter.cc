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

#include "src/core/ext/filters/server_config_selector/server_config_selector.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

namespace {

class ServerConfigSelectorFilter {
 public:
  static absl::StatusOr<ServerConfigSelectorFilter> Create(
      const grpc_channel_args* args);

  ArenaPromise<TrailingMetadata> MakeCallPromise(
      ClientInitialMetadata initial_metadata,
      NextPromiseFactory next_promise_factory);

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector() {
    MutexLock lock(&mu_);
    return config_selector_.value();
  }

 private:
  struct State {
    Mutex mu_;
    absl::optional<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
        config_selector_ ABSL_GUARDED_BY(mu_);
  };
  class ServerConfigSelectorWatcher
      : public ServerConfigSelectorProvider::ServerConfigSelectorWatcher {
   public:
    explicit ServerConfigSelectorWatcher(ServerConfigSelectorFilter* chand)
        : chand_(chand) {}
    void OnServerConfigSelectorUpdate(
        absl::StatusOr<RefCountedPtr<ServerConfigSelector>> update) override {
      MutexLock lock(&chand_->mu_);
      chand_->config_selector_ = std::move(update);
    }

   private:
    ServerConfigSelectorFilter* chand_;
  };

  explicit ServerConfigSelectorFilter(
      RefCountedPtr<ServerConfigSelectorProvider>
          server_config_selector_provider);
  ~ServerConfigSelectorFilter();

  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider_;
};

absl::StatusOr<ServerConfigSelectorFilter> ServerConfigSelectorFilter::Create(
    const grpc_channel_args* args) {
  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider =
      ServerConfigSelectorProvider::GetFromChannelArgs(*args);
  if (server_config_selector_provider == nullptr) {
    return absl::UnknownError("No ServerConfigSelectorProvider object found");
  }
  return ServerConfigSelectorFilter(std::move(server_config_selector_provider));
}

ServerConfigSelectorFilter::ServerConfigSelectorFilter(
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider)
    : server_config_selector_provider_(
          std::move(server_config_selector_provider)) {
  GPR_ASSERT(server_config_selector_provider_ != nullptr);
  auto server_config_selector_watcher =
      absl::make_unique<ServerConfigSelectorWatcher>(this);
  auto config_selector = server_config_selector_provider_->Watch(
      std::move(server_config_selector_watcher));
  MutexLock lock(&mu_);
  // It's possible for the watcher to have already updated config_selector_
  if (!config_selector_.has_value()) {
    config_selector_ = std::move(config_selector);
  }
}

ServerConfigSelectorFilter::~ServerConfigSelectorFilter() {
  server_config_selector_provider_->CancelWatch();
}

ArenaPromise<TrailingMetadata> ServerConfigSelectorFilter::MakeCallPromise(
    ClientInitialMetadata initial_metadata,
    NextPromiseFactory next_promise_factory) {
  auto sel = config_selector();
  if (!sel.ok()) return Immediate(sel.status());
  auto call_config = sel.value()->GetCallConfig(calld->recv_initial_metadata_);
  if (call_config.error != GRPC_ERROR_NONE) {
    return grpc_error_to_absl_status(call_config.error);
  }
  GetContext<
      grpc_call_context_element>()[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA] =
      GetContext<Arena>()->New<ServiceConfigCallData>(
          std::move(call_config.service_config), call_config.method_configs,
          {});
  return next_promise_factory(std::move(initial_metadata));
}

}  // namespace

const grpc_channel_filter kServerConfigSelectorFilter =
    MakePromiseBasedFilter<ServerConfigSelectorFilter, FilterEndpoint::kServer>(
        "server_config_selector_filter");

}  // namespace grpc_core
