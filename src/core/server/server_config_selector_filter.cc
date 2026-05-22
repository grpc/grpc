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

#include <grpc/support/port_platform.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/observable.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server_config_selector.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

namespace {

class LegacyServerConfigSelectorFilter final
    : public ImplementChannelFilter<LegacyServerConfigSelectorFilter>,
      public InternallyRefCounted<LegacyServerConfigSelectorFilter> {
 public:
  explicit LegacyServerConfigSelectorFilter(
      RefCountedPtr<ServerConfigSelectorProvider>
          server_config_selector_provider);

  static absl::string_view TypeName() {
    return "server_config_selector_filter";
  }

  LegacyServerConfigSelectorFilter(const LegacyServerConfigSelectorFilter&) =
      delete;
  LegacyServerConfigSelectorFilter& operator=(
      const LegacyServerConfigSelectorFilter&) = delete;

  static absl::StatusOr<OrphanablePtr<LegacyServerConfigSelectorFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args);

  void Orphan() override;

  class Call {
   public:
    absl::Status OnClientInitialMetadata(
        ClientMetadata& md, LegacyServerConfigSelectorFilter* filter);
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
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
        RefCountedPtr<LegacyServerConfigSelectorFilter> filter)
        : filter_(filter) {}
    void OnServerConfigSelectorUpdate(
        absl::StatusOr<RefCountedPtr<ServerConfigSelector>> update) override {
      MutexLock lock(&filter_->mu_);
      filter_->config_selector_ = std::move(update);
    }

   private:
    RefCountedPtr<LegacyServerConfigSelectorFilter> filter_;
  };

  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider_;
  std::shared_ptr<ServerConfigSelectorWatcher> watcher_;
  Mutex mu_;
  std::optional<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
      config_selector_ ABSL_GUARDED_BY(mu_);
};

absl::StatusOr<OrphanablePtr<LegacyServerConfigSelectorFilter>>
LegacyServerConfigSelectorFilter::Create(const ChannelArgs& args,
                                         ChannelFilter::Args) {
  ServerConfigSelectorProvider* server_config_selector_provider =
      args.GetObject<ServerConfigSelectorProvider>();
  if (server_config_selector_provider == nullptr) {
    return absl::UnknownError("No ServerConfigSelectorProvider object found");
  }
  return MakeOrphanable<LegacyServerConfigSelectorFilter>(
      server_config_selector_provider->Ref());
}

LegacyServerConfigSelectorFilter::LegacyServerConfigSelectorFilter(
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider)
    : server_config_selector_provider_(
          std::move(server_config_selector_provider)) {
  GRPC_CHECK(server_config_selector_provider_ != nullptr);
  watcher_ = std::make_shared<ServerConfigSelectorWatcher>(Ref());
  auto config_selector = server_config_selector_provider_->Watch(watcher_);
  MutexLock lock(&mu_);
  // It's possible for the watcher to have already updated config_selector_
  if (!config_selector_.has_value()) {
    config_selector_ = std::move(config_selector);
  }
}

void LegacyServerConfigSelectorFilter::Orphan() {
  if (server_config_selector_provider_ != nullptr) {
    server_config_selector_provider_->CancelWatch(std::move(watcher_));
  }
  Unref();
}

absl::Status LegacyServerConfigSelectorFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, LegacyServerConfigSelectorFilter* filter) {
  GRPC_LATENT_SEE_SCOPE(
      "LegacyServerConfigSelectorFilter::Call::OnClientInitialMetadata");
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

}  // namespace

const grpc_channel_filter kLegacyServerConfigSelectorFilter =
    MakePromiseBasedFilter<LegacyServerConfigSelectorFilter,
                           FilterEndpoint::kServer>();

namespace {

class ServerConfigSelectorFilter final
    : public ImplementChannelFilter<ServerConfigSelectorFilter>,
      public InternallyRefCounted<ServerConfigSelectorFilter> {
 public:
  class Call {
   public:
    ArenaPromise<absl::Status> OnClientInitialMetadata(
        ClientMetadata& md, ServerConfigSelectorFilter* filter);
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
  };

  static absl::StatusOr<OrphanablePtr<ServerConfigSelectorFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args) {
    auto server_config_selector_provider =
        args.GetObjectRef<ServerConfigSelectorProvider>();
    if (server_config_selector_provider == nullptr) {
      return absl::UnknownError("No ServerConfigSelectorProvider object found");
    }
    return MakeOrphanable<ServerConfigSelectorFilter>(
        std::move(server_config_selector_provider));
  }

  explicit ServerConfigSelectorFilter(
      RefCountedPtr<ServerConfigSelectorProvider>
          server_config_selector_provider)
      : server_config_selector_provider_(
            std::move(server_config_selector_provider)),
        config_selector_(nullptr) {
    watcher_ = std::make_shared<ServerConfigSelectorWatcher>(Ref());
    // TODO(roth): Remove the void cast when removing the
    // xds_server_filter_chain_per_route experiment.
    (void)server_config_selector_provider_->Watch(watcher_);
  }

  ServerConfigSelectorFilter(const ServerConfigSelectorFilter&) = delete;
  ServerConfigSelectorFilter& operator=(const ServerConfigSelectorFilter&) =
      delete;

  void Orphan() override {
    if (server_config_selector_provider_ != nullptr) {
      server_config_selector_provider_->CancelWatch(std::move(watcher_));
    }
    Unref();
  }

  static absl::string_view TypeName() {
    return "server_config_selector_filter";
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
      // TODO(roth): Build filter chains here.
      filter_->config_selector_.Set(std::move(update));
    }

   private:
    RefCountedPtr<ServerConfigSelectorFilter> filter_;
  };

  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider_;
  std::shared_ptr<ServerConfigSelectorWatcher> watcher_;
  Observable<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
      config_selector_;
};

ArenaPromise<absl::Status>
ServerConfigSelectorFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ServerConfigSelectorFilter* filter) {
  GRPC_LATENT_SEE_SCOPE(
      "ServerConfigSelectorFilter::Call::OnClientInitialMetadata");
  return TrySeq(
      filter->config_selector_.Next(nullptr),
      [&md](
          absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector) {
        if (!config_selector.ok()) return config_selector.status();
        auto call_config = (*config_selector)->GetCallConfig(&md);
        if (!call_config.ok()) {
          return absl::UnavailableError(StatusToString(call_config.status()));
        }
        auto* arena = GetContext<Arena>();
        auto* service_config_call_data =
            arena->New<ServiceConfigCallData>(arena);
        service_config_call_data->SetServiceConfig(
            std::move(call_config->service_config),
            call_config->method_configs);
        return absl::OkStatus();
      });
}

}  // namespace

const grpc_channel_filter kServerConfigSelectorFilter =
    MakePromiseBasedFilter<ServerConfigSelectorFilter,
                           FilterEndpoint::kServer>();

}  // namespace grpc_core
