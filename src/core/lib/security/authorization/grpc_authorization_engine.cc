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

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"

namespace grpc_core {

GrpcAuthorizationEngine::GrpcAuthorizationEngine(Rbac policy)
    : action_(policy.action) {
  for (auto& sub_policy : policy.policies) {
    policies_[sub_policy.first] = absl::make_unique<PolicyAuthorizationMatcher>(
        std::move(sub_policy.second));
  }
}

AuthorizationEngine::Decision GrpcAuthorizationEngine::Evaluate(
    const EvaluateArgs& args) const {
  Decision decision;
  bool matches = false;
  for (const auto& policy : policies_) {
    if (policy.second->Matches(args)) {
      matches = true;
      decision.matching_policy_name = policy.first;
      break;
    }
  }
  if (action_ == Rbac::Action::DENY) {
    decision.type = matches ? Decision::DecisionType::kDeny
                            : Decision::DecisionType::kAllow;
  } else {
    decision.type = matches ? Decision::DecisionType::kAllow
                            : Decision::DecisionType::kDeny;
  }
  return decision;
}

}  // namespace grpc_core
