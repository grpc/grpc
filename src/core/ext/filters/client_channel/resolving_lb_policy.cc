/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/resolving_lb_policy.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/service_config.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

namespace grpc_core {

//
// ResolvingLoadBalancingPolicy::ResolvingControlHelper
//

class ResolvingLoadBalancingPolicy::ResolvingControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ResolvingControlHelper(
      RefCountedPtr<ResolvingLoadBalancingPolicy> parent)
      : parent_(std::move(parent)) {}

  Subchannel* CreateSubchannel(const grpc_channel_args& args) override {
    if (parent_->resolver_ == nullptr) return nullptr;  // Shutting down.
    return parent_->channel_control_helper()->CreateSubchannel(args);
  }

  grpc_channel* CreateChannel(const char* target, grpc_client_channel_type type,
                              const grpc_channel_args& args) override {
    if (parent_->resolver_ == nullptr) return nullptr;  // Shutting down.
    return parent_->channel_control_helper()->CreateChannel(target, type, args);
  }

  void UpdateState(grpc_connectivity_state state, grpc_error* state_error,
                   UniquePtr<SubchannelPicker> picker) override {
    if (parent_->resolver_ == nullptr) {
      // shutting down.
      GRPC_ERROR_UNREF(state_error);
      return;
    }
    parent_->channel_control_helper()->UpdateState(state, state_error,
                                                   std::move(picker));
  }

  void RequestReresolution() override {
    if (parent_->tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: started name re-resolving",
              parent_.get());
    }
    if (parent_->resolver_ != nullptr) {
      parent_->resolver_->RequestReresolutionLocked();
    }
  }

 private:
  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
};

//
// ResolvingLoadBalancingPolicy
//

ResolvingLoadBalancingPolicy::ResolvingLoadBalancingPolicy(
    Args args, TraceFlag* tracer, UniquePtr<char> target_uri,
    UniquePtr<char> child_policy_name, RefCountedPtr<Config> child_lb_config,
    grpc_error** error)
    : LoadBalancingPolicy(std::move(args)),
      tracer_(tracer),
      target_uri_(std::move(target_uri)),
      child_policy_name_(std::move(child_policy_name)),
      child_lb_config_(std::move(child_lb_config)) {
  GPR_ASSERT(child_policy_name_ != nullptr);
  // Don't fetch service config, since this ctor is for use in nested LB
  // policies, not at the top level, and we only fetch the service
  // config at the top level.
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION), 0);
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add(args.args, &arg, 1);
  *error = Init(*new_args);
  grpc_channel_args_destroy(new_args);
}

ResolvingLoadBalancingPolicy::ResolvingLoadBalancingPolicy(
    Args args, TraceFlag* tracer, UniquePtr<char> target_uri,
    ProcessResolverResultCallback process_resolver_result,
    void* process_resolver_result_user_data, grpc_error** error)
    : LoadBalancingPolicy(std::move(args)),
      tracer_(tracer),
      target_uri_(std::move(target_uri)),
      process_resolver_result_(process_resolver_result),
      process_resolver_result_user_data_(process_resolver_result_user_data) {
  GPR_ASSERT(process_resolver_result != nullptr);
  *error = Init(*args.args);
}

grpc_error* ResolvingLoadBalancingPolicy::Init(const grpc_channel_args& args) {
  GRPC_CLOSURE_INIT(
      &on_resolver_result_changed_,
      &ResolvingLoadBalancingPolicy::OnResolverResultChangedLocked, this,
      grpc_combiner_scheduler(combiner()));
  resolver_ = ResolverRegistry::CreateResolver(
      target_uri_.get(), &args, interested_parties(), combiner());
  if (resolver_ == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("resolver creation failed");
  }
  // Return our picker to the channel.
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_IDLE, GRPC_ERROR_NONE,
      UniquePtr<SubchannelPicker>(New<QueuePicker>(Ref())));
  return GRPC_ERROR_NONE;
}

ResolvingLoadBalancingPolicy::~ResolvingLoadBalancingPolicy() {
  GPR_ASSERT(resolver_ == nullptr);
  GPR_ASSERT(lb_policy_ == nullptr);
}

void ResolvingLoadBalancingPolicy::ShutdownLocked() {
  if (resolver_ != nullptr) {
    resolver_.reset();
    if (lb_policy_ != nullptr) {
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties());
      lb_policy_.reset();
    }
  }
}

void ResolvingLoadBalancingPolicy::ExitIdleLocked() {
  if (lb_policy_ != nullptr) {
    lb_policy_->ExitIdleLocked();
  } else {
    if (!started_resolving_ && resolver_ != nullptr) {
      StartResolvingLocked();
    }
  }
}

void ResolvingLoadBalancingPolicy::ResetBackoffLocked() {
  if (resolver_ != nullptr) {
    resolver_->ResetBackoffLocked();
    resolver_->RequestReresolutionLocked();
  }
  if (lb_policy_ != nullptr) {
    lb_policy_->ResetBackoffLocked();
  }
}

