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

//
// LoadBalancingPolicy
//

LoadBalancingPolicy::LoadBalancingPolicy(Args args, intptr_t initial_refcount)
    : InternallyRefCounted(&grpc_trace_lb_policy_refcount, initial_refcount),
      combiner_(GRPC_COMBINER_REF(args.combiner, "lb_policy")),
      interested_parties_(grpc_pollset_set_create()),
      channel_control_helper_(std::move(args.channel_control_helper)) {}

LoadBalancingPolicy::~LoadBalancingPolicy() {
  grpc_pollset_set_destroy(interested_parties_);
  GRPC_COMBINER_UNREF(combiner_, "lb_policy");
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

//
// LoadBalancingPolicy::UpdateArgs
//

LoadBalancingPolicy::UpdateArgs::UpdateArgs(const UpdateArgs& other) {
  addresses = other.addresses;
  config = other.config;
  args = grpc_channel_args_copy(other.args);
}

LoadBalancingPolicy::UpdateArgs::UpdateArgs(UpdateArgs&& other) {
  addresses = std::move(other.addresses);
  config = std::move(other.config);
  args = other.args;
  other.args = nullptr;
}

LoadBalancingPolicy::UpdateArgs& LoadBalancingPolicy::UpdateArgs::operator=(
    const UpdateArgs& other) {
  addresses = other.addresses;
  config = other.config;
  grpc_channel_args_destroy(args);
  args = grpc_channel_args_copy(other.args);
  return *this;
}

LoadBalancingPolicy::UpdateArgs& LoadBalancingPolicy::UpdateArgs::operator=(
    UpdateArgs&& other) {
  addresses = std::move(other.addresses);
  config = std::move(other.config);
  args = other.args;
  other.args = nullptr;
  return *this;
}

}  // namespace grpc_core
