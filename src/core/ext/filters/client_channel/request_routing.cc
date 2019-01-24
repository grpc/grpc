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

#include "src/core/ext/filters/client_channel/request_routing.h"

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
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"
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

  void UpdateState(RefCountedPtr<SubchannelPicker> picker) override {
    parent_->channel_control_helper()->UpdateState(std::move(picker));
  }

  void RequestReresolution() override {
// FIXME: need to hop into combiner here?
    if (parent_->tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: started name re-resolving",
              parent_.get());
    }
    parent_->resolver_->RequestReresolutionLocked();
  }

 private:
  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
};

//
// ResolvingLoadBalancingPolicy::Picker
//

class ResolvingLoadBalancingPolicy::Picker
    : public LoadBalancingPolicy::SubchannelPicker {
 public:
  explicit Picker(RefCountedPtr<ResolvingLoadBalancingPolicy> parent)
      : parent_(std::move(parent)) {}

  PickResult Pick(PickState* pick, grpc_error** error) override {
// FIXME: need to pop into combiner here?
// FIXME: is this check needed?
    if (parent_->resolver_ == nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Disconnected");
      return PICK_TRANSIENT_FAILURE;
    }
    // If we haven't yet started resolving, do so.
    if (!parent_->started_resolving_) {
      parent_->StartResolvingLocked();
    }
    // Tell channel to queue the pick until we create an LB policy and
    // return its picker to the channel.
    return PICK_QUEUE;
  }

 private:
  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
};

//
// ResolvingLoadBalancingPolicy::LbConnectivityWatcher
//

class ResolvingLoadBalancingPolicy::LbConnectivityWatcher {
 public:
  LbConnectivityWatcher(RefCountedPtr<ResolvingLoadBalancingPolicy> parent,
                        grpc_connectivity_state state,
                        LoadBalancingPolicy* lb_policy)
      : parent_(std::move(parent)),
        state_(state),
        lb_policy_(lb_policy) {
    GRPC_CLOSURE_INIT(&on_changed_, &OnLbPolicyStateChangedLocked, this,
                      grpc_combiner_scheduler(parent_->combiner()));
    lb_policy_->NotifyOnStateChangeLocked(&state_, &on_changed_);
  }

 private:
  static void OnLbPolicyStateChangedLocked(void* arg, grpc_error* error) {
    LbConnectivityWatcher* self = static_cast<LbConnectivityWatcher*>(arg);
    // If the notification is not for the current policy, we're stale,
    // so delete ourselves.
    if (self->lb_policy_ != self->parent_->lb_policy_.get()) {
      Delete(self);
      return;
    }
    // Otherwise, process notification.
    if (self->parent_->tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "resolving_lb_policy=%p: lb_policy=%p state changed to %s",
              self->parent_.get(), self->lb_policy_,
              grpc_connectivity_state_name(self->state_));
    }
    self->parent_->SetConnectivityStateLocked(
        self->state_, GRPC_ERROR_REF(error), "lb_changed");
    // If shutting down, terminate watch.
    if (self->state_ == GRPC_CHANNEL_SHUTDOWN) {
      Delete(self);
      return;
    }
    // Renew watch.
    self->lb_policy_->NotifyOnStateChangeLocked(&self->state_,
                                                &self->on_changed_);
  }

  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
  grpc_connectivity_state state_;
  // LB policy address. No ref held, so not safe to dereference unless
  // it happens to match request_router->lb_policy_.
  LoadBalancingPolicy* lb_policy_;
  grpc_closure on_changed_;
};

//
// ResolvingLoadBalancingPolicy
//

