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

#include "absl/memory/memory.h"

#include "src/core/lib/security/authorization/authorization_engine.h"

namespace grpc_core {

// Symbols for traversing Envoy Attributes
constexpr absl::string_view AuthorizationEngine::kUrlPath_;
constexpr absl::string_view AuthorizationEngine::kHost_;
constexpr absl::string_view AuthorizationEngine::kMethod_;
constexpr absl::string_view AuthorizationEngine::kHeaders_;
constexpr absl::string_view AuthorizationEngine::kSourceAddress_;
constexpr absl::string_view AuthorizationEngine::kSourcePort_;
constexpr absl::string_view AuthorizationEngine::kDestinationAddress_;
constexpr absl::string_view AuthorizationEngine::kDestinationPort_;
constexpr absl::string_view AuthorizationEngine::kSpiffeId_;
constexpr absl::string_view AuthorizationEngine::kCertServerName_;

std::unique_ptr<AuthorizationEngine>
AuthorizationEngine::CreateAuthorizationEngine(
    const std::vector<envoy_config_rbac_v3_RBAC*>& rbac_policies) {
  if (rbac_policies.empty() || rbac_policies.size() > 2) {
    gpr_log(GPR_ERROR,
            "Invalid rbac policies vector. Must contain either one or two rbac "
            "policies.");
    return nullptr;
  } else if (rbac_policies.size() == 2 &&
             (envoy_config_rbac_v3_RBAC_action(rbac_policies[0]) != kDeny ||
              envoy_config_rbac_v3_RBAC_action(rbac_policies[1]) != kAllow)) {
    gpr_log(GPR_ERROR,
            "Invalid rbac policies vector. Must contain one deny \
                         policy and one allow policy, in that order.");
    return nullptr;
  } else {
    return absl::make_unique<AuthorizationEngine>(rbac_policies);
  }
}

AuthorizationEngine::AuthorizationEngine(
    const std::vector<envoy_config_rbac_v3_RBAC*>& rbac_policies) {
  for (const auto& rbac_policy : rbac_policies) {
    // Extract array of policies and store their condition fields in either
    // allow_if_matched_ or deny_if_matched_, depending on the policy action.
    upb::Arena temp_arena;
    size_t policy_num = UPB_MAP_BEGIN;
    const envoy_config_rbac_v3_RBAC_PoliciesEntry* policy_entry;
    while ((policy_entry = envoy_config_rbac_v3_RBAC_policies_next(
                rbac_policy, &policy_num)) != nullptr) {
      const upb_strview policy_name_strview =
          envoy_config_rbac_v3_RBAC_PoliciesEntry_key(policy_entry);
      const std::string policy_name(policy_name_strview.data,
                                    policy_name_strview.size);
      const envoy_config_rbac_v3_Policy* policy =
          envoy_config_rbac_v3_RBAC_PoliciesEntry_value(policy_entry);
      const google_api_expr_v1alpha1_Expr* condition =
          envoy_config_rbac_v3_Policy_condition(policy);
      // Parse condition to make a pointer tied to the lifetime of arena_.
      size_t serial_len;
      const char* serialized = google_api_expr_v1alpha1_Expr_serialize(
          condition, temp_arena.ptr(), &serial_len);
      const google_api_expr_v1alpha1_Expr* parsed_condition =
          google_api_expr_v1alpha1_Expr_parse(serialized, serial_len,
                                              arena_.ptr());
      if (envoy_config_rbac_v3_RBAC_action(rbac_policy) == kAllow) {
        allow_if_matched_.insert(std::make_pair(policy_name, parsed_condition));
      } else {
        deny_if_matched_.insert(std::make_pair(policy_name, parsed_condition));
      }
    }
  }
}

std::unique_ptr<google::api::expr::runtime::Activation>
AuthorizationEngine::CreateActivation(const EvaluateArgs& args) {
  std::unique_ptr<google::api::expr::runtime::Activation> activation;
  for (const auto& elem : envoy_attributes_) {
    if (elem == kUrlPath_) {
      absl::string_view url_path(args.Path());
      if (url_path != "") {
        activation->InsertValue(
            kUrlPath_,
            google::api::expr::runtime::CelValue::CreateStringView(url_path));
      }
    } else if (elem == kHost_) {
      absl::string_view host(args.Host());
      if (host != "") {
        activation->InsertValue(
            kHost_,
            google::api::expr::runtime::CelValue::CreateStringView(host));
      }
    } else if (elem == kMethod_) {
      absl::string_view method(args.Method());
      if (method != "") {
        activation->InsertValue(
            kMethod_,
            google::api::expr::runtime::CelValue::CreateStringView(method));
      }
    } else if (elem == kHeaders_) {
      std::multimap<absl::string_view, absl::string_view> headers =
          args.Headers();
      std::vector<std::pair<google::api::expr::runtime::CelValue,
                            google::api::expr::runtime::CelValue>>
          header_items;
      for (const auto& header_key : header_keys_) {
        auto header_item = headers.find(header_key);
        if (header_item != headers.end()) {
          header_items.push_back(
              std::pair<google::api::expr::runtime::CelValue,
                        google::api::expr::runtime::CelValue>(
                  google::api::expr::runtime::CelValue::CreateStringView(
                      header_key),
                  google::api::expr::runtime::CelValue::CreateStringView(
                      header_item->second)));
        }
      }
      headers_ = google::api::expr::runtime::ContainerBackedMapImpl::Create(
          absl::Span<std::pair<google::api::expr::runtime::CelValue,
                               google::api::expr::runtime::CelValue>>(
              header_items));
      activation->InsertValue(
          kHeaders_,
          google::api::expr::runtime::CelValue::CreateMap(headers_.get()));
    } else if (elem == kSourceAddress_) {
      absl::string_view source_address(args.SourceAddress());
      if (source_address != "") {
        activation->InsertValue(
            kSourceAddress_,
            google::api::expr::runtime::CelValue::CreateStringView(
                source_address));
      }
    } else if (elem == kSourcePort_) {
      activation->InsertValue(
          kSourcePort_,
          google::api::expr::runtime::CelValue::CreateInt64(args.SourcePort()));
    } else if (elem == kDestinationAddress_) {
      absl::string_view destination_address(args.DestinationAddress());
      if (destination_address != "") {
        activation->InsertValue(
            kDestinationAddress_,
            google::api::expr::runtime::CelValue::CreateStringView(
                destination_address));
      }
    } else if (elem == kDestinationPort_) {
      activation->InsertValue(kDestinationPort_,
                              google::api::expr::runtime::CelValue::CreateInt64(
                                  args.DestinationPort()));
    } else if (elem == kSpiffeId_) {
      absl::string_view spiffe_id(args.SpiffeID());
      if (spiffe_id != "") {
        activation->InsertValue(
            kSpiffeId_,
            google::api::expr::runtime::CelValue::CreateStringView(spiffe_id));
      }
    } else if (elem == kCertServerName_) {
      absl::string_view cert_server_name(args.CertServerName());
      if (cert_server_name != "") {
        activation->InsertValue(
            kCertServerName_,
            google::api::expr::runtime::CelValue::CreateStringView(
                cert_server_name));
      }
    } else {
      gpr_log(GPR_ERROR,
              "Error: Authorization engine does not support evaluating "
              "attribute %s.",
              elem.c_str());
    }
  }
  return activation;
}

}  // namespace grpc_core
