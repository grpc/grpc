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

UniquePtr<LoadBalancingPolicy::Swapper>
LoadBalancingPolicy::Swapper::CreateLocked(
    LoadBalancingPolicy::Swapper::Args args) {
  auto swapper = MakeUnique<Swapper>(std::move(args));
  // If there is no pending LB policy, there is no need for any LB swapper.
  if (swapper->new_policy_ == nullptr) return nullptr;
  return swapper;
}

LoadBalancingPolicy::Swapper::Swapper(LoadBalancingPolicy::Swapper::Args args)
    : old_policy_(args.old_policy),
      interested_parties_(args.interested_parties),
      tracer_(args.tracer) {
  args.new_args.channel_control_helper->set_lb_swapper(this);
  new_policy_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
      args.new_name, std::move(args.new_args));
  *args.new_policy = new_policy_.get();
  if (GPR_UNLIKELY(new_policy_ == nullptr)) {
    gpr_log(GPR_ERROR, "lb_swapper=%p: could not create LB policy \"%s\"", this,
            args.new_name);
    return;
  }
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO,
            "lb_swapper=%p: created new pending LB policy \"%s\" (%p)", this,
            args.new_name, new_policy_.get());
  }
  grpc_pollset_set_add_pollset_set(new_policy_->interested_parties(),
                                   args.interested_parties);
  new_policy_->ExitIdleLocked();
  // If we don't have any existing policy yet, swap immediately.
  if (*args.old_policy == nullptr || status_ == TO_BE_CURRENT) {
    DoSwapLocked();
  }
  status_ = DETERMINED;
}

LoadBalancingPolicy::Swapper::~Swapper() {
  if (new_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(new_policy_->interested_parties(),
                                     interested_parties_);
    new_policy_->channel_control_helper()->set_lb_swapper(nullptr);
  }
}

void LoadBalancingPolicy::Swapper::MaybeSwap() {
  if (status_ == TO_BE_PENDING || status_ == TO_BE_CURRENT) {
    status_ = TO_BE_CURRENT;
    return;
  }
  DoSwapLocked();
}

void LoadBalancingPolicy::Swapper::DoSwapLocked() {
  if (*old_policy_ != nullptr) {
    if (tracer_->enabled()) {
      gpr_log(GPR_INFO, "lb_swapper=%p: shutting down old lb_policy=%p", this,
              (*old_policy_).get());
    }
    grpc_pollset_set_del_pollset_set((*old_policy_)->interested_parties(),
                                     interested_parties_);
  }
  *old_policy_ = std::move(new_policy_);
  (*old_policy_)->channel_control_helper()->set_lb_swapper(nullptr);
}

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

LoadBalancingPolicy::LoadBalancingPolicy(Args args, intptr_t initial_refcount)
    : InternallyRefCounted(&grpc_trace_lb_policy_refcount, initial_refcount),
      combiner_(GRPC_COMBINER_REF(args.combiner, "lb_policy")),
      interested_parties_(grpc_pollset_set_create()),
      channel_control_helper_(std::move(args.channel_control_helper)) {
  channel_control_helper_->set_lb_policy(this);
}

LoadBalancingPolicy::~LoadBalancingPolicy() {
  grpc_pollset_set_destroy(interested_parties_);
  GRPC_COMBINER_UNREF(combiner_, "lb_policy");
}

}  // namespace grpc_core