void ResolvingLoadBalancingPolicy::FillChildRefsForChannelz(
    channelz::ChildRefsList* child_subchannels,
    channelz::ChildRefsList* child_channels) {
  if (lb_policy_ != nullptr) {
    lb_policy_->FillChildRefsForChannelz(child_subchannels, child_channels);
  }
}

void ResolvingLoadBalancingPolicy::StartResolvingLocked() {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "resolving_lb=%p: starting name resolution", this);
  }
  GPR_ASSERT(!started_resolving_);
  started_resolving_ = true;
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE,
      UniquePtr<SubchannelPicker>(New<QueuePicker>(Ref())));
  Ref().release();
  resolver_->NextLocked(&resolver_result_, &on_resolver_result_changed_);
}

// Invoked from the resolver NextLocked() callback when the resolver
// is shutting down.
void ResolvingLoadBalancingPolicy::OnResolverShutdownLocked(grpc_error* error) {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "resolving_lb=%p: shutting down", this);
  }
  if (lb_policy_ != nullptr) {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: shutting down lb_policy=%p", this,
              lb_policy_.get());
    }
    grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                     interested_parties());
    lb_policy_.reset();
  }
  if (resolver_ != nullptr) {
    // This should never happen; it can only be triggered by a resolver
    // implementation spotaneously deciding to report shutdown without
    // being orphaned.  This code is included just to be defensive.
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "resolving_lb=%p: spontaneous shutdown from resolver %p", this,
              resolver_.get());
    }
    resolver_.reset();
    grpc_error* error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Resolver spontaneous shutdown", &error, 1);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_SHUTDOWN, GRPC_ERROR_REF(error),
        UniquePtr<SubchannelPicker>(New<TransientFailurePicker>(error)));
  }
  grpc_channel_args_destroy(resolver_result_);
  resolver_result_ = nullptr;
  GRPC_ERROR_UNREF(error);
  Unref();
}

// Creates a new LB policy, replacing any previous one.
// Updates trace_strings to indicate what was done.
void ResolvingLoadBalancingPolicy::CreateNewLbPolicyLocked(
    const char* lb_policy_name, TraceStringVector* trace_strings) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.channel_control_helper =
      UniquePtr<ChannelControlHelper>(New<ResolvingControlHelper>(Ref()));
  lb_policy_args.args = resolver_result_;
  OrphanablePtr<LoadBalancingPolicy> new_lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          lb_policy_name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(new_lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "could not create LB policy \"%s\"", lb_policy_name);
    if (channelz_node() != nullptr) {
      char* str;
      gpr_asprintf(&str, "Could not create LB policy \"%s\"", lb_policy_name);
      trace_strings->push_back(str);
    }
  } else {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: created new LB policy \"%s\" (%p)",
              this, lb_policy_name, new_lb_policy.get());
    }
    if (channelz_node() != nullptr) {
      char* str;
      gpr_asprintf(&str, "Created new LB policy \"%s\"", lb_policy_name);
      trace_strings->push_back(str);
    }
    // Propagate channelz node.
    auto* channelz = channelz_node();
    if (channelz != nullptr) {
      new_lb_policy->set_channelz_node(channelz->Ref());
    }
    // Swap out the LB policy and update the fds in interested_parties_.
    if (lb_policy_ != nullptr) {
      if (tracer_->enabled()) {
        gpr_log(GPR_INFO, "resolving_lb=%p: shutting down lb_policy=%p", this,
                lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties());
    }
    lb_policy_ = std::move(new_lb_policy);
    grpc_pollset_set_add_pollset_set(lb_policy_->interested_parties(),
                                     interested_parties());
  }
}

void ResolvingLoadBalancingPolicy::MaybeAddTraceMessagesForAddressChangesLocked(
    TraceStringVector* trace_strings) {
  const ServerAddressList* addresses =
      FindServerAddressListChannelArg(resolver_result_);
  const bool resolution_contains_addresses =
      addresses != nullptr && addresses->size() > 0;
  if (!resolution_contains_addresses &&
      previous_resolution_contained_addresses_) {
    trace_strings->push_back(gpr_strdup("Address list became empty"));
  } else if (resolution_contains_addresses &&
             !previous_resolution_contained_addresses_) {
    trace_strings->push_back(gpr_strdup("Address list became non-empty"));
  }
  previous_resolution_contained_addresses_ = resolution_contains_addresses;
}

void ResolvingLoadBalancingPolicy::ConcatenateAndAddChannelTraceLocked(
    TraceStringVector* trace_strings) const {
  if (!trace_strings->empty()) {
    gpr_strvec v;
    gpr_strvec_init(&v);
    gpr_strvec_add(&v, gpr_strdup("Resolution event: "));
    bool is_first = 1;
    for (size_t i = 0; i < trace_strings->size(); ++i) {
      if (!is_first) gpr_strvec_add(&v, gpr_strdup(", "));
      is_first = false;
      gpr_strvec_add(&v, (*trace_strings)[i]);
    }
    char* flat;
    size_t flat_len = 0;
    flat = gpr_strvec_flatten(&v, &flat_len);
    channelz_node()->AddTraceEvent(channelz::ChannelTrace::Severity::Info,
                                   grpc_slice_new(flat, flat_len, gpr_free));
    gpr_strvec_destroy(&v);
  }
}

