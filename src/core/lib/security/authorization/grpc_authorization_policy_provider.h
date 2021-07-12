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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_AUTHORIZATION_POLICY_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_AUTHORIZATION_POLICY_PROVIDER_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/status/statusor.h"

#include "src/core/lib/security/authorization/authorization_policy_provider.h"
#include "src/core/lib/security/authorization/rbac_translator.h"

namespace grpc_core {

// Provider class will get SDK Authorization policy from string during
// initialization. This policy will be translated to Envoy RBAC policies and
// used to initialize allow and deny AuthorizationEngine objects. This provider
// will return the same authorization engines everytime.
class StaticDataAuthorizationPolicyProvider
    : public grpc_authorization_policy_provider {
 public:
  static absl::StatusOr<RefCountedPtr<grpc_authorization_policy_provider>>
  Create(absl::string_view authz_policy);

  explicit StaticDataAuthorizationPolicyProvider(RbacPolicies policies);

  AuthorizationEngines engines() const override {
    return {allow_engine_, deny_engine_};
  }

  void Orphan() override {}

 private:
  RefCountedPtr<AuthorizationEngine> allow_engine_;
  RefCountedPtr<AuthorizationEngine> deny_engine_;
};

// TODO(ashithasantosh): Add implementation for file watcher authorization
// policy provider.

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_AUTHORIZATION_POLICY_PROVIDER_H
