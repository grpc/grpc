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
