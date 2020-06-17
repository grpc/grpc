/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/security/authorization/cel_evaluation_engine.h"

/* Creates a CelEvaluationEngine that makes authorization decisions based on the
 * CEL condition fields of the provided rbac policies. All rbac policies should
 * have principals and permissions set to any.
 */
CelEvaluationEngine::CelEvaluationEngine(
    const envoy_config_rbac_v2_RBAC& rbac_policy) {
  for (const auto& policy : rbac_policy.policies()) {
    policies_.emplace(policy.first, 
      std::make_unique<google_api_expr_v1alpha1_Expr>(policy.second.condition());
  }
}
 