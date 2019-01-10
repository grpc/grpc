/*
 *
 * Copyright 2018 gRPC authors.
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

#include "test/core/util/forwarding_load_balancing_policy.h"

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/pollset_set.h"

namespace grpc_core {

TraceFlag grpc_trace_forwarding_lb(false, "forwarding_lb");

ForwardingLoadBalancingPolicy::ForwardingLoadBalancingPolicy(
    const Args& args, const std::string& delegate_policy_name)
    : LoadBalancingPolicy(args) {
  delegate_ =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          delegate_policy_name.c_str(), args);
  grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                   interested_parties());
  // Give re-resolution closure to delegate.
  GRPC_CLOSURE_INIT(&on_delegate_request_reresolution_,
                    OnDelegateRequestReresolutionLocked, this,
                    grpc_combiner_scheduler(combiner()));
  Ref().release();  // held by callback.
  delegate_->SetReresolutionClosureLocked(&on_delegate_request_reresolution_);
}

ForwardingLoadBalancingPolicy::~ForwardingLoadBalancingPolicy() {}

void ForwardingLoadBalancingPolicy::OnDelegateRequestReresolutionLocked(
    void* arg, grpc_error* error) {
  ForwardingLoadBalancingPolicy* self =
      static_cast<ForwardingLoadBalancingPolicy*>(arg);
  if (error != GRPC_ERROR_NONE || self->delegate_ == nullptr) {
    self->Unref();
    return;
  }
  self->TryReresolutionLocked(&grpc_trace_forwarding_lb, GRPC_ERROR_NONE);
  self->delegate_->SetReresolutionClosureLocked(
      &self->on_delegate_request_reresolution_);
}

}  // namespace grpc_core
