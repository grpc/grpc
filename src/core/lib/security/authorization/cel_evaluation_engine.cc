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

#include "src/core/ext/upb-generated/envoy/config/rbac/v2/rbac.upb.h"
#include "src/core/ext/upb-generated/google/api/expr/v1alpha1/syntax.upb.h"
#include "src/core/lib/security/authorization/cel_evaluation_engine.h"

CelEvaluationEngine::CelEvaluationEngine(
    const envoy_config_rbac_v2_RBAC& rbac_policy) {
  // Extract array of policies and store their condition fields in policies_.
  size_t size;
  const envoy_config_rbac_v2_RBAC_PoliciesEntry* const* 
   policies = envoy_config_rbac_v2_RBAC_policies(&rbac_policy, &size);
  upb::Arena temp_arena;

  for (size_t i = 0; i < size; ++i) {
    std::string key = envoy_config_rbac_v2_RBAC_PoliciesEntry_key(
                                                             policies[i]).data;
    const envoy_config_rbac_v2_Policy* 
     policy = envoy_config_rbac_v2_RBAC_PoliciesEntry_value(policies[i]);
    const google_api_expr_v1alpha1_Expr* 
     condition = envoy_config_rbac_v2_Policy_condition(policy);
    // Parse condition to make a pointer tied to the lifetime of arena_.
    size_t serial_len;
    char* serialized = google_api_expr_v1alpha1_Expr_serialize(
                                               condition, temp_arena.ptr(), &serial_len);
    google_api_expr_v1alpha1_Expr* 
     parsed_condition = google_api_expr_v1alpha1_Expr_parse(
                                               serialized, serial_len, arena_.ptr());
       
    policies_.insert(std::make_pair(key, parsed_condition));
  }
  
  action_allow_ = false;
}
 
