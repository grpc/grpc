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

#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"
#include "src/core/lib/iomgr/combiner.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_lb_policy_refcount(
    false, "lb_policy_refcount");

namespace grpc_core {

LoadBalancingPolicy::LoadBalancingPolicy(const Args& args)
    : InternallyRefCounted(&grpc_trace_lb_policy_refcount),
      combiner_(GRPC_COMBINER_REF(args.combiner, "lb_policy")),
      client_channel_factory_(args.client_channel_factory),
      interested_parties_(grpc_pollset_set_create()),
      request_reresolution_(nullptr) {
  const grpc_arg* arg =
      grpc_channel_args_find(args.args, GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL);
  use_local_subchannel_pool_ = grpc_channel_arg_get_bool(arg, false);
  if (use_local_subchannel_pool_) {
    subchannel_pool_ = grpc_core::New<LocalSubchannelPool>();
  } else {
    subchannel_pool_ = grpc_core::GlobalSubchannelPool::instance().Ref();
  }
}

LoadBalancingPolicy::~LoadBalancingPolicy() {
  grpc_pollset_set_destroy(interested_parties_);
  GRPC_COMBINER_UNREF(combiner_, "lb_policy");
  if (use_local_subchannel_pool_) {
    grpc_core::Delete(subchannel_pool_);
  } else {
    static_cast<grpc_core::GlobalSubchannelPool*>(subchannel_pool_)->Unref();
  }
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
