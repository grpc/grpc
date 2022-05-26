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

#include <vector>

#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb.h"
#include "envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/struct.upbdefs.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/json_encode.h"
#include "xds/type/v3/typed_struct.upb.h"

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

namespace {

class RingHashLbPolicyConfigFactory
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  absl::StatusOr<Json::Object> ConvertXdsLbPolicyConfig(
      const XdsEncodingContext& context, upb_StringView configuration,
      int recursion_depth) {
    const auto* resource =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_parse(
            configuration.data, configuration.size, context.arena);
    if (resource == nullptr) {
      return absl::InvalidArgumentError(
          "Can't decode RingHash loadbalancing policy");
    }
    if (envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_hash_function(
            resource) !=
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_XX_HASH) {
      return absl::InvalidArgumentError(
          "Invalid hash function provided for RingHash loadbalancing policy. "
          "Only XX_HASH is supported.");
    }
    Json::Object json;
    const auto* min_ring_size =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_minimum_ring_size(
            resource);
    if (min_ring_size != nullptr) {
      json.emplace("minRingSize",
                   google_protobuf_UInt64Value_value(min_ring_size));
    }
    const auto* max_ring_size =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_maximum_ring_size(
            resource);
    if (max_ring_size != nullptr) {
      json.emplace("maxRingSize",
                   google_protobuf_UInt64Value_value(max_ring_size));
    }
    return Json::Object{{"ring_hash_experimental", std::move(json)}};
  }

  static absl::string_view type() {
    return "envoy.extensions.load_balancing_policies.ring_hash.v3.RingHash";
  }
};

class RoundRobinLbPolicyConfigFactory
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  absl::StatusOr<Json::Object> ConvertXdsLbPolicyConfig(
      const XdsEncodingContext& /* context */,
      upb_StringView /* configuration */, int recursion_depth) {
    return Json::Object{{"round_robin", Json::Object()}};
  }

  static absl::string_view type() {
    return "envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin";
  }
};

class WrrLocalityLbPolicyConfigFactory
    : public XdsLbPolicyRegistry::ConfigFactory {
 public:
  absl::StatusOr<Json::Object> ConvertXdsLbPolicyConfig(
      const XdsEncodingContext& context, upb_StringView configuration,
      int recursion_depth) {
    const auto* resource =
        envoy_extensions_load_balancing_policies_wrr_locality_v3_WrrLocality_parse(
            configuration.data, configuration.size, context.arena);
    if (resource == nullptr) {
      return absl::InvalidArgumentError(
          "Can't decode WrrLocality loadbalancing policy");
    }
    const auto* endpoint_picking_policy =
        envoy_extensions_load_balancing_policies_wrr_locality_v3_WrrLocality_endpoint_picking_policy(
            resource);
    if (endpoint_picking_policy == nullptr) {
      return absl::InvalidArgumentError(
          "WrrLocality: endpoint_picking_policy not found");
    }
    auto child_policy = XdsLbPolicyRegistry::ConvertXdsLbPolicyConfig(
        context, endpoint_picking_policy, recursion_depth + 1);
    if (!child_policy.ok()) {
      return StatusCreate(absl::StatusCode::kInvalidArgument,
                          "Error parsing WrrLocality load balancing policy",
                          DEBUG_LOCATION, {child_policy.status()});
    }
    return Json::Object{
        {"xds_wrr_locality_experimental",
         Json::Object{{"child_policy", *std::move(child_policy)}}}};
  }

  static absl::string_view type() {
    return "envoy.extensions.load_balancing_policies.wrr_locality.v3."
           "WrrLocality";
  }
};

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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto json = Json::Parse(reinterpret_cast<char*>(buf), &error);
  if (error != GRPC_ERROR_NONE) {
    // This should not happen
    auto ret_status = absl::InternalError(
        absl::StrCat("Error parsing JSON form of google::Protobuf::Struct "
                     "produced by upb library",
                     grpc_error_std_string(error)));
    GRPC_ERROR_UNREF(error);
    return ret_status;
  }
  return json;
}

}  // namespace

//
// XdsLbPolicyRegistry
//

absl::StatusOr<Json::Array> XdsLbPolicyRegistry::ConvertXdsLbPolicyConfig(
    const XdsEncodingContext& context,
    const envoy_config_cluster_v3_LoadBalancingPolicy* lb_policy,
    int recursion_depth) {
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
  absl::StatusOr<Json::Object> policy = absl::InvalidArgumentError(
      "No supported load balancing policy config found.");
  for (size_t i = 0; i < size; ++i) {
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
    absl::string_view type_url;
    xds_type_v3_TypedStruct* typed_struct = nullptr;
    auto error = ExtractExtensionTypeName(context, typed_config, &type_url,
                                          &typed_struct);
    if (error != GRPC_ERROR_NONE) {
      policy = grpc_error_to_absl_status(error);
      GRPC_ERROR_UNREF(error);
      break;
    }
    upb_StringView value = google_protobuf_Any_value(typed_config);
    auto config_factory_it = Get()->policy_config_factories_.find(type_url);
    if (config_factory_it != Get()->policy_config_factories_.end()) {
      policy = config_factory_it->second->ConvertXdsLbPolicyConfig(
          context, value, recursion_depth);
    } else if (typed_struct != nullptr) {
      // Custom lb policy config
      absl::string_view custom_type_url =
          UpbStringToAbsl(xds_type_v3_TypedStruct_type_url(typed_struct));
      size_t pos = custom_type_url.rfind('/');
      if (pos == absl::string_view::npos) {
        return absl::InvalidArgumentError(
            absl::StrCat("Error parsing LoadBalancingPolicy: Custom Policy: "
                         "Invalid type_url ",
                         custom_type_url));
      }
      std::string name = std::string(type_url.substr(pos + 1));
      if (!LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(name.c_str(),
                                                                  nullptr)) {
        continue;
      }
      auto value = xds_type_v3_TypedStruct_value(typed_struct);
      if (value == nullptr) {
        policy = Json::Object{{std::string(name), Json() /* null */}};
      } else {
        auto parsed_value = ParseStructToJson(context, value);
        if (!parsed_value.ok()) {
          return StatusCreate(
              absl::StatusCode::kInvalidArgument,
              absl::StrCat("Error parsing LoadBalancingPolicy: Custom Policy: ",
                           type_url),
              DEBUG_LOCATION, {parsed_value.status()});
        }
        policy = Json::Object{{std::string(name), *(std::move(parsed_value))}};
      }
    } else {
      // Unsupported type. Skipping entry.
    }
    if (policy.ok()) {
      return Json::Array{std::move(policy.value())};
    }
  }
  return StatusCreate(absl::StatusCode::kInvalidArgument,
                      "Error parsing LoadBalancingPolicy", DEBUG_LOCATION,
                      {policy.status()});
}

XdsLbPolicyRegistry* XdsLbPolicyRegistry::Get() {
  // This is thread-safe since C++11
  static XdsLbPolicyRegistry* instance = new XdsLbPolicyRegistry();
  instance->policy_config_factories_.emplace(
      RingHashLbPolicyConfigFactory::type(),
      absl::make_unique<RingHashLbPolicyConfigFactory>());
  instance->policy_config_factories_.emplace(
      RoundRobinLbPolicyConfigFactory::type(),
      absl::make_unique<RoundRobinLbPolicyConfigFactory>());
  instance->policy_config_factories_.emplace(
      WrrLocalityLbPolicyConfigFactory::type(),
      absl::make_unique<WrrLocalityLbPolicyConfigFactory>());
  return instance;
}

}  // namespace grpc_core
