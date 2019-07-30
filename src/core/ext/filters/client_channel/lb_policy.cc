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

namespace grpc_core {

DebugOnlyTraceFlag grpc_trace_lb_policy_refcount(false, "lb_policy_refcount");

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

void LoadBalancingPolicy::Orphan() {
  ShutdownLocked();
  Unref();
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
  // TODO(roth): Use std::move() once channel args is converted to C++.
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
  // TODO(roth): Use std::move() once channel args is converted to C++.
  grpc_channel_args_destroy(args);
  args = other.args;
  other.args = nullptr;
  return *this;
}

//
// LoadBalancingPolicy::QueuePicker
//

LoadBalancingPolicy::PickResult LoadBalancingPolicy::QueuePicker::Pick(
    PickArgs args) {
  // We invoke the parent's ExitIdleLocked() via a closure instead
  // of doing it directly here, for two reasons:
  // 1. ExitIdleLocked() may cause the policy's state to change and
  //    a new picker to be delivered to the channel.  If that new
  //    picker is delivered before ExitIdleLocked() returns, then by
  //    the time this function returns, the pick will already have
  //    been processed, and we'll be trying to re-process the same
  //    pick again, leading to a crash.
  // 2. We are currently running in the data plane combiner, but we
  //    need to bounce into the control plane combiner to call
  //    ExitIdleLocked().
  if (!exit_idle_called_) {
    exit_idle_called_ = true;
    parent_->Ref().release();  // ref held by closure.
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(&CallExitIdle, parent_.get(),
                            grpc_combiner_scheduler(parent_->combiner())),
        GRPC_ERROR_NONE);
  }
  PickResult result;
  result.type = PickResult::PICK_QUEUE;
  return result;
}

void LoadBalancingPolicy::QueuePicker::CallExitIdle(void* arg,
                                                    grpc_error* error) {
  LoadBalancingPolicy* parent = static_cast<LoadBalancingPolicy*>(arg);
  parent->ExitIdleLocked();
  parent->Unref();
}

//
// LoadBalancingPolicy::TransientFailurePicker
//

LoadBalancingPolicy::PickResult
LoadBalancingPolicy::TransientFailurePicker::Pick(PickArgs args) {
  PickResult result;
  result.type = PickResult::PICK_TRANSIENT_FAILURE;
  result.error = GRPC_ERROR_REF(error_);
  return result;
}

}  // namespace grpc_core
