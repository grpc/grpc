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
// ServerConfigSelectorFilterV1::DynamicFilterChainBuilder
//

// FilterChainBuilder impl to inject into ServerConfigSelector.
class ServerConfigSelectorFilterV1::DynamicFilterChainBuilder final
    : public FilterChainBuilder {
 public:
  explicit DynamicFilterChainBuilder(const ChannelArgs& channel_args)
      : channel_args_(channel_args) {}

  absl::StatusOr<RefCountedPtr<FilterChain>> Build() override {
    filters_.push_back(
        {&ServerDynamicTerminationFilter::kFilterVtable, nullptr});
    RefCountedPtr<DynamicFilters> dynamic_filters = DynamicFilters::Create(
        GRPC_CLIENT_DYNAMIC, channel_args_, std::move(filters_),
        // FIXME: how do we plumb blackboard here?
        /*blackboard=*/nullptr);
    if (dynamic_filters == nullptr) {
      return absl::InternalError("error constructing dynamic filter stack");
    }
    filters_.clear();
    return dynamic_filters;
  }

 private:
  void AddFilter(const FilterHandle& filter_handle,
                 RefCountedPtr<const FilterConfig> config) override {
    filter_handle.AddToBuilder(&filters_, std::move(config));
  }

  const ChannelArgs channel_args_;
  std::vector<FilterAndConfig> filters_;
};

//
// ServerConfigSelectorFilterV1
//

// FIXME: move this somewhere public, so filters can use it when
// registering themselves
#define GRPC_ARG_BELOW_DYNAMIC_FILTERS "grpc.internal.below_dynamic_filters"

absl::Status ServerConfigSelectorFilterV1::Init(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  GRPC_CHECK(args->is_last);
  GRPC_CHECK(elem->filter == &kFilterVtable);
  // Get ConfigSelectorProvider from channel args.
  ServerConfigSelectorProvider* server_config_selector_provider =
      args->channel_args.GetObject<ServerConfigSelectorProvider>();
  if (server_config_selector_provider == nullptr) {
    return absl::UnknownError("No ServerConfigSelectorProvider object found");
  }
  // Build bottom channel stack.
  auto bottom_stack = DynamicFilters::Create(
      GRPC_SERVER_CHANNEL,
      args->channel_args.SetInt(GRPC_ARG_BELOW_DYNAMIC_FILTERS, 1),
      /*filters=*/{}, /*blackboard=*/nullptr);
  // Instantiate filter.
  new (elem->channel_data) ServerConfigSelectorFilterV1(
      args->channel_args, server_config_selector_provider->Ref(),
      std::move(bottom_stack));
  return absl::OkStatus();
}

