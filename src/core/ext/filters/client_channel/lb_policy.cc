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
    : InternallyRefCounted(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_lb_policy_refcount)
              ? "LoadBalancingPolicy"
              : nullptr,
          initial_refcount),
      work_serializer_(std::move(args.work_serializer)),
      interested_parties_(grpc_pollset_set_create()),
      channel_control_helper_(std::move(args.channel_control_helper)) {}

LoadBalancingPolicy::~LoadBalancingPolicy() {
  grpc_pollset_set_destroy(interested_parties_);
}

void LoadBalancingPolicy::Orphan() {
  ShutdownLocked();
  Unref(DEBUG_LOCATION, "Orphan");
}

//
// LoadBalancingPolicy::UpdateArgs
//

LoadBalancingPolicy::UpdateArgs::UpdateArgs(const UpdateArgs& other) {
  addresses = other.addresses;
  config = other.config;
  args = grpc_channel_args_copy(other.args);
}

LoadBalancingPolicy::UpdateArgs::UpdateArgs(UpdateArgs&& other) noexcept {
  addresses = std::move(other.addresses);
  config = std::move(other.config);
  // TODO(roth): Use std::move() once channel args is converted to C++.
  args = other.args;
  other.args = nullptr;
}

LoadBalancingPolicy::UpdateArgs& LoadBalancingPolicy::UpdateArgs::operator=(
    const UpdateArgs& other) {
  if (&other == this) {
    return *this;
  }
  addresses = other.addresses;
  config = other.config;
  grpc_channel_args_destroy(args);
  args = grpc_channel_args_copy(other.args);
  return *this;
}

LoadBalancingPolicy::UpdateArgs& LoadBalancingPolicy::UpdateArgs::operator=(
    UpdateArgs&& other) noexcept {
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
    PickArgs /*args*/) {
  // We invoke the parent's ExitIdleLocked() via a closure instead
  // of doing it directly here, for two reasons:
  // 1. ExitIdleLocked() may cause the policy's state to change and
  //    a new picker to be delivered to the channel.  If that new
  //    picker is delivered before ExitIdleLocked() returns, then by
  //    the time this function returns, the pick will already have
  //    been processed, and we'll be trying to re-process the same
  //    pick again, leading to a crash.
  // 2. We are currently running in the data plane mutex, but we
  //    need to bounce into the control plane work_serializer to call
  //    ExitIdleLocked().
  if (!exit_idle_called_ && parent_ != nullptr) {
    exit_idle_called_ = true;
    auto* parent = parent_->Ref().release();  // ref held by lambda.
    ExecCtx::Run(DEBUG_LOCATION,
                 GRPC_CLOSURE_CREATE(
                     [](void* arg, grpc_error_handle /*error*/) {
                       auto* parent = static_cast<LoadBalancingPolicy*>(arg);
                       parent->work_serializer()->Run(
                           [parent]() {
                             parent->ExitIdleLocked();
                             parent->Unref();
                           },
                           DEBUG_LOCATION);
                     },
                     parent, nullptr),
                 GRPC_ERROR_NONE);
  }
  PickResult result;
  result.type = PickResult::PICK_QUEUE;
  return result;
}

//
// LoadBalancingPolicy::TransientFailurePicker
//

LoadBalancingPolicy::PickResult
LoadBalancingPolicy::TransientFailurePicker::Pick(PickArgs /*args*/) {
  PickResult result;
  result.type = PickResult::PICK_FAILED;
  result.error = GRPC_ERROR_REF(error_);
  return result;
}

}  // namespace grpc_core
