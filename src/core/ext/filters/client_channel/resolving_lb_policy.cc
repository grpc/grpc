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
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/sync.h"
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
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

namespace grpc_core {

//
// ResolvingLoadBalancingPolicy::ResolverResultHandler
//

class ResolvingLoadBalancingPolicy::ResolverResultHandler
    : public Resolver::ResultHandler {
 public:
  explicit ResolverResultHandler(
      RefCountedPtr<ResolvingLoadBalancingPolicy> parent)
      : parent_(std::move(parent)) {}

  ~ResolverResultHandler() {
    if (GRPC_TRACE_FLAG_ENABLED(*(parent_->tracer_))) {
      gpr_log(GPR_INFO, "resolving_lb=%p: resolver shutdown complete",
              parent_.get());
    }
  }

  void ReturnResult(Resolver::Result result) override {
    parent_->OnResolverResultChangedLocked(std::move(result));
  }

  void ReturnError(grpc_error* error) override {
    parent_->OnResolverError(error);
  }

 private:
  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
};

//
// ResolvingLoadBalancingPolicy::ResolvingControlHelper
//

class ResolvingLoadBalancingPolicy::ResolvingControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ResolvingControlHelper(
      RefCountedPtr<ResolvingLoadBalancingPolicy> parent)
      : parent_(std::move(parent)) {}

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      const grpc_channel_args& args) override {
    if (parent_->resolver_ == nullptr) return nullptr;  // Shutting down.
    if (!CalledByCurrentChild() && !CalledByPendingChild()) return nullptr;
    return parent_->channel_control_helper()->CreateSubchannel(args);
  }

  void UpdateState(grpc_connectivity_state state,
                   std::unique_ptr<SubchannelPicker> picker) override {
    if (parent_->resolver_ == nullptr) return;  // Shutting down.
    // If this request is from the pending child policy, ignore it until
    // it reports READY, at which point we swap it into place.
    if (CalledByPendingChild()) {
      if (GRPC_TRACE_FLAG_ENABLED(*(parent_->tracer_))) {
        gpr_log(GPR_INFO,
                "resolving_lb=%p helper=%p: pending child policy %p reports "
                "state=%s",
                parent_.get(), this, child_, ConnectivityStateName(state));
      }
      if (state != GRPC_CHANNEL_READY) return;
      grpc_pollset_set_del_pollset_set(
          parent_->lb_policy_->interested_parties(),
          parent_->interested_parties());
      parent_->lb_policy_ = std::move(parent_->pending_lb_policy_);
    } else if (!CalledByCurrentChild()) {
      // This request is from an outdated child, so ignore it.
      return;
    }
    parent_->channel_control_helper()->UpdateState(state, std::move(picker));
  }

  void RequestReresolution() override {
    // If there is a pending child policy, ignore re-resolution requests
    // from the current child policy (or any outdated child).
    if (parent_->pending_lb_policy_ != nullptr && !CalledByPendingChild()) {
      return;
    }
    if (GRPC_TRACE_FLAG_ENABLED(*(parent_->tracer_))) {
      gpr_log(GPR_INFO, "resolving_lb=%p: started name re-resolving",
              parent_.get());
    }
    if (parent_->resolver_ != nullptr) {
      parent_->resolver_->RequestReresolutionLocked();
    }
  }

  void AddTraceEvent(TraceSeverity /*severity*/,
                     StringView /*message*/) override {}

  void set_child(LoadBalancingPolicy* child) { child_ = child; }

 private:
  bool CalledByPendingChild() const {
    GPR_ASSERT(child_ != nullptr);
    return child_ == parent_->pending_lb_policy_.get();
  }

  bool CalledByCurrentChild() const {
    GPR_ASSERT(child_ != nullptr);
    return child_ == parent_->lb_policy_.get();
  };

  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
  LoadBalancingPolicy* child_ = nullptr;
};

//
// ResolvingLoadBalancingPolicy
//

