
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

#include <map>
#include <memory>
#include <string>

#include "src/core/ext/upb-generated/envoy/config/rbac/v2/rbac.upb.h"
#include "src/core/ext/upb-generated/google/api/expr/v1alpha1/syntax.upb.h"
#include "upb/upb.h"

// CelEvaluationEngine makes an AuthorizationDecision to ALLOW or DENY the
// current action based on the condition fields in the provided RBAC policy.
// The engine returns UNDECIDED decision if it fails to find a match in RBAC
// policy. This engine ignores the principal and permission fields in RBAC
// policy. It is the caller's responsibility to provide an RBAC policy that
// is compatible with this engine.
//
// Example:
// CelEvaluationEngine* cel_engine = new CelEvaluationEngine(rbac_policy);
// cel_engine->Evaluate(evaluate_args); // returns authorization decision.
class CelEvaluationEngine {
 public:
  explicit CelEvaluationEngine(const envoy_config_rbac_v2_RBAC& rbac_policy);
  // TODO(mywang@google.com): add an Evaluate member function.

 private:
  // Set allowed_if_matched_ to true if the RBAC policy is an ALLOW policy,
  // and false if it is a DENY policy.
  const bool allowed_if_matched_;
  std::map<std::string, google_api_expr_v1alpha1_Expr*> policies_;
  upb::Arena arena_;
};

#endif  //  GRPC_CORE_LIB_SECURITY_AUTHORIZATION_CEL_EVALUATION_ENGINE_H