ServerConfigSelectorFilterV1::ServerConfigSelectorFilterV1(
    const ChannelArgs& args,
    RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider,
    RefCountedPtr<const DynamicFilters> bottom_stack)
    : args_(args),
      server_config_selector_provider_(
          std::move(server_config_selector_provider)),
      bottom_stack_(std::move(bottom_stack)) {
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

void ServerConfigSelectorFilterV1::StartTransportOp(grpc_transport_op* op) {
  // Propagate straight down to the bottom stack.
  // We don't bother sending this through the dynamic stack, because (a)
  // we don't know which dynamic stack to use, and (b) none of the
  // dynamic filters need to see transport ops anyway.
  grpc_channel_element* elem =
      grpc_channel_stack_element(bottom_stack_->channel_stack(), 0);
  elem->filter->start_transport_op(elem, op);
}

void ServerConfigSelectorFilterV1::BuildDynamicFilterChains(
    ServerConfigSelector& config_selector) {
  DynamicFilterChainBuilder builder(args_.SetObject(this));
  config_selector.BuildFilterChains(builder);
}

absl::StatusOr<RefCountedPtr<ServerConfigSelector>>
ServerConfigSelectorFilterV1::GetConfigSelector() {
  MutexLock lock(&mu_);
  return config_selector_.value();
}

RefCountedPtr<DynamicFilters::Call>
ServerConfigSelectorFilterV1::MakeBottomCall(const grpc_call_element_args& args,
                                             absl::Status* error) {
  DynamicFilters::Call::Args call_args = {
      bottom_stack_,       args.server_transport_data,
      /*pollent=*/nullptr, args.start_time,
      args.deadline,       args.arena,
      args.call_combiner};
  return bottom_stack_->CreateCall(std::move(call_args), error);
}

const grpc_channel_filter ServerConfigSelectorFilterV1::kFilterVtable = {
    // start_transport_stream_op_batch
    [](grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
      auto* chand =
          static_cast<ServerConfigSelectorFilterV1*>(elem->channel_data);
      auto* calld =
          static_cast<ServerConfigSelectorFilterV1::Call*>(elem->call_data);
      calld->StartTransportStreamOpBatch(chand, batch);
    },
    // start_transport_op
    [](grpc_channel_element* elem, grpc_transport_op* op) {
      auto* chand =
          static_cast<ServerConfigSelectorFilterV1*>(elem->channel_data);
      chand->StartTransportOp(op);
    },
    // sizeof_call_data
    sizeof(ServerConfigSelectorFilterV1::Call),
    // init_call_elem
    [](grpc_call_element* elem, const grpc_call_element_args* args) {
      auto* chand =
          static_cast<ServerConfigSelectorFilterV1*>(elem->channel_data);
      auto* calld =
          static_cast<ServerConfigSelectorFilterV1::Call*>(elem->call_data);
      new (calld) ServerConfigSelectorFilterV1::Call(args->call_combiner);
      return calld->Init(chand, args);
    },
    // set_pollset_or_pollset_set
    [](grpc_call_element* elem, grpc_polling_entity* pollent) {},
    // destroy_call_elem
    [](grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
       grpc_closure* then_schedule_closure) {
      auto* calld =
          static_cast<ServerConfigSelectorFilterV1::Call*>(elem->call_data);
      calld->Destroy(then_schedule_closure);
    },
    // sizeof_channel_data
    sizeof(ServerConfigSelectorFilterV1),
    // init_channel_elem
    ServerConfigSelectorFilterV1::Init,
    // post_init_channel_elem
    [](grpc_channel_stack* stk, grpc_channel_element* elem) {},
    // destroy_channel_elem
    [](grpc_channel_element* elem) {
      auto* chand =
          static_cast<ServerConfigSelectorFilterV1*>(elem->channel_data);
      chand->~ServerConfigSelectorFilterV1();
    },
    // get_channel_info
    [](grpc_channel_element* elem, const grpc_channel_info* channel_info) {},
    // name
    GRPC_UNIQUE_TYPE_NAME_HERE("server_config_selector_v1"),
};

//
// ServerConfigSelectorFilterV1::Call
//

ServerConfigSelectorFilterV1::Call::Call(CallCombiner* call_combiner)
    : buffered_call_(call_combiner,
                     &grpc_server_config_selector_filter_call_trace) {}

absl::Status ServerConfigSelectorFilterV1::Call::Init(
    ServerConfigSelectorFilterV1* filter, const grpc_call_element_args* args) {
  owning_call_ = args->call_stack;
  server_transport_data_ = args->server_transport_data;
  call_start_time_ = args->start_time;
  deadline_ = args->deadline;
  arena_ = args->arena;
  call_combiner_ = args->call_combiner;
  return absl::OkStatus();
}

void ServerConfigSelectorFilterV1::Call::Destroy(
    grpc_closure* then_schedule_closure) {
  RefCountedPtr<DynamicFilters::Call> dynamic_call = std::move(dynamic_call_);
  this->~Call();
  if (dynamic_call != nullptr) {
    dynamic_call->SetAfterCallStackDestroy(then_schedule_closure);
  } else {
    ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, absl::OkStatus());
  }
}