ResolvingLoadBalancingPolicy::ResolvingLoadBalancingPolicy(
    Args args, TraceFlag* tracer, grpc_core::UniquePtr<char> target_uri,
    ProcessResolverResultCallback process_resolver_result,
    void* process_resolver_result_user_data)
    : LoadBalancingPolicy(std::move(args)),
      tracer_(tracer),
      target_uri_(std::move(target_uri)),
      process_resolver_result_(process_resolver_result),
      process_resolver_result_user_data_(process_resolver_result_user_data) {
  GPR_ASSERT(process_resolver_result != nullptr);
  resolver_ = ResolverRegistry::CreateResolver(
      target_uri_.get(), args.args, interested_parties(), combiner(),
      grpc_core::MakeUnique<ResolverResultHandler>(Ref()));
  // Since the validity of args has been checked when create the channel,
  // CreateResolver() must return a non-null result.
  GPR_ASSERT(resolver_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: starting name resolution", this);
  }
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_CONNECTING, grpc_core::MakeUnique<QueuePicker>(Ref()));
  resolver_->StartLocked();
}

ResolvingLoadBalancingPolicy::~ResolvingLoadBalancingPolicy() {
  GPR_ASSERT(resolver_ == nullptr);
  GPR_ASSERT(lb_policy_ == nullptr);
}

void ResolvingLoadBalancingPolicy::ShutdownLocked() {
  if (resolver_ != nullptr) {
    resolver_.reset();
    if (lb_policy_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
        gpr_log(GPR_INFO, "resolving_lb=%p: shutting down lb_policy=%p", this,
                lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties());
      lb_policy_.reset();
    }
    if (pending_lb_policy_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
        gpr_log(GPR_INFO, "resolving_lb=%p: shutting down pending lb_policy=%p",
                this, pending_lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(pending_lb_policy_->interested_parties(),
                                       interested_parties());
      pending_lb_policy_.reset();
    }
  }
}

void ResolvingLoadBalancingPolicy::ExitIdleLocked() {
  if (lb_policy_ != nullptr) {
    lb_policy_->ExitIdleLocked();
    if (pending_lb_policy_ != nullptr) pending_lb_policy_->ExitIdleLocked();
  }
}

void ResolvingLoadBalancingPolicy::ResetBackoffLocked() {
  if (resolver_ != nullptr) {
    resolver_->ResetBackoffLocked();
    resolver_->RequestReresolutionLocked();
  }
  if (lb_policy_ != nullptr) lb_policy_->ResetBackoffLocked();
  if (pending_lb_policy_ != nullptr) pending_lb_policy_->ResetBackoffLocked();
}

void ResolvingLoadBalancingPolicy::OnResolverError(grpc_error* error) {
  if (resolver_ == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: resolver transient failure: %s", this,
            grpc_error_string(error));
  }
  // If we already have an LB policy from a previous resolution
  // result, then we continue to let it set the connectivity state.
  // Otherwise, we go into TRANSIENT_FAILURE.
  if (lb_policy_ == nullptr) {
    grpc_error* state_error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Resolver transient failure", &error, 1);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        grpc_core::MakeUnique<TransientFailurePicker>(state_error));
  }
  GRPC_ERROR_UNREF(error);
}

