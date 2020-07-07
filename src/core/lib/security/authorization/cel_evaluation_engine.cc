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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/cel_evaluation_engine.h"

std::unique_ptr<CelEvaluationEngine>
CelEvaluationEngine::CreateCelEvaluationEngine(
    const std::vector<envoy_config_rbac_v2_RBAC*>& rbac_policies) {
  if (rbac_policies.size() == NumPolicies &&
      envoy_config_rbac_v2_RBAC_action(rbac_policies[0]) == DENY &&
      envoy_config_rbac_v2_RBAC_action(rbac_policies[1]) == ALLOW) {
    return std::unique_ptr<CelEvaluationEngine>(
        new CelEvaluationEngine(rbac_policies));
  } else {
    return nullptr;
  }
}

CelEvaluationEngine::CelEvaluationEngine(
    const std::vector<envoy_config_rbac_v2_RBAC*>& rbac_policies) {
  for (const auto& rbac_policy : rbac_policies) {
    // Extract array of policies and store their condition fields in either
    // allow_if_matched_ or deny_if_matched_, depending on the policy action.
    upb::Arena temp_arena;
    size_t policy_num = UPB_MAP_BEGIN;
    const envoy_config_rbac_v2_RBAC_PoliciesEntry* policy_entry;

    while ((policy_entry = envoy_config_rbac_v2_RBAC_policies_next(
                rbac_policy, &policy_num)) != nullptr) {
      const upb_strview policy_name_strview =
          envoy_config_rbac_v2_RBAC_PoliciesEntry_key(policy_entry);
      const std::string policy_name(policy_name_strview.data,
                                    policy_name_strview.size);
      const envoy_config_rbac_v2_Policy* policy =
          envoy_config_rbac_v2_RBAC_PoliciesEntry_value(policy_entry);
      const google_api_expr_v1alpha1_Expr* condition =
          envoy_config_rbac_v2_Policy_condition(policy);
      // Parse condition to make a pointer tied to the lifetime of arena_.
      size_t serial_len;
      const char* serialized = google_api_expr_v1alpha1_Expr_serialize(
          condition, temp_arena.ptr(), &serial_len);
      const google_api_expr_v1alpha1_Expr* parsed_condition =
          google_api_expr_v1alpha1_Expr_parse(serialized, serial_len,
                                              arena_.ptr());

      if (envoy_config_rbac_v2_RBAC_action(rbac_policy) == ALLOW) {
        allow_if_matched_.insert(std::make_pair(policy_name, parsed_condition));
      } else {
        deny_if_matched_.insert(std::make_pair(policy_name, parsed_condition));
      }
    }
  }
}
