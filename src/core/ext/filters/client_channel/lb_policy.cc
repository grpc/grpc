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

#include "src/core/ext/filters/client_channel/lb_policy.h"

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/iomgr/combiner.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_lb_policy_refcount(
    false, "lb_policy_refcount");

namespace grpc_core {

grpc_json* LoadBalancingPolicy::ParseLoadBalancingConfig(
    const grpc_json* lb_config_array) {
  if (lb_config_array == nullptr || lb_config_array->type != GRPC_JSON_ARRAY) {
    return nullptr;
  }
  // Find the first LB policy that this client supports.
  for (const grpc_json* lb_config = lb_config_array->child;
       lb_config != nullptr; lb_config = lb_config->next) {
    if (lb_config->type != GRPC_JSON_OBJECT) return nullptr;
    grpc_json* policy = nullptr;
    for (grpc_json* field = lb_config->child; field != nullptr;
         field = field->next) {
      if (field->key == nullptr || field->type != GRPC_JSON_OBJECT)
        return nullptr;
      if (policy != nullptr) return nullptr;  // Violate "oneof" type.
      policy = field;
    }
    if (policy == nullptr) return nullptr;
    // If we support this policy, then select it.
    if (LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(policy->key)) {
      return policy;
    }
  }
  return nullptr;
}

OrphanablePtr<LoadBalancingPolicy::Swapper>
LoadBalancingPolicy::Swapper::Create(LoadBalancingPolicy::Swapper::Args args) {
  auto swapper = MakeOrphanable<Swapper>(std::move(args));
  if (swapper->new_policy_ == nullptr) return nullptr;
  return swapper;
}

void LoadBalancingPolicy::Swapper::DoSwap() {
  // Swap out the LB policy and update the fds in interested_parties_.
  if (*old_policy_ != nullptr) {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO, "lb_swapper=%p: shutting down old lb_policy=%p", this,
              (*old_policy_).get());
    }
    grpc_pollset_set_del_pollset_set((*old_policy_)->interested_parties(),
                                     interested_parties_);
    (*old_policy_)->HandOffPendingPicksLocked(new_policy_.get());
  }
  *old_policy_ = std::move(new_policy_);
  after_swap_(pre_after_swap_arg_);
  swap_done = true;
}

LoadBalancingPolicy::Swapper::Swapper(LoadBalancingPolicy::Swapper::Args args)
    : old_policy_(args.old_policy),
      new_policy_(LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          args.new_name, std::move(args.new_args))),
      interested_parties_(args.interested_parties),
      after_swap_(args.after_swap),
      pre_after_swap_arg_(args.pre_post_swap_arg),
      tracer_(args.tracer) {
  *args.new_policy = new_policy_.get();
  if (GPR_UNLIKELY(new_policy_ == nullptr)) {
    gpr_log(GPR_ERROR, "could not create LB policy \"%s\"", args.new_name);
    return;
  } else {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO,
              "lb_swapper=%p: created new pending LB policy \"%s\" (%p)", this,
              args.new_name, new_policy_.get());
    }
    if (args.exit_idle) {
      new_policy_->ExitIdleLocked();
    }
    grpc_pollset_set_add_pollset_set(new_policy_->interested_parties(),
                                     args.interested_parties);
  }
  if (*args.old_policy == nullptr) {
    DoSwap();
    return;
  }
  GRPC_CLOSURE_INIT(&on_changed_, &OnLbPolicyStateChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  new_policy_->NotifyOnStateChangeLocked(&state_, &on_changed_);
}

void LoadBalancingPolicy::Swapper::Orphan() {
  // If swap is already done, it's safe to delete self.
  if (swap_done) {
    Delete(this);
    return;
  }
  // Otherwise, set the shutdown flag and cancel the state watch.
  shutdown = true;
  new_policy_->NotifyOnStateChangeLocked(nullptr, &on_changed_);
}

void LoadBalancingPolicy::Swapper::OnLbPolicyStateChangedLocked(
    void* arg, grpc_error* error) {
  Swapper* self = static_cast<Swapper*>(arg);
  if (self->shutdown) {
    Delete(self);
    return;
  }
  if (self->state_ == GRPC_CHANNEL_READY && error == GRPC_ERROR_NONE) {
    self->DoSwap();
    return;
  }
  // Retry.
  self->new_policy_->NotifyOnStateChangeLocked(&self->state_,
                                               &self->on_changed_);
}

LoadBalancingPolicy::LoadBalancingPolicy(Args args)
    : InternallyRefCounted(&grpc_trace_lb_policy_refcount),
      combiner_(GRPC_COMBINER_REF(args.combiner, "lb_policy")),
      client_channel_factory_(args.client_channel_factory),
      subchannel_pool_(std::move(args.subchannel_pool)),
      interested_parties_(grpc_pollset_set_create()),
      request_reresolution_(nullptr) {}

LoadBalancingPolicy::~LoadBalancingPolicy() {
  grpc_pollset_set_destroy(interested_parties_);
  GRPC_COMBINER_UNREF(combiner_, "lb_policy");
}

void LoadBalancingPolicy::TryReresolutionLocked(
    grpc_core::TraceFlag* grpc_lb_trace, grpc_error* error) {
  if (request_reresolution_ != nullptr) {
    GRPC_CLOSURE_SCHED(request_reresolution_, error);
    request_reresolution_ = nullptr;
    if (grpc_lb_trace->enabled()) {
      gpr_log(GPR_INFO,
              "%s %p: scheduling re-resolution closure with error=%s.",
              grpc_lb_trace->name(), this, grpc_error_string(error));
    }
  } else {
    if (grpc_lb_trace->enabled()) {
      gpr_log(GPR_INFO, "%s %p: no available re-resolution closure.",
              grpc_lb_trace->name(), this);
    }
  }
}

}  // namespace grpc_core