void ResolvingLoadBalancingPolicy::CreateOrUpdateLbPolicyLocked(
    const char* lb_policy_name,
    RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config,
    Resolver::Result result, TraceStringVector* trace_strings) {
  // If the child policy name changes, we need to create a new child
  // policy.  When this happens, we leave child_policy_ as-is and store
  // the new child policy in pending_child_policy_.  Once the new child
  // policy transitions into state READY, we swap it into child_policy_,
  // replacing the original child policy.  So pending_child_policy_ is
  // non-null only between when we apply an update that changes the child
  // policy name and when the new child reports state READY.
  //
  // Updates can arrive at any point during this transition.  We always
  // apply updates relative to the most recently created child policy,
  // even if the most recent one is still in pending_child_policy_.  This
  // is true both when applying the updates to an existing child policy
  // and when determining whether we need to create a new policy.
  //
  // As a result of this, there are several cases to consider here:
  //
  // 1. We have no existing child policy (i.e., we have started up but
  //    have not yet received a serverlist from the balancer or gone
  //    into fallback mode; in this case, both child_policy_ and
  //    pending_child_policy_ are null).  In this case, we create a
  //    new child policy and store it in child_policy_.
  //
  // 2. We have an existing child policy and have no pending child policy
  //    from a previous update (i.e., either there has not been a
  //    previous update that changed the policy name, or we have already
  //    finished swapping in the new policy; in this case, child_policy_
  //    is non-null but pending_child_policy_ is null).  In this case:
  //    a. If child_policy_->name() equals child_policy_name, then we
  //       update the existing child policy.
  //    b. If child_policy_->name() does not equal child_policy_name,
  //       we create a new policy.  The policy will be stored in
  //       pending_child_policy_ and will later be swapped into
  //       child_policy_ by the helper when the new child transitions
  //       into state READY.
  //
  // 3. We have an existing child policy and have a pending child policy
  //    from a previous update (i.e., a previous update set
  //    pending_child_policy_ as per case 2b above and that policy has
  //    not yet transitioned into state READY and been swapped into
  //    child_policy_; in this case, both child_policy_ and
  //    pending_child_policy_ are non-null).  In this case:
  //    a. If pending_child_policy_->name() equals child_policy_name,
  //       then we update the existing pending child policy.
  //    b. If pending_child_policy->name() does not equal
  //       child_policy_name, then we create a new policy.  The new
  //       policy is stored in pending_child_policy_ (replacing the one
  //       that was there before, which will be immediately shut down)
  //       and will later be swapped into child_policy_ by the helper
  //       when the new child transitions into state READY.
  const bool create_policy =
      // case 1
      lb_policy_ == nullptr ||
      // case 2b
      (pending_lb_policy_ == nullptr &&
       strcmp(lb_policy_->name(), lb_policy_name) != 0) ||
      // case 3b
      (pending_lb_policy_ != nullptr &&
       strcmp(pending_lb_policy_->name(), lb_policy_name) != 0);
  LoadBalancingPolicy* policy_to_update = nullptr;
  if (create_policy) {
    // Cases 1, 2b, and 3b: create a new child policy.
    // If lb_policy_ is null, we set it (case 1), else we set
    // pending_lb_policy_ (cases 2b and 3b).
    if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
      gpr_log(GPR_INFO, "resolving_lb=%p: Creating new %schild policy %s", this,
              lb_policy_ == nullptr ? "" : "pending ", lb_policy_name);
    }
    auto& lb_policy = lb_policy_ == nullptr ? lb_policy_ : pending_lb_policy_;
    lb_policy =
        CreateLbPolicyLocked(lb_policy_name, *result.args, trace_strings);
    policy_to_update = lb_policy.get();
  } else {
    // Cases 2a and 3a: update an existing policy.
    // If we have a pending child policy, send the update to the pending
    // policy (case 3a), else send it to the current policy (case 2a).
    policy_to_update = pending_lb_policy_ != nullptr ? pending_lb_policy_.get()
                                                     : lb_policy_.get();
  }
  GPR_ASSERT(policy_to_update != nullptr);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: Updating %schild policy %p", this,
            policy_to_update == pending_lb_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  UpdateArgs update_args;
  update_args.addresses = std::move(result.addresses);
  update_args.config = std::move(lb_policy_config);
  // TODO(roth): Once channel args is converted to C++, use std::move() here.
  update_args.args = result.args;
  result.args = nullptr;
  policy_to_update->UpdateLocked(std::move(update_args));
}