ResolvingLoadBalancingPolicy::ResolvingLoadBalancingPolicy(
    Args args, TraceFlag* tracer,
    ProcessResolverResultCallback process_resolver_result,
    void* process_resolver_result_user_data, UniquePtr<char> target_uri,
    UniquePtr<char> child_policy_name, grpc_json* child_lb_config,
    grpc_error** error)
    : LoadBalancingPolicy(std::move(args)),
      tracer_(tracer),
      process_resolver_result_(process_resolver_result),
      process_resolver_result_user_data_(process_resolver_result_user_data),
      target_uri_(std::move(target_uri)),
      child_policy_name_(std::move(child_policy_name)),
      child_lb_config_str_(grpc_json_dump_to_string(child_lb_config, 0)),
      child_lb_config_(grpc_json_parse_string(child_lb_config_str_.get())) {
  GRPC_CLOSURE_INIT(
      &on_resolver_result_changed_,
      &ResolvingLoadBalancingPolicy::OnResolverResultChangedLocked, this,
      grpc_combiner_scheduler(combiner()));
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "request_router");
  grpc_channel_args* new_args = nullptr;
  if (process_resolver_result == nullptr) {
    grpc_arg arg = grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION), 0);
    new_args = grpc_channel_args_copy_and_add(args.args, &arg, 1);
  }
  resolver_ = ResolverRegistry::CreateResolver(
      target_uri_.get(), (new_args == nullptr ? args.args : new_args),
      interested_parties(), combiner());
  grpc_channel_args_destroy(new_args);
  if (resolver_ == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("resolver creation failed");
  }
  // Return our picker to the channel.
  channel_control_helper()->UpdateState(MakeRefCounted<Picker>(Ref()));
}

ResolvingLoadBalancingPolicy::~ResolvingLoadBalancingPolicy() {
  if (resolver_ != nullptr) {
// FIXME: this is probably not true anymore...
    // The only way we can get here is if we never started resolving,
    // because we take a ref to the channel stack when we start
    // resolving and do not release it until the resolver callback is
    // invoked after the resolver shuts down.
    resolver_.reset();
  }
  if (lb_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                     interested_parties());
    lb_policy_.reset();
  }
  grpc_connectivity_state_destroy(&state_tracker_);
  if (child_lb_config_ != nullptr) grpc_json_destroy(child_lb_config_);
}

void ResolvingLoadBalancingPolicy::ShutdownLocked() {
  if (resolver_ != nullptr) {
    SetConnectivityStateLocked(
        GRPC_CHANNEL_SHUTDOWN,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Shutting down"), "disconnect");
    resolver_.reset();
    if (lb_policy_ != nullptr) {
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties());
      lb_policy_.reset();
    }
  }
}

grpc_connectivity_state ResolvingLoadBalancingPolicy::CheckConnectivityLocked(
    grpc_error** connectivity_error) {
  return grpc_connectivity_state_get(&state_tracker_, connectivity_error);
}

void ResolvingLoadBalancingPolicy::NotifyOnStateChangeLocked(
    grpc_connectivity_state* state, grpc_closure* closure) {
  grpc_connectivity_state_notify_on_state_change(&state_tracker_, state,
                                                 closure);
}

void ResolvingLoadBalancingPolicy::ExitIdleLocked() {
  if (lb_policy_ != nullptr) {
    lb_policy_->ExitIdleLocked();
  } else {
    exit_idle_when_lb_policy_arrives_ = true;
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

namespace {

const char* GetChannelConnectivityStateChangeString(
    grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE:
      return "Channel state change to IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "Channel state change to CONNECTING";
    case GRPC_CHANNEL_READY:
      return "Channel state change to READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "Channel state change to TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "Channel state change to SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

}  // namespace

void ResolvingLoadBalancingPolicy::SetConnectivityStateLocked(
    grpc_connectivity_state state, grpc_error* error, const char* reason) {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "resolving_lb=%p: setting connectivity state to %s",
            this, grpc_connectivity_state_name(state));
  }
  if (channelz_node() != nullptr) {
    channelz_node()->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            GetChannelConnectivityStateChangeString(state)));
  }
  grpc_connectivity_state_set(&state_tracker_, state, error, reason);
}

void ResolvingLoadBalancingPolicy::StartResolvingLocked() {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "resolving_lb=%p: starting name resolution", this);
  }
  GPR_ASSERT(!started_resolving_);
  started_resolving_ = true;
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
    SetConnectivityStateLocked(GRPC_CHANNEL_SHUTDOWN,
                               GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                   "Resolver spontaneous shutdown", &error, 1),
                               "resolver_spontaneous_shutdown");
  }
  grpc_channel_args_destroy(resolver_result_);
  resolver_result_ = nullptr;
  GRPC_ERROR_UNREF(error);
  Unref();
}

