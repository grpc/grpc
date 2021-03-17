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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_ENGINE_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_ENGINE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/authorization_engine_interface.h"
#include "src/core/lib/security/authorization/matchers.h"
#include "src/core/lib/security/authorization/rbac_policy.h"

namespace grpc_core {

// AuthorizationEngine can be either Allow or Deny engine. This engine makes
// authorization decisions to ALLOW or DENY incoming RPC request based on
// permission and principal configs in the provided RBAC policy and engine type.
// This engine ignores condition field in RBAC config. It is the caller's
// responsibility to provide RBAC policies that are compatible with this engine.
class AuthorizationEngine : public AuthorizationEngineInterface {
 public:
  // Builds AuthorizationEngine without any policies.
  explicit AuthorizationEngine(Rbac::Action action) : action_(action) {}
  // Builds AuthorizationEngine with allow/deny policy.
  explicit AuthorizationEngine(const Rbac& rbac_policy);
  // Used only for testing purposes.
  AuthorizationEngine(Rbac::Action action,
                      std::map<std::string, std::unique_ptr<Matcher>> policies)
      : action_(action), policies_(std::move(policies)) {}
  // Evaluate incoming request against RBAC policy and make a decision to
  // allow/deny this request.
  AuthorizationEngineInterface::AuthorizationDecision Evaluate(
      const EvaluateArgs& args) const override;

 private:
  Rbac::Action action_;
  std::map<std::string, std::unique_ptr<Matcher>> policies_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_ENGINE_H
