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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_TRANSLATOR_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_TRANSLATOR_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/authorization/rbac_policy.h"

namespace grpc_core {

struct RbacPolicies {
  Rbac deny_policy;
  Rbac allow_policy;
};

// Translates SDK authorization policy to Envoy RBAC policies. Returns error on
// failure.
// authz_policy: Authorization Policy string in JSON format.
absl::StatusOr<RbacPolicies> GenerateRbacPolicies(
    absl::string_view authz_policy);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_TRANSLATOR_H */