void ServerConfigSelectorFilterV1::Call::StartTransportStreamOpBatch(
    ServerConfigSelectorFilterV1* filter,
    grpc_transport_stream_op_batch* batch) {
  GRPC_LATENT_SEE_SCOPE(
      "ServerConfigSelectorFilterV1::Call::StartTransportStreamOpBatch");
  if (GRPC_TRACE_FLAG_ENABLED(server_config_selector_filter_call) &&
      !GRPC_TRACE_FLAG_ENABLED(channel)) {
    LOG(INFO) << "chand=" << filter << " calld=" << this
              << ": batch started from above: "
              << grpc_transport_stream_op_batch_string(batch, false);
  }
  // If we already have a dynamic call, pass the batch down to it.
  // Note that once we have done so, we do not need to acquire the channel's
  // resolution mutex, which is more efficient (especially for streaming calls).
  if (dynamic_call_ != nullptr) {
    GRPC_TRACE_LOG(server_config_selector_filter_call, INFO)
        << "chand=" << filter << " calld=" << this
        << ": starting batch on dynamic_call=" << dynamic_call_.get();
    dynamic_call_->StartTransportStreamOpBatch(batch);
    return;
  }
  // We do not yet have a dynamic call.
  //
  // If we've previously been cancelled, immediately fail any new batches.
  if (GPR_UNLIKELY(!cancel_error_.ok())) {
    GRPC_TRACE_LOG(server_config_selector_filter_call, INFO)
        << "chand=" << filter << " calld=" << this
        << ": failing batch with error: " << StatusToString(cancel_error_);
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(batch, cancel_error_,
                                                       call_combiner_);
    return;
  }
  // Handle cancellation.
  if (GPR_UNLIKELY(batch->cancel_stream)) {
    // Stash a copy of cancel_error in our call data, so that we can use
    // it for subsequent operations.  This ensures that if the call is
    // cancelled before any batches are passed down (e.g., if the deadline
    // is in the past when the call starts), we can return the right
    // error to the caller when the first batch does get passed down.
    cancel_error_ = batch->payload->cancel_stream.cancel_error;
    GRPC_TRACE_LOG(server_config_selector_filter_call, INFO)
        << "chand=" << filter << " calld=" << this
        << ": recording cancel_error=" << StatusToString(cancel_error_);
    // Fail all pending batches.
    buffered_call_.Fail(cancel_error_, BufferedCall::NoYieldCallCombiner);
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(batch, cancel_error_,
                                                       call_combiner_);
    return;
  }
  // Add the batch to the pending list.
  buffered_call_.EnqueueBatch(batch);
  // For batches containing a send_initial_metadata op, acquire the
  // channel's resolution mutex to apply the service config to the call,
  // after which we will create a dynamic call.
  if (GPR_LIKELY(batch->send_initial_metadata)) {
    GRPC_TRACE_LOG(server_config_selector_filter_call, INFO)
        << "chand=" << filter << " calld=" << this
        << ": grabbing ServerConfigSelector to find dynamic filter stack";
    // Get the config selector.
    auto config_selector = filter->GetConfigSelector();
    if (!config_selector.ok()) {
      buffered_call_.Fail(config_selector.status(),
                          BufferedCall::YieldCallCombiner);
      return;
    }
    // Use config selector to choose dynamic filter stack.
    auto call_config = (*config_selector)->GetCallConfig(&md);
    if (!call_config.ok()) {
      buffered_call_.Fail(call_config.status(),
                          BufferedCall::YieldCallCombiner);
      return;
    }
    // Create call object on dynamic filter stack.
    auto dynamic_filters =
        call_config->filter_chain.TakeAsSubclass<const DynamicFilters>();
    DynamicFilters::Call::Args args = {
        dynamic_filters,     server_transport_data_,
        /*pollent=*/nullptr, call_start_time_,       deadline_, arena_,
        call_combiner_};
    absl::Status error;
    dynamic_call_ = dynamic_filters->CreateCall(std::move(args), &error);
    if (!error.ok()) {
      buffered_call_.Fail(error, BufferedCall::YieldCallCombiner);
      return;
    }
    buffered_call_.Resume(
        [dynamic_call = dynamic_call_](grpc_transport_stream_op_batch* batch) {
          dynamic_call->StartTransportStreamOpBatch(batch);
        });
  } else {
    // For all other batches, release the call combiner.
    GRPC_TRACE_LOG(server_config_selector_filter_call, INFO)
        << "chand=" << filter << " calld=" << this
        << ": saved batch, yielding call combiner";
    GRPC_CALL_COMBINER_STOP(call_combiner_,
                            "batch does not include send_initial_metadata");
  }
}