// Creates a new LB policy.
// Updates trace_strings to indicate what was done.
OrphanablePtr<LoadBalancingPolicy>
ResolvingLoadBalancingPolicy::CreateLbPolicyLocked(
    const char* lb_policy_name, const grpc_channel_args& args,
    TraceStringVector* trace_strings) {
  ResolvingControlHelper* helper = new ResolvingControlHelper(Ref());
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  lb_policy_args.args = &args;
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          lb_policy_name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "could not create LB policy \"%s\"", lb_policy_name);
    char* str;
    gpr_asprintf(&str, "Could not create LB policy \"%s\"", lb_policy_name);
    trace_strings->push_back(str);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: created new LB policy \"%s\" (%p)",
            this, lb_policy_name, lb_policy.get());
  }
  char* str;
  gpr_asprintf(&str, "Created new LB policy \"%s\"", lb_policy_name);
  trace_strings->push_back(str);
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void ResolvingLoadBalancingPolicy::MaybeAddTraceMessagesForAddressChangesLocked(
    bool resolution_contains_addresses, TraceStringVector* trace_strings) {
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
    size_t len = 0;
    grpc_core::UniquePtr<char> message(gpr_strvec_flatten(&v, &len));
    channel_control_helper()->AddTraceEvent(ChannelControlHelper::TRACE_INFO,
                                            StringView(message.get()));
    gpr_strvec_destroy(&v);
  }
}

void ResolvingLoadBalancingPolicy::OnResolverResultChangedLocked(
    Resolver::Result result) {
  // Handle race conditions.
  if (resolver_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: got resolver result", this);
  }
  // We only want to trace the address resolution in the follow cases:
  // (a) Address resolution resulted in service config change.
  // (b) Address resolution that causes number of backends to go from
  //     zero to non-zero.
  // (c) Address resolution that causes number of backends to go from
  //     non-zero to zero.
  // (d) Address resolution that causes a new LB policy to be created.
  //
  // We track a list of strings to eventually be concatenated and traced.
  TraceStringVector trace_strings;
  const bool resolution_contains_addresses = result.addresses.size() > 0;
  // Process the resolver result.
  const char* lb_policy_name = nullptr;
  RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config;
  bool service_config_changed = false;
  char* service_config_error_string = nullptr;
  if (process_resolver_result_ != nullptr) {
    grpc_error* service_config_error = GRPC_ERROR_NONE;
    service_config_changed = process_resolver_result_(
        process_resolver_result_user_data_, result, &lb_policy_name,
        &lb_policy_config, &service_config_error);
    if (service_config_error != GRPC_ERROR_NONE) {
      service_config_error_string =
          gpr_strdup(grpc_error_string(service_config_error));
      if (lb_policy_name == nullptr) {
        // Use an empty lb_policy_name as an indicator that we received an
        // invalid service config and we don't have a fallback service config.
        OnResolverError(service_config_error);
      } else {
        GRPC_ERROR_UNREF(service_config_error);
      }
    }
  } else {
    lb_policy_name = child_policy_name_.get();
    lb_policy_config = child_lb_config_;
  }
  if (lb_policy_name != nullptr) {
    // Create or update LB policy, as needed.
    CreateOrUpdateLbPolicyLocked(lb_policy_name, lb_policy_config,
                                 std::move(result), &trace_strings);
  }
  // Add channel trace event.
  if (service_config_changed) {
    // TODO(ncteisen): might be worth somehow including a snippet of the
    // config in the trace, at the risk of bloating the trace logs.
    trace_strings.push_back(gpr_strdup("Service config changed"));
  }
  if (service_config_error_string != nullptr) {
    trace_strings.push_back(service_config_error_string);
    service_config_error_string = nullptr;
  }
  MaybeAddTraceMessagesForAddressChangesLocked(resolution_contains_addresses,
                                               &trace_strings);
  ConcatenateAndAddChannelTraceLocked(&trace_strings);
}

}  // namespace grpc_core