// Creates a new LB policy, replacing any previous one.
// If the new policy is created successfully, sets *connectivity_state and
// *connectivity_error to its initial connectivity state; otherwise,
// leaves them unchanged.
void ResolvingLoadBalancingPolicy::CreateNewLbPolicyLocked(
    const char* lb_policy_name, grpc_json* lb_config,
    grpc_connectivity_state* connectivity_state,
    grpc_error** connectivity_error, TraceStringVector* trace_strings) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.client_channel_factory = client_channel_factory();
  lb_policy_args.channel_control_helper =
      MakeRefCounted<ResolvingControlHelper>(Ref());
  lb_policy_args.subchannel_pool = subchannel_pool()->Ref();
  lb_policy_args.args = resolver_result_;
  lb_policy_args.lb_config = lb_config;
  OrphanablePtr<LoadBalancingPolicy> new_lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          lb_policy_name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(new_lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "could not create LB policy \"%s\"", lb_policy_name);
    if (channelz_node() != nullptr) {
      char* str;
      gpr_asprintf(&str, "Could not create LB policy \'%s\'", lb_policy_name);
      trace_strings->push_back(str);
    }
  } else {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: created new LB policy \"%s\" (%p)",
              this, lb_policy_name, new_lb_policy.get());
    }
    if (channelz_node() != nullptr) {
      char* str;
      gpr_asprintf(&str, "Created new LB policy \'%s\'", lb_policy_name);
      trace_strings->push_back(str);
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
// FIXME: should we initialize this here or just wait for the LB policy
// to set it via the helper?
    // Get the new LB policy's initial connectivity state and start a
    // connectivity watch.
    GRPC_ERROR_UNREF(*connectivity_error);
    *connectivity_state =
        lb_policy_->CheckConnectivityLocked(connectivity_error);
    if (exit_idle_when_lb_policy_arrives_) {
      lb_policy_->ExitIdleLocked();
      exit_idle_when_lb_policy_arrives_ = false;
    }
    // Create new watcher.  It will delete itself when done.
    New<LbConnectivityWatcher>(Ref(), *connectivity_state, lb_policy_.get());
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
  // Data used to set the channel's connectivity state.
  bool set_connectivity_state = true;
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
  grpc_connectivity_state connectivity_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  grpc_error* connectivity_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("No load balancing policy");
  // resolver_result_ will be null in the case of a transient
  // resolution error.  In that case, we don't have any new result to
  // process, which means that we keep using the previous result (if any).
  if (self->resolver_result_ == nullptr) {
    if (self->tracer_->enabled()) {
      gpr_log(GPR_INFO, "resolving_lb=%p: resolver transient failure", self);
    }
    // Don't override connectivity state if we already have an LB policy.
    if (self->lb_policy_ != nullptr) set_connectivity_state = false;
  } else {
// FIXME: skip a lot of this if we're not fetching service config?
    // Parse the resolver result.
    const char* lb_policy_name = nullptr;
    grpc_json* lb_policy_config = nullptr;
    const bool service_config_changed = self->process_resolver_result_(
        self->process_resolver_result_user_data_, *self->resolver_result_,
        &lb_policy_name, &lb_policy_config);
    GPR_ASSERT(lb_policy_name != nullptr);
    // Check to see if we're already using the right LB policy.
    const bool lb_policy_name_changed =
        self->lb_policy_ == nullptr ||
        strcmp(self->lb_policy_->name(), lb_policy_name) != 0;
    if (self->lb_policy_ != nullptr && !lb_policy_name_changed) {
      // Continue using the same LB policy.  Update with new addresses.
      if (self->tracer_->enabled()) {
        gpr_log(GPR_INFO,
                "resolving_lb=%p: updating existing LB policy \"%s\" (%p)",
                self, lb_policy_name, self->lb_policy_.get());
      }
      self->lb_policy_->UpdateLocked(*self->resolver_result_, lb_policy_config);
      // No need to set the channel's connectivity state; the existing
      // watch on the LB policy will take care of that.
      set_connectivity_state = false;
    } else {
      // Instantiate new LB policy.
      self->CreateNewLbPolicyLocked(lb_policy_name, lb_policy_config,
                                    &connectivity_state, &connectivity_error,
                                    &trace_strings);
    }
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
  // Set the channel's connectivity state if needed.
  if (set_connectivity_state) {
    self->SetConnectivityStateLocked(connectivity_state, connectivity_error,
                                     "resolver_result");
// FIXME: if we are in TRANSIENT_FAILURE due to a resolver failure and
// no LB policy exists, use the helper to return a TransientFailurePicker
  } else {
    GRPC_ERROR_UNREF(connectivity_error);
  }
  self->resolver_->NextLocked(&self->resolver_result_,
                              &self->on_resolver_result_changed_);
}

}  // namespace grpc_core
