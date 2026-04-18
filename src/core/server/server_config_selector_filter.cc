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

namespace {

//
// ServerConfigSelectorFilter
//

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
// ServerConfigSelectorFilterV1
//

#define GRPC_ARG_SERVER_CONFIG_SELECTOR_FILTER_V1 \
  "grpc.internal.server_config_selector_filter_v1"

absl::Status ServerConfigSelectorFilterV1::Init(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  GRPC_CHECK(args->is_last);
  GRPC_CHECK(elem->filter == &kFilterVtable);
  // Get ConfigSelectorProvider from channel args.
  ServerConfigSelectorProvider* server_config_selector_provider =
      args.GetObject<ServerConfigSelectorProvider>();
  if (server_config_selector_provider == nullptr) {
    return absl::UnknownError("No ServerConfigSelectorProvider object found");
  }

// FIXME: build bottom channel stack

  // Instantiate filter.
  new (elem->channel_data) ServerConfigSelectorFilterV1(
      server_config_selector_provider->Ref());
  return absl::OkStatus();
}

ServerConfigSelectorFilterV1::ServerConfigSelectorFilterV1(
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider)
    : server_config_selector_provider_(
          std::move(server_config_selector_provider)) {
  GRPC_CHECK(server_config_selector_provider_ != nullptr);
  auto server_config_selector_watcher =
      std::make_unique<ServerConfigSelectorWatcher>(Ref());
  auto config_selector = server_config_selector_provider_->Watch(
      std::move(server_config_selector_watcher));
  if (config_selector.ok()) BuildDynamicFilterChains(**config_selector);
  MutexLock lock(&mu_);
  // It's possible for the watcher to have already updated config_selector_
  if (!config_selector_.has_value()) {
    config_selector_ = std::move(config_selector);
  }
}

void ServerConfigSelectorFilterV1::BuildDynamicFilterChains(
    ServerConfigSelector& config_selector) {
// FIXME: tell config selector to build filter chains
}

void ServerConfigSelectorFilterV1::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  GRPC_LATENT_SEE_SCOPE(
      "ServerConfigSelectorFilterV1::StartTransportStreamOpBatch");
// FIXME: need to support batch queueing in case we get batches in the wrong order
  if (batch->send_initial_metadata) {
    auto config_selector = GetConfigSelector();
    if (!config_selector.ok()) return config_selector.status();
    auto dynamic_filter_stack = (*config_selector)->GetCallConfig(&md);
    if (!dynamic_filter_stack.ok()) return dynamic_filter_stack.status();
// FIXME: create call stack for dynamic call
  }
  return absl::OkStatus();
}

void ServerConfigSelectorFilterV1::StartTransportOp(grpc_transport_op* op) {
  // Propagate straight down to the bottom stack.
  // We don't bother sending this through the dynamic stack, because (a)
  // we don't know which dynamic stack to use, and (b) none of the
  // dynamic filters need to see transport ops anyway.
  grpc_channel_element* elem =
      grpc_channel_stack_element(bottom_stack_.get(), 0);
  elem->filter->start_transport_op(elem, op);
}

const grpc_channel_filter ServerConfigSelectorFilterV1::kFilterVtable = {
  // start_transport_stream_op_batch
  [](grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
    auto* chand =
        static_cast<ServerConfigSelectorFilterV1*>(elem->channel_data);
    chand->StartTransportStreamOpBatch(op);
  },
  // start_transport_op
  [](grpc_channel_element* elem, grpc_transport_op* op) {
    auto* chand =
        static_cast<ServerConfigSelectorFilterV1*>(elem->channel_data);
    chand->StartTransportOp(op);
  },
  // sizeof_call_data
  0,
  // init_call_elem
  [](grpc_call_element* elem, const grpc_call_element_args* args) {
    return absl::OkStatus();
  },
  // set_pollset_or_pollset_set
  [](grpc_call_element* elem, grpc_polling_entity* pollent) {
  },
  // destroy_call_elem
  [](grpc_call_element* elem, const grpc_call_final_info* final_info,
     grpc_closure* then_schedule_closure) {
  },
  // sizeof_channel_data
  sizeof(ServerConfigSelectorFilterV1),
  // init_channel_elem
  ServerConfigSelectorFilterV1::Init,
  // post_init_channel_elem
  [](grpc_channel_stack* stk, grpc_channel_element* elem) {
  },
  // destroy_channel_elem
  [](grpc_channel_element* elem) {
    auto* chand =
        static_cast<ServerConfigSelectorFilterV1*>(elem->channel_data);
    chand->~ServerConfigSelectorFilterV1();
  },
  // get_channel_info
  [](grpc_channel_element* elem, const grpc_channel_info* channel_info) {
  },
  // name
  GRPC_UNIQUE_TYPE_NAME_HERE("server_config_selector_v1"),
};

//
// ServerConfigSelectorFilterV1::BottomCall
//

RefCountedPtr<ServerConfigSelectorFilterV1::BottomCall>
ServerConfigSelectorFilterV1::BottomCall::Create(Arena* arena) {
// FIXME: create call stack on bottom_stack_
  return nullptr;
}

grpc_call_stack* ServerConfigSelectorFilterV1::BottomCall::call_stack() {
  return (grpc_call_stack*)(
      (char*)(this) + GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(BottomCall)));
}

void ServerConfigSelectorFilterV1::BottomCall::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack(), 0);
  GRPC_TRACE_LOG(channel, INFO)
      << "OP[" << top_elem->filter->name << ":" << top_elem
      << "]: " << grpc_transport_stream_op_batch_string(batch, false);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
}

RefCountedPtr<ServerConfigSelectorFilterV1::BottomCall>
ServerConfigSelectorFilterV1::BottomCall::Ref() {
  IncrementRefCount();
  return RefCountedPtr<ServerConfigSelectorFilterV1::BottomCall>(this);
}

RefCountedPtr<ServerConfigSelectorFilterV1::BottomCall>
ServerConfigSelectorFilterV1::BottomCall::Ref(
    const DebugLocation& location, const char* reason) {
  IncrementRefCount(location, reason);
  return RefCountedPtr<ServerConfigSelectorFilterV1::BottomCall>(this);
}

void ServerConfigSelectorFilterV1::BottomCall::Unref() {
  GRPC_CALL_STACK_UNREF(call_stack(), "");
}

void ServerConfigSelectorFilterV1::BottomCall::Unref(
    const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_UNREF(call_stack(), reason);
}

void ServerConfigSelectorFilterV1::BottomCall::IncrementRefCount() {
  GRPC_CALL_STACK_REF(call_stack(), "");
}

void ServerConfigSelectorFilterV1::BottomCall::IncrementRefCount(
    const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_REF(call_stack(), reason);
}

}  // namespace grpc_core
