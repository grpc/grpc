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

#include <grpc/grpc_security.h>

#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"

namespace grpc_core {

absl::StatusOr<grpc_authorization_policy_provider*>
StaticDataAuthorizationPolicyProvider::Create(const std::string& authz_policy) {
  auto policies_or = GenerateRbacPolicies(authz_policy);
  if (!policies_or.ok()) {
    return policies_or.status();
  }
  return new StaticDataAuthorizationPolicyProvider(
      std::move(policies_or.value()));
}

StaticDataAuthorizationPolicyProvider::StaticDataAuthorizationPolicyProvider(
    RbacPolicies policies)
    : allow_engine_(MakeRefCounted<GrpcAuthorizationEngine>(
          std::move(policies.allow_policy))),
      deny_engine_(MakeRefCounted<GrpcAuthorizationEngine>(
          std::move(policies.deny_policy))) {}

}  // namespace grpc_core

/** Wrapper APIs declared in grpc_security.h **/

grpc_authorization_policy_provider*
grpc_authorization_policy_provider_static_data_create(const char* authz_policy,
                                                      grpc_status_code status,
                                                      const char* error) {
  GPR_ASSERT(authz_policy != nullptr);
  auto provider_or =
      grpc_core::StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  if (!provider_or.ok()) {
    status = GRPC_STATUS_INVALID_ARGUMENT;
    error = provider_or.status().message().data();
    return nullptr;
  }
  return provider_or.value();
}

void grpc_authorization_policy_provider_release(
    grpc_authorization_policy_provider* provider) {
  if (provider != nullptr) provider->Unref();
}