//
// ServerConfigSelectorFilterV1::ServerDynamicTerminationFilter
//

class ServerConfigSelectorFilterV1::ServerDynamicTerminationFilter {
 public:
  static const grpc_channel_filter kFilterVtable;

 private:
  class Call {
   public:
    explicit Call(RefCountedPtr<DynamicFilters::Call> bottom_call_)
        : bottom_call_(std::move(bottom_call)) {}

    void Destroy(grpc_closure* then_schedule_closure) {
      RefCountedPtr<DynamicFilters::Call> bottom_call = std::move(bottom_call_);
      this->~Call();
      if (bottom_call != nullptr) {
        bottom_call->SetAfterCallStackDestroy(then_schedule_closure);
      } else {
        ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, absl::OkStatus());
      }
    }

    void StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch) {
      bottom_call_->StartTransportStreamOpBatch(batch);
    }

   private:
    ~Call() = default;

    RefCountedPtr<DynamicFilters::Call> bottom_call_;
  };

  explicit ServerDynamicTerminationFilter(const ChannelArgs& args)
      : config_selector_filter_(
            args.GetObject<ServerConfigSelectorFilterV1>()) {}

  ServerConfigSelectorFilterV1* config_selector_filter_;
};

const grpc_channel_filter ServerConfigSelectorFilterV1::
    ServerDynamicTerminationFilter::kFilterVtable = {
        // start_transport_stream_op_batch
        [](grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
          auto* calld = static_cast<ServerDynamicTerminationFilter::Call*>(
              elem->call_data);
          calld->StartTransportStreamOpBatch(batch);
        },
        // start_transport_op
        [](grpc_channel_element* elem, grpc_transport_op* op) {
          auto* chand =
              static_cast<ServerDynamicTerminationFilter*>(elem->channel_data);
          chand->StartTransportOp(op);
        },
        // sizeof_call_data
        sizeof(ServerDynamicTerminationFilter::Call),
        // init_call_elem
        [](grpc_call_element* elem, const grpc_call_element_args* args) {
          auto* chand =
              static_cast<ServerDynamicTerminationFilter*>(elem->channel_data);
          auto* calld = static_cast<ServerDynamicTerminationFilter::Call*>(
              elem->call_data);
          absl::Status error;
          new (calld) Call(chand->MakeBottomCall(*args, &error));
          return error;
        },
        // set_pollset_or_pollset_set
        [](grpc_call_element* elem, grpc_polling_entity* pollent) {},
        // destroy_call_elem
        [](grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
           grpc_closure* then_schedule_closure) {
          auto* calld = static_cast<ServerDynamicTerminationFilter::Call*>(
              elem->call_data);
          calld->Destroy(then_schedule_closure);
        },
        // sizeof_channel_data
        sizeof(ServerDynamicTerminationFilter),
        // init_channel_elem
        [](grpc_channel_element* elem, grpc_channel_element_args* args) {
          GRPC_CHECK(args->is_last);
          GRPC_CHECK(elem->filter ==
                     &ServerDynamicTerminationFilter::kFilterVtable);
          new (elem->channel_data)
              ServerDynamicTerminationFilter(args->channel_args);
          return absl::OkStatus();
        },
        // post_init_channel_elem
        [](grpc_channel_stack* stk, grpc_channel_element* elem) {},
        // destroy_channel_elem
        [](grpc_channel_element* elem) {
          auto* chand =
              static_cast<ServerDynamicTerminationFilter*>(elem->channel_data);
          chand->~ServerDynamicTerminationFilter();
        },
        // get_channel_info
        [](grpc_channel_element* elem, const grpc_channel_info* channel_info) {
        },
        // name
        GRPC_UNIQUE_TYPE_NAME_HERE("server_dynamic_termination_filter"),
};

}  // namespace grpc_core
