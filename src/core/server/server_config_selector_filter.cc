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
#include "src/core/call/status_util.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
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
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

//
// ServerConfigSelectorFilter
//

namespace {

class ServerConfigSelectorFilter final
    : public ImplementChannelFilter<ServerConfigSelectorFilter>,
      public InternallyRefCounted<ServerConfigSelectorFilter> {
 public:
  explicit ServerConfigSelectorFilter(
      RefCountedPtr<ServerConfigSelectorProvider>
          server_config_selector_provider);

  static absl::string_view TypeName() {
    return "server_config_selector_filter";
  }

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
  std::optional<absl::StatusOr<RefCountedPtr<ServerConfigSelector>>>
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
  GRPC_CHECK(server_config_selector_provider_ != nullptr);
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
  GRPC_LATENT_SEE_SCOPE(
      "ServerConfigSelectorFilter::Call::OnClientInitialMetadata");
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

const grpc_channel_filter kServerConfigSelectorFilter =
    MakePromiseBasedFilter<ServerConfigSelectorFilter,
                           FilterEndpoint::kServer>();

//
// ServerConfigSelectorInterceptor::Watcher
//

// Watcher for ServerConfigSelector.
class ServerConfigSelectorInterceptor::Watcher final
    : public ServerConfigSelectorProvider::ServerConfigSelectorWatcher {
 public:
  explicit Watcher(
      WeakRefCountedPtr<ServerConfigSelectorInterceptor> interceptor)
      : interceptor_(std::move(interceptor)) {}

  void OnServerConfigSelectorUpdate(
      absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector)
      override {
    if (config_selector.ok()) {
      interceptor_->BuildDynamicFilterChains(**config_selector);
    }
    interceptor_->config_selector_.Set(std::move(config_selector));
  }

 private:
  WeakRefCountedPtr<ServerConfigSelectorInterceptor> interceptor_;
};

//
// ServerConfigSelectorInterceptor
//

const grpc_channel_filter ServerConfigSelectorInterceptor::kFilterVtable =
    MakePromiseBasedFilter<
        ServerConfigSelectorInterceptor, FilterEndpoint::kClient,
        kFilterExaminesServerInitialMetadata | kFilterExaminesOutboundMessages |
            kFilterExaminesInboundMessages | kFilterExaminesCallContext>();

absl::StatusOr<RefCountedPtr<ServerConfigSelectorInterceptor>>
ServerConfigSelectorInterceptor::Create(const ChannelArgs& args,
                                        ChannelFilter::Args filter_args) {
  // Get ConfigSelectorProvider from channel args.
  auto server_config_selector_provider =
      args.GetObjectRef<ServerConfigSelectorProvider>();
  if (server_config_selector_provider == nullptr) {
    return absl::UnknownError("No ServerConfigSelectorProvider object found");
  }
  return MakeRefCounted<ServerConfigSelectorInterceptor>(
      args, std::move(filter_args), std::move(server_config_selector_provider));
}

ServerConfigSelectorInterceptor::ServerConfigSelectorInterceptor(
    const ChannelArgs& args, ChannelFilter::Args filter_args,
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider)
    : args_(args),
      server_config_selector_provider_(
          std::move(server_config_selector_provider)),
      config_selector_(nullptr) {
  // Start watch for ServerConfigSelector.
  auto watcher = std::make_unique<Watcher>(
      WeakRef().TakeAsSubclass<ServerConfigSelectorInterceptor>());
  auto config_selector =
      server_config_selector_provider_->Watch(std::move(watcher));
  if (config_selector.ok()) BuildDynamicFilterChains(**config_selector);
  // FIXME: possible race condition?
  // FIXME: maybe just use observable in provider instead of
  // callback-based watcher API?
  config_selector_.Set(std::move(config_selector));
}

namespace {

class FilterChainImpl final : public FilterChain {
 public:
  explicit FilterChainImpl(RefCountedPtr<UnstartedCallDestination> destination)
      : destination_(std::move(destination)) {}

  UnstartedCallDestination* destination() const { return destination_.get(); }

 private:
  RefCountedPtr<UnstartedCallDestination> destination_;
};

class FilterChainBuilderImpl final : public FilterChainBuilder {
 public:
  FilterChainBuilderImpl(const ChannelArgs& channel_args,
                         Blackboard* blackboard,
                         RefCountedPtr<UnstartedCallDestination> destination)
      : channel_args_(channel_args),
        blackboard_(blackboard),
        destination_(std::move(destination)) {}

  absl::StatusOr<RefCountedPtr<FilterChain>> Build() override {
    if (builder_ == nullptr) InitBuilder();
    auto top_of_stack_destination = builder_->Build(destination_);
    if (!top_of_stack_destination.ok()) {
// FIXME: use MaybeRewriteIllegalStatusCode() throughout?
      return MaybeRewriteIllegalStatusCode(top_of_stack_destination.status(),
                                           "channel construction");
    }
    builder_.reset();
    return MakeRefCounted<FilterChainImpl>(
        std::move(*top_of_stack_destination));
  }

 private:
  void AddFilter(const FilterHandle& filter_handle,
                 RefCountedPtr<const FilterConfig> config) override {
    if (builder_ == nullptr) InitBuilder();
    filter_handle.AddToBuilder(builder_.get(), std::move(config));
  }

  void InitBuilder() {
    builder_ =
        std::make_unique<InterceptionChainBuilder>(channel_args_, blackboard_);
  }

  const ChannelArgs channel_args_;
  const Blackboard* blackboard_;
  const RefCountedPtr<UnstartedCallDestination> destination_;
  std::unique_ptr<InterceptionChainBuilder> builder_;
};

}  // namespace

void ServerConfigSelectorInterceptor::BuildDynamicFilterChains(
    ServerConfigSelector& config_selector) {
  FilterChainBuilderImpl builder(
      args_,
      /*blackboard=*/nullptr,  // FIXME: plumb blackboard?
      wrapped_destination());
  config_selector.BuildFilterChains(builder);
}

// FIXME: add a new tracer here

void ServerConfigSelectorInterceptor::InterceptCall(
    UnstartedCallHandler unstarted_call_handler) {
  // Consume the call coming to us from the client side.
  CallHandler handler = Consume(std::move(unstarted_call_handler));
  handler.SpawnGuarded(
      "choose_filter_chain",
      [self = RefAsSubclass<ServerConfigSelectorInterceptor>(),
       handler]() mutable {
        return TrySeq(
            handler.PullClientInitialMetadata(),
            [handler, self](ClientMetadataHandle metadata) mutable {
              return Map(
                  self->config_selector_.Next(nullptr),
                  [self, handler = std::move(handler),
                   metadata = std::move(metadata)](
                      absl::StatusOr<RefCountedPtr<ServerConfigSelector>>
                          config_selector) mutable {
                    if (!config_selector.ok()) {
                      GRPC_TRACE_LOG(channel, INFO)
                          << "[server_config_selector_interceptor "
                          << self.get() << "]: config selector is error: "
                          << config_selector.status();
                      return config_selector.status();
                    }
                    // Use config selector to choose dynamic filter stack.
                    auto call_config =
                        (*config_selector)->GetCallConfig(metadata.get());
                    if (!call_config.ok()) {
                      GRPC_TRACE_LOG(channel, INFO)
                          << "[server_config_selector_interceptor "
                          << self.get() << "]: config selector returned error: "
                          << call_config.status();
                      return call_config.status();
                    }
                    if (!call_config->filter_chain.ok()) {
                      GRPC_TRACE_LOG(channel, INFO)
                          << "[server_config_selector_interceptor "
                          << self.get()
                          << "]: config selector returned failure for filter "
                             "chain: "
                          << call_config->filter_chain.status();
                      return call_config->filter_chain.status();
                    }
                    // Start call on selected filter chain.
                    GRPC_TRACE_LOG(channel, INFO)
                        << "[server_config_selector_interceptor " << self.get()
                        << "]: starting call on filter chain";
                    auto& filter_chain = DownCast<const FilterChainImpl&>(
                        **call_config->filter_chain);
                    auto [initiator, unstarted_handler] = MakeCallPair(
                        std::move(metadata), GetContext<Arena>()->Ref());
                    filter_chain.destination()->StartCall(
                        std::move(unstarted_handler));
                    ForwardCall(handler, initiator);
                    return absl::OkStatus();
                  });
            });
      });
}

}  // namespace grpc_core
