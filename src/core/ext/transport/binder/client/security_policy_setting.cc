// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifndef GRPC_NO_BINDER

#include "src/core/ext/transport/binder/client/security_policy_setting.h"

namespace grpc_binder {

void SecurityPolicySetting::Set(
    absl::string_view connection_id,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  grpc_core::MutexLock l(&m_);
  GPR_ASSERT(security_policy_map_.count(std::string(connection_id)) == 0);
  security_policy_map_[std::string(connection_id)] = security_policy;
}

std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
SecurityPolicySetting::Get(absl::string_view connection_id) {
  grpc_core::MutexLock l(&m_);
  GPR_ASSERT(security_policy_map_.count(std::string(connection_id)) != 0);
  return security_policy_map_[std::string(connection_id)];
}

SecurityPolicySetting* GetSecurityPolicySetting() {
  static SecurityPolicySetting* s = new SecurityPolicySetting();
  return s;
}

}  // namespace grpc_binder
#endif
