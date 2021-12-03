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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_SECURITY_POLICY_INTERNAL_ONLY_SECURITY_POLICY_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_SECURITY_POLICY_INTERNAL_ONLY_SECURITY_POLICY_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/security_policy/security_policy.h"

#ifdef GPR_ANDROID

namespace grpc {
namespace experimental {
namespace binder {

// Only allows the connections from processes with the same UID
class InternalOnlySecurityPolicy : public SecurityPolicy {
 public:
  InternalOnlySecurityPolicy();
  ~InternalOnlySecurityPolicy() override;
  bool IsAuthorized(int uid) override;
};

}  // namespace binder
}  // namespace experimental
}  // namespace grpc

#endif

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_SECURITY_POLICY_INTERNAL_ONLY_SECURITY_POLICY_H
