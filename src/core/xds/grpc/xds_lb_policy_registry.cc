//
// Copyright 2022 gRPC authors.
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
//

#include "src/core/xds/grpc/xds_lb_policy_registry.h"

#include <string>
#include <utility>
#include <variant>

#include "envoy/config/core/v3/extension.upb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

Json::Array XdsLbPolicyRegistry::ConvertXdsLbPolicyConfig(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_cluster_v3_LoadBalancingPolicy* lb_policy,
    ValidationErrors* errors, int recursion_depth) const {
  constexpr int kMaxRecursionDepth = 16;
  if (recursion_depth >= kMaxRecursionDepth) {
    errors->AddError(
        absl::StrCat("exceeded max recursion depth of ", kMaxRecursionDepth));
    return {};
  }
  const size_t original_error_size = errors->size();
  size_t size = 0;
  const auto* policies =
      envoy_config_cluster_v3_LoadBalancingPolicy_policies(lb_policy, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".policies[", i, "].typed_extension_config"));
    const auto* typed_extension_config =
        envoy_config_cluster_v3_LoadBalancingPolicy_Policy_typed_extension_config(
            policies[i]);
    if (typed_extension_config == nullptr) {
      errors->AddError("field not present");
      return {};
    }
    ValidationErrors::ScopedField field2(errors, ".typed_config");
    const auto* typed_config =
        envoy_config_core_v3_TypedExtensionConfig_typed_config(
            typed_extension_config);
    auto extension = ExtractXdsExtension(context, typed_config, errors);
    if (!extension.has_value()) return {};
    // Check for registered LB policy type.
    absl::string_view* serialized_value =
        std::get_if<absl::string_view>(&extension->value);
    if (serialized_value != nullptr) {
      auto config_factory_it = policy_config_factories_.find(extension->type);
      if (config_factory_it != policy_config_factories_.end()) {
        return Json::Array{Json::FromObject(
            config_factory_it->second->ConvertXdsLbPolicyConfig(
                this, context, *serialized_value, errors, recursion_depth))};
      }
    }
    // Check for custom LB policy type.
    Json* json = std::get_if<Json>(&extension->value);
    if (json != nullptr &&
        CoreConfiguration::Get().lb_policy_registry().LoadBalancingPolicyExists(
            extension->type, nullptr)) {
      return Json::Array{
          Json::FromObject({{std::string(extension->type), std::move(*json)}})};
    }
    // Unsupported type.  Continue to next entry.
  }
  if (original_error_size == errors->size()) {
    errors->AddError("no supported load balancing policy config found");
  }
  return {};
}

}  // namespace grpc_core