// Callback invoked when a resolver result is available.
void ResolvingLoadBalancingPolicy::OnResolverResultChangedLocked(
    void* arg, grpc_error* error) {
  auto* self = static_cast<ResolvingLoadBalancingPolicy*>(arg);
  if (self->tracer_->enabled()) {
    const char* disposition =
        self->resolver_result_ != nullptr
            ? ""
            : (error == GRPC_ERROR_NONE ? " (transient error)"
                                        : " (resolver shutdown)");
    gpr_log(GPR_INFO,
            "resolving_lb=%p: got resolver result: resolver_result=%p "
            "error=%s%s",
            self, self->resolver_result_, grpc_error_string(error),
            disposition);
  }
  // Handle shutdown.
  if (error != GRPC_ERROR_NONE || self->resolver_ == nullptr) {
    self->OnResolverShutdownLocked(GRPC_ERROR_REF(error));
    return;
  }
  // We only want to trace the address resolution in the follow cases:
  // (a) Address resolution resulted in service config change.
  // (b) Address resolution that causes number of backends to go from
  //     zero to non-zero.
  // (c) Address resolution that causes number of backends to go from
  //     non-zero to zero.
  // (d) Address resolution that causes a new LB policy to be created.
  //
  // we track a list of strings to eventually be concatenated and traced.
  TraceStringVector trace_strings;
  // resolver_result_ will be null in the case of a transient
  // resolution error.  In that case, we don't have any new result to
  // process, which means that we keep using the previous result (if any).
  if (self->resolver_result_ == nullptr) {
    if (self->tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: resolver transient failure", self);
    }
    // If we already have an LB policy from a previous resolution
    // result, then we continue to let it set the connectivity state.
    // Otherwise, we go into TRANSIENT_FAILURE.
    if (self->lb_policy_ == nullptr) {
      // TODO(roth): When we change the resolver API to be able to
      // return transient errors in a cleaner way, we should make it the
      // resolver's responsibility to attach a status to the error,
      // rather than doing it centrally here.
      grpc_error* state_error = grpc_error_set_int(
          GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
              "Resolver transient failure", &error, 1),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
      self->channel_control_helper()->UpdateState(
          GRPC_CHANNEL_TRANSIENT_FAILURE, GRPC_ERROR_REF(state_error),
          UniquePtr<SubchannelPicker>(
              New<TransientFailurePicker>(state_error)));
    }
  } else {
    // Parse the resolver result.
    const char* lb_policy_name = nullptr;
    RefCountedPtr<Config> lb_policy_config;
    bool service_config_changed = false;
    if (self->process_resolver_result_ != nullptr) {
      service_config_changed = self->process_resolver_result_(
          self->process_resolver_result_user_data_, *self->resolver_result_,
          &lb_policy_name, &lb_policy_config);
    } else {
      lb_policy_name = self->child_policy_name_.get();
      lb_policy_config = self->child_lb_config_;
    }
    GPR_ASSERT(lb_policy_name != nullptr);
    // If we're not already using the right LB policy name, instantiate
    // a new one.
    if (self->lb_policy_ == nullptr ||
        strcmp(self->lb_policy_->name(), lb_policy_name) != 0) {
      if (self->tracer_->enabled()) {
        gpr_log(GPR_INFO, "resolving_lb=%p: creating new LB policy \"%s\"",
                self, lb_policy_name);
      }
      self->CreateNewLbPolicyLocked(lb_policy_name, &trace_strings);
    }
    // Update the LB policy with the new addresses and config.
    if (self->tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: updating LB policy \"%s\" (%p)", self,
              lb_policy_name, self->lb_policy_.get());
    }
    self->lb_policy_->UpdateLocked(*self->resolver_result_,
                                   std::move(lb_policy_config));
    // Add channel trace event.
    if (self->channelz_node() != nullptr) {
      if (service_config_changed) {
        // TODO(ncteisen): might be worth somehow including a snippet of the
        // config in the trace, at the risk of bloating the trace logs.
        trace_strings.push_back(gpr_strdup("Service config changed"));
      }
      self->MaybeAddTraceMessagesForAddressChangesLocked(&trace_strings);
      self->ConcatenateAndAddChannelTraceLocked(&trace_strings);
    }
    // Clean up.
    grpc_channel_args_destroy(self->resolver_result_);
    self->resolver_result_ = nullptr;
  }
  // Renew resolver callback.
  self->resolver_->NextLocked(&self->resolver_result_,
                              &self->on_resolver_result_changed_);
}

}  // namespace grpc_core
