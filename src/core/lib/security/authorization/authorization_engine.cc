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

#include "src/core/lib/security/authorization/authorization_engine.h"

namespace grpc_core {

AuthorizationEngine::AuthorizationEngine(const Rbac& rbac_policy)
    : action_(rbac_policy.action) {
  for (const auto& policy : rbac_policy.policies) {
    policies_[policy.first] = absl::make_unique<PolicyMatcher>(policy.second);
  }
}

AuthorizationEngineInterface::AuthorizationDecision
AuthorizationEngine::Evaluate(const EvaluateArgs& args) const {
  AuthorizationDecision authz_decision;
  bool matches = false;
  for (const auto& policy : policies_) {
    if (policy.second->Matches(args)) {
      matches = true;
      authz_decision.matching_policy_name = policy.first;
      break;
    }
  }
  if (action_ == Rbac::Action::DENY) {
    authz_decision.type = matches ? AuthorizationDecision::DecisionType::DENY
                                  : AuthorizationDecision::DecisionType::ALLOW;
  } else {
    authz_decision.type = matches ? AuthorizationDecision::DecisionType::ALLOW
                                  : AuthorizationDecision::DecisionType::DENY;
  }
  return authz_decision;
}

}  // namespace grpc_core
