
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_EVALUATION_ENGINE_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_EVALUATION_ENGINE_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/core/ext/upb-generated/envoy/config/rbac/v2/rbac.upb.h"
#include "src/core/ext/upb-generated/google/api/expr/v1alpha1/syntax.upb.h"
#include "upb/upb.hpp"

// CelEvaluationEngine makes an AuthorizationDecision to ALLOW or DENY the
// current action based on the condition fields in two provided RBAC policies.
// The engine may be constructed with one or two policies. If two polcies,
// the first policy is deny-if-matched and the second is allow-if-matched.
// The engine returns UNDECIDED decision if it fails to find a match in either
// policy. This engine ignores the principal and permission fields in RBAC
// policies. It is the caller's responsibility to provide RBAC policies that
// are compatible with this engine.
//
// Example:
// CelEvaluationEngine* cel_engine = CelEvaluationEngineFactory(rbac_policies);
// cel_engine->Evaluate(evaluate_args); // returns authorization decision.
class CelEvaluationEngine {
 public:
  // rbac_policies must be a vector containing either a single policy of any
  // kind, or one deny policy and one allow policy, in that order.
  static std::unique_ptr<CelEvaluationEngine> CreateCelEvaluationEngine(
      const std::vector<envoy_config_rbac_v2_RBAC*>& rbac_policies);
  // TODO(mywang@google.com): add an Evaluate member function.

 private:
  enum Action {
    ALLOW,
    DENY,
  };
  static const size_t NumPolicies = 2;

  explicit CelEvaluationEngine(
      const std::vector<envoy_config_rbac_v2_RBAC*>& rbac_policies);
  std::map<const std::string, const google_api_expr_v1alpha1_Expr*>
      deny_if_matched_;
  std::map<const std::string, const google_api_expr_v1alpha1_Expr*>
      allow_if_matched_;
  upb::Arena arena_;
};

#endif /* GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_EVALUATION_ENGINE_H */
