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

#include "src/core/lib/security/authorization/cel_authorization_engine.h"

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <algorithm>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "upb/base/string_view.h"
#include "upb/message/map.h"

namespace grpc_core {

namespace {

// Symbols for traversing Envoy Attributes
constexpr char kUrlPath[] = "url_path";
constexpr char kHost[] = "host";
constexpr char kMethod[] = "method";
constexpr char kHeaders[] = "headers";
constexpr char kSourceAddress[] = "source_address";
constexpr char kSourcePort[] = "source_port";
constexpr char kDestinationAddress[] = "destination_address";
constexpr char kDestinationPort[] = "destination_port";
constexpr char kSpiffeId[] = "spiffe_id";
constexpr char kCertServerName[] = "cert_server_name";

}  // namespace

std::unique_ptr<CelAuthorizationEngine>
CelAuthorizationEngine::CreateCelAuthorizationEngine(
    const std::vector<envoy_config_rbac_v3_RBAC*>& rbac_policies) {
  if (rbac_policies.empty() || rbac_policies.size() > 2) {
    LOG(ERROR) << "Invalid rbac policies vector. Must contain either one or "
                  "two rbac policies.";
    return nullptr;
  } else if (rbac_policies.size() == 2 &&
             (envoy_config_rbac_v3_RBAC_action(rbac_policies[0]) != kDeny ||
              envoy_config_rbac_v3_RBAC_action(rbac_policies[1]) != kAllow)) {
    LOG(ERROR) << "Invalid rbac policies vector. Must contain one deny policy "
                  "and one allow policy, in that order.";
    return nullptr;
  } else {
    return std::make_unique<CelAuthorizationEngine>(rbac_policies);
  }
}

CelAuthorizationEngine::CelAuthorizationEngine(
    const std::vector<envoy_config_rbac_v3_RBAC*>& rbac_policies) {
  for (const auto& rbac_policy : rbac_policies) {
    // Extract array of policies and store their condition fields in either
    // allow_if_matched_ or deny_if_matched_, depending on the policy action.
    upb::Arena temp_arena;
    // TODO(b/397931390): Clean up the code after gRPC OSS migrates to proto
    // v30.0.
    const upb_Map* upb_map =
        _envoy_config_rbac_v3_RBAC_policies_upb_map(rbac_policy);
    if (upb_map) {
      size_t policy_num = kUpb_Map_Begin;
      upb_MessageValue k, v;
      while (upb_Map_Next(upb_map, &k, &v, &policy_num)) {
        upb_StringView policy_name_strview = k.str_val;
        const envoy_config_rbac_v3_Policy* policy =
            (envoy_config_rbac_v3_Policy*)v.msg_val;
        const std::string policy_name(policy_name_strview.data,
                                      policy_name_strview.size);
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
          allow_if_matched_.insert(std::pair(policy_name, parsed_condition));
        } else {
          deny_if_matched_.insert(std::pair(policy_name, parsed_condition));
        }
      }
    }
  }
}

std::unique_ptr<mock_cel::Activation> CelAuthorizationEngine::CreateActivation(
    const EvaluateArgs& args) {
  std::unique_ptr<mock_cel::Activation> activation;
  for (const auto& elem : envoy_attributes_) {
    if (elem == kUrlPath) {
      absl::string_view url_path(args.GetPath());
      if (!url_path.empty()) {
        activation->InsertValue(kUrlPath,
                                mock_cel::CelValue::CreateStringView(url_path));
      }
    } else if (elem == kHost) {
      absl::string_view host(args.GetAuthority());
      if (!host.empty()) {
        activation->InsertValue(kHost,
                                mock_cel::CelValue::CreateStringView(host));
      }
    } else if (elem == kMethod) {
      absl::string_view method(args.GetMethod());
      if (!method.empty()) {
        activation->InsertValue(kMethod,
                                mock_cel::CelValue::CreateStringView(method));
      }
    } else if (elem == kHeaders) {
      std::vector<std::pair<mock_cel::CelValue, mock_cel::CelValue>>
          header_items;
      for (const auto& header_key : header_keys_) {
        std::string temp_value;
        std::optional<absl::string_view> header_value =
            args.GetHeaderValue(header_key, &temp_value);
        if (header_value.has_value()) {
          header_items.push_back(
              std::pair<mock_cel::CelValue, mock_cel::CelValue>(
                  mock_cel::CelValue::CreateStringView(header_key),
                  mock_cel::CelValue::CreateStringView(*header_value)));
        }
      }
      headers_ = mock_cel::ContainerBackedMapImpl::Create(
          absl::Span<std::pair<mock_cel::CelValue, mock_cel::CelValue>>(
              header_items));
      activation->InsertValue(kHeaders,
                              mock_cel::CelValue::CreateMap(headers_.get()));
    } else if (elem == kSourceAddress) {
      absl::string_view source_address(args.GetPeerAddressString());
      if (!source_address.empty()) {
        activation->InsertValue(
            kSourceAddress,
            mock_cel::CelValue::CreateStringView(source_address));
      }
    } else if (elem == kSourcePort) {
      activation->InsertValue(
          kSourcePort, mock_cel::CelValue::CreateInt64(args.GetPeerPort()));
    } else if (elem == kDestinationAddress) {
      absl::string_view destination_address(args.GetLocalAddressString());
      if (!destination_address.empty()) {
        activation->InsertValue(
            kDestinationAddress,
            mock_cel::CelValue::CreateStringView(destination_address));
      }
    } else if (elem == kDestinationPort) {
      activation->InsertValue(kDestinationPort, mock_cel::CelValue::CreateInt64(
                                                    args.GetLocalPort()));
    } else if (elem == kSpiffeId) {
      absl::string_view spiffe_id(args.GetSpiffeId());
      if (!spiffe_id.empty()) {
        activation->InsertValue(
            kSpiffeId, mock_cel::CelValue::CreateStringView(spiffe_id));
      }
    } else if (elem == kCertServerName) {
      absl::string_view cert_server_name(args.GetCommonName());
      if (!cert_server_name.empty()) {
        activation->InsertValue(
            kCertServerName,
            mock_cel::CelValue::CreateStringView(cert_server_name));
      }
    } else {
      LOG(ERROR) << "Error: Authorization engine does not support evaluating "
                    "attribute "
                 << elem;
    }
  }
  return activation;
}

}  // namespace grpc_core
