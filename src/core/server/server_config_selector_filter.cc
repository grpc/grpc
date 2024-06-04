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

#include "src/core/server/server_config_selector_filter.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
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
#include "src/core/server/server_config_selector.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_call_data.h"

namespace grpc_core {

namespace {

class ServerConfigSelectorFilter final
    : public ImplementChannelFilter<ServerConfigSelectorFilter>,
      public InternallyRefCounted<ServerConfigSelectorFilter> {
 public:
  explicit ServerConfigSelectorFilter(
      RefCountedPtr<ServerConfigSelectorProvider>
          server_config_selector_provider);

  ServerConfigSelectorFilter(const ServerConfigSelectorFilter&) = delete;
  ServerConfigSelectorFilter& operator=(const ServerConfigSelectorFilter&) =
      delete;

  static absl::StatusOr<OrphanablePtr<ServerConfigSelectorFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args);

  void Orphan() override;

  class Call {
   public:
    absl::Status OnClientInitialMetadata(ClientMetadata& md,
                                         ServerConfigSelectorFilter* filter);
    static const NoInterceptor OnServerInitialMetadata;
    static const NoInterceptor OnServerTrailingMetadata;
    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnClientToServerHalfClose;
    static const NoInterceptor OnServerToClientMessage;
    static const NoInterceptor OnFinalize;
  };

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector() {
    MutexLock lock(&mu_);
    return config_selector_.value();
  }

 private:
  class ServerConfigSelectorWatcher
      : public ServerConfigSelectorProvider::ServerConfigSelectorWatcher {
   public:
    explicit ServerConfigSelectorWatcher(
        RefCountedPtr<ServerConfigSelectorFilter> filter)
        : filter_(filter) {}
    void OnServerConfigSelectorUpdate(
        absl::StatusOr<RefCountedPtr<ServerConfigSelector>> update) override {
      MutexLock lock(&filter_->mu_);
      filter_->config_selector_ = std::move(update);
    }

   private:
    RefCountedPtr<ServerConfigSelectorFilter> filter_;
  };

  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider_;
  Mutex mu_;
  absl::optional<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
      config_selector_ ABSL_GUARDED_BY(mu_);
};

absl::StatusOr<OrphanablePtr<ServerConfigSelectorFilter>>
ServerConfigSelectorFilter::Create(const ChannelArgs& args,
                                   ChannelFilter::Args) {
  ServerConfigSelectorProvider* server_config_selector_provider =
      args.GetObject<ServerConfigSelectorProvider>();
  if (server_config_selector_provider == nullptr) {
    return absl::UnknownError("No ServerConfigSelectorProvider object found");
  }
  return MakeOrphanable<ServerConfigSelectorFilter>(
      server_config_selector_provider->Ref());
}

ServerConfigSelectorFilter::ServerConfigSelectorFilter(
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider)
    : server_config_selector_provider_(
          std::move(server_config_selector_provider)) {
  CHECK(server_config_selector_provider_ != nullptr);
  auto server_config_selector_watcher =
      std::make_unique<ServerConfigSelectorWatcher>(Ref());
  auto config_selector = server_config_selector_provider_->Watch(
      std::move(server_config_selector_watcher));
  MutexLock lock(&mu_);
  // It's possible for the watcher to have already updated config_selector_
  if (!config_selector_.has_value()) {
    config_selector_ = std::move(config_selector);
  }
}

void ServerConfigSelectorFilter::Orphan() {
  if (server_config_selector_provider_ != nullptr) {
    server_config_selector_provider_->CancelWatch();
  }
  Unref();
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
      GetContext<Arena>()->New<ServiceConfigCallData>(GetContext<Arena>());
  service_config_call_data->SetServiceConfig(
      std::move(call_config->service_config), call_config->method_configs);
  return absl::OkStatus();
}

const NoInterceptor ServerConfigSelectorFilter::Call::OnServerInitialMetadata;
const NoInterceptor ServerConfigSelectorFilter::Call::OnServerTrailingMetadata;
const NoInterceptor ServerConfigSelectorFilter::Call::OnClientToServerMessage;
const NoInterceptor ServerConfigSelectorFilter::Call::OnClientToServerHalfClose;
const NoInterceptor ServerConfigSelectorFilter::Call::OnServerToClientMessage;
const NoInterceptor ServerConfigSelectorFilter::Call::OnFinalize;

}  // namespace

const grpc_channel_filter kServerConfigSelectorFilter =
    MakePromiseBasedFilter<ServerConfigSelectorFilter, FilterEndpoint::kServer>(
        "server_config_selector_filter");

}  // namespace grpc_core
