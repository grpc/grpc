//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/load_balancing/lb_policy.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"

namespace grpc_core {

//
// LoadBalancingPolicy
//

LoadBalancingPolicy::LoadBalancingPolicy(Args args, intptr_t initial_refcount)
    : InternallyRefCounted(GRPC_TRACE_FLAG_ENABLED(lb_policy_refcount)
                               ? "LoadBalancingPolicy"
                               : nullptr,
                           initial_refcount),
      work_serializer_(std::move(args.work_serializer)),
      interested_parties_(grpc_pollset_set_create()),
      channel_control_helper_(std::move(args.channel_control_helper)),
      channel_args_(std::move(args.args)) {}

LoadBalancingPolicy::~LoadBalancingPolicy() {
  grpc_pollset_set_destroy(interested_parties_);
}

void LoadBalancingPolicy::Orphan() {
  ShutdownLocked();
  Unref(DEBUG_LOCATION, "Orphan");
}

//
// LoadBalancingPolicy::SubchannelPicker
//

LoadBalancingPolicy::SubchannelPicker::SubchannelPicker()
    : DualRefCounted(GRPC_TRACE_FLAG_ENABLED(lb_policy_refcount)
                         ? "SubchannelPicker"
                         : nullptr) {}

//
// LoadBalancingPolicy::QueuePicker
//

LoadBalancingPolicy::PickResult LoadBalancingPolicy::QueuePicker::Pick(
    PickArgs /*args*/) {
  // We invoke the parent's ExitIdleLocked() via a closure instead
  // of doing it directly here because ExitIdleLocked() may cause the
  // policy's state to change and a new picker to be delivered to the
  // channel.  If that new picker is delivered before ExitIdleLocked()
  // returns, then by the time this function returns, the pick will already
  // have been processed, and we'll be trying to re-process the same pick
  // again, leading to a crash.
  MutexLock lock(&mu_);
  if (parent_ != nullptr) {
    auto* parent = parent_.release();  // ref held by lambda.
    ExecCtx::Run(DEBUG_LOCATION,
                 GRPC_CLOSURE_CREATE(
                     [](void* arg, grpc_error_handle /*error*/) {
                       auto* parent = static_cast<LoadBalancingPolicy*>(arg);
                       parent->work_serializer()->Run([parent]() {
                         parent->ExitIdleLocked();
                         parent->Unref();
                       });
                     },
                     parent, nullptr),
                 absl::OkStatus());
  }
  return PickResult::Queue();
}

}  // namespace grpc_core
