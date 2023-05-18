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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_INTERNAL_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <utility>

#include "absl/functional/any_invocable.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/service_config/service_config_call_data.h"

//
// This file contains internal interfaces used to allow various plugins
// (filters, LB policies, etc) to access internal data provided by the
// ClientChannel that is not normally accessible via external APIs.
//

// Channel arg key for health check service name.
#define GRPC_ARG_HEALTH_CHECK_SERVICE_NAME \
  "grpc.internal.health_check_service_name"

namespace grpc_core {

// Internal type for LB call state interface.  Provides an interface for
// LB policies to access internal call attributes.
class ClientChannelLbCallState : public LoadBalancingPolicy::CallState {
 public:
  virtual ServiceConfigCallData::CallAttributeInterface* GetCallAttribute(
      UniqueTypeName type) const = 0;
};

// Internal type for ServiceConfigCallData.  Handles call commits.
class ClientChannelServiceConfigCallData : public ServiceConfigCallData {
 public:
  ClientChannelServiceConfigCallData(Arena* arena,
                                     grpc_call_context_element* call_context)
      : ServiceConfigCallData(arena, call_context) {}

  void SetOnCommit(absl::AnyInvocable<void()> on_commit) {
    GPR_ASSERT(on_commit_ == nullptr);
    on_commit_ = std::move(on_commit);
  }

  void Commit() {
    auto on_commit = std::move(on_commit_);
    if (on_commit != nullptr) on_commit();
  }

 private:
  absl::AnyInvocable<void()> on_commit_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CLIENT_CHANNEL_INTERNAL_H
