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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_lb_policy_registry.h"

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/struct.upbdefs.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/arena.h"
#include "upb/json_encode.h"
#include "upb/status.h"
#include "upb/upb.hpp"
#include "xds/type/v3/typed_struct.upb.h"

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"

namespace grpc_core {

namespace {

absl::StatusOr<Json> ParseStructToJson(const XdsEncodingContext& context,
                                       const google_protobuf_Struct* resource) {
  upb::Status status;
  const auto* msg_def = google_protobuf_Struct_getmsgdef(context.symtab);
  size_t json_size = upb_JsonEncode(resource, msg_def, context.symtab, 0,
                                    nullptr, 0, status.ptr());
  if (json_size == static_cast<size_t>(-1)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Error parsing google::Protobuf::Struct: ",
                     upb_Status_ErrorMessage(status.ptr())));
  }
  void* buf = upb_Arena_Malloc(context.arena, json_size + 1);
  upb_JsonEncode(resource, msg_def, context.symtab, 0,
                 reinterpret_cast<char*>(buf), json_size + 1, status.ptr());
  auto json = Json::Parse(reinterpret_cast<char*>(buf));
  if (!json.ok()) {
    // This should not happen
    return absl::InternalError(
        absl::StrCat("Error parsing JSON form of google::Protobuf::Struct "
                     "produced by upb library: ",
                     json.status().ToString()));
  }
  return std::move(*json);
}

}  // namespace

void XdsLbPolicyRegistry::RegisterPolicy(std::unique_ptr<XdsLbPolicy> policy) {
  lb_policies_[policy->ConfigProtoType()] = std::move(policy);
}

absl::StatusOr<Json::Array> XdsLbPolicyRegistry::ConvertXdsLbPolicyConfig(
    const XdsEncodingContext& context,
    const envoy_config_cluster_v3_LoadBalancingPolicy* lb_policy,
    int recursion_depth) const {
  constexpr int kMaxRecursionDepth = 16;
  if (recursion_depth >= kMaxRecursionDepth) {
    return absl::InvalidArgumentError(
        absl::StrFormat("LoadBalancingPolicy configuration has a recursion "
                        "depth of more than %d.",
                        kMaxRecursionDepth));
  }
  size_t size = 0;
  const auto* policies =
      envoy_config_cluster_v3_LoadBalancingPolicy_policies(lb_policy, &size);
  for (size_t i = 0; i < size; ++i) {
    absl::StatusOr<Json::Object> policy;
    const auto* typed_extension_config =
        envoy_config_cluster_v3_LoadBalancingPolicy_Policy_typed_extension_config(
            policies[i]);
    if (typed_extension_config == nullptr) {
      return absl::InvalidArgumentError(
          "Error parsing LoadBalancingPolicy::Policy - Missing "
          "typed_extension_config field");
    }
    const auto* typed_config =
        envoy_config_core_v3_TypedExtensionConfig_typed_config(
            typed_extension_config);
    if (typed_config == nullptr) {
      return absl::InvalidArgumentError(
          "Error parsing LoadBalancingPolicy::Policy - Missing "
          "TypedExtensionConfig::typed_config field");
    }
    auto type = ExtractExtensionTypeName(context, typed_config);
    if (!type.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Error parsing "
          "LoadBalancingPolicy::Policy::TypedExtensionConfig::typed_config: ",
          type.status().message()));
    }
    absl::string_view value =
        UpbStringToAbsl(google_protobuf_Any_value(typed_config));
    auto config_factory_it = lb_policies_.find(type->type);
    if (config_factory_it != lb_policies_.end()) {
      policy = config_factory_it->second->ConvertXdsLbPolicyConfig(
          context, *this, value, recursion_depth);
      if (!policy.ok()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Error parsing "
                         "LoadBalancingPolicy::Policy::TypedExtensionConfig::"
                         "typed_config to JSON: ",
                         policy.status().message()));
      }
    } else if (type->typed_struct != nullptr) {
      // Custom lb policy config
      std::string custom_type = std::string(type->type);
      if (!LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(
              custom_type.c_str(), nullptr)) {
        // Skip unsupported custom lb policy.
        continue;
      }
      // Convert typed struct to json.
      auto value = xds_type_v3_TypedStruct_value(type->typed_struct);
      if (value == nullptr) {
        policy = Json::Object{{std::move(custom_type), Json() /* null */}};
      } else {
        auto parsed_value = ParseStructToJson(context, value);
        if (!parsed_value.ok()) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Error parsing LoadBalancingPolicy: Custom Policy: ", custom_type,
              ": ", parsed_value.status().message()));
        }
        policy =
            Json::Object{{std::move(custom_type), *(std::move(parsed_value))}};
      }
    } else {
      // Unsupported type. Skipping entry.
      continue;
    }
    return Json::Array{std::move(policy.value())};
  }
  return absl::InvalidArgumentError(
      "No supported load balancing policy config found.");
}

}  // namespace grpc_core
