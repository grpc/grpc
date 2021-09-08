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

#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"

#include <grpc/grpc_security.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<grpc_authorization_policy_provider>>
StaticDataAuthorizationPolicyProvider::Create(absl::string_view authz_policy) {
  auto policies_or = GenerateRbacPolicies(authz_policy);
  if (!policies_or.ok()) {
    return policies_or.status();
  }
  return MakeRefCounted<StaticDataAuthorizationPolicyProvider>(
      std::move(*policies_or));
}

StaticDataAuthorizationPolicyProvider::StaticDataAuthorizationPolicyProvider(
    RbacPolicies policies)
    : allow_engine_(MakeRefCounted<GrpcAuthorizationEngine>(
          std::move(policies.allow_policy))),
      deny_engine_(MakeRefCounted<GrpcAuthorizationEngine>(
          std::move(policies.deny_policy))) {}

}  // namespace grpc_core

// Wrapper APIs declared in grpc_security.h

grpc_authorization_policy_provider*
grpc_authorization_policy_provider_static_data_create(
    const char* authz_policy, grpc_status_code* code,
    const char** error_details) {
  GPR_ASSERT(authz_policy != nullptr);
  auto provider_or =
      grpc_core::StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  if (!provider_or.ok()) {
    *code = static_cast<grpc_status_code>(provider_or.status().code());
    *error_details =
        gpr_strdup(std::string(provider_or.status().message()).c_str());
    return nullptr;
  }
  *code = GRPC_STATUS_OK;
  *error_details = nullptr;
  return provider_or->release();
}

void grpc_authorization_policy_provider_release(
    grpc_authorization_policy_provider* provider) {
  if (provider != nullptr) provider->Unref();
}
