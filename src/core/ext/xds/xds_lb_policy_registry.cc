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
#include "google/protobuf/wrappers.upb.h"
#include "xds/type/v3/typed_struct.upb.h"

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/xds/upb_utils.h"

namespace grpc_core {

namespace {

TraceFlag grpc_xds_lb_registry(false, "xds_lb_registry");

// An individual lb policy that knows how to convert xDS configurations of its
// type to gRPC's JSON form.
class LbPolicy {
 public:
  // Converts the proto serialized form of the configuration \a configuration to
  // a Json::Object, or returns an error if conversion fails. If this lb policy
  // can hold other lb policies, \a recursion_depth indicates the current depth
  // of the tree.
  virtual absl::StatusOr<Json::Object> ToJson(absl::string_view configuration,
                                              int recursion_depth) = 0;
  // The lb policy config type that this object expects,
  // 'envoy.extensions.load_balancing_policies.ring_hash.v3.RingHash' or
  // 'envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin' for
  // example.
  virtual const char* type() = 0;
};

// Converts an xDS cluster load balancing policy message to gRPC's JSON format.
// An error is returned if none of the lb policies in the list are supported, or
// if a supported lb policy configuration conversion fails. \a recursion_depth
// indicates the current depth of the tree if lb_policy configuration
// recursively holds other lb policies.
absl::StatusOr<Json::Array> ToJsonInternal(
    const envoy_config_cluster_v3_LoadBalancingPolicy* lb_policy,
    upb_Arena* arena, int recursion_depth);

class RingHashLbPolicy {
 public:
  static absl::optional<absl::StatusOr<Json::Object>> ToJson(
      upb_StringView configuration, upb_Arena* arena) {
    const auto* resource =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_parse(
            configuration.data, configuration.size, arena);
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
    const auto* min_ring_size =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_minimum_ring_size(
            resource);
    const auto* max_ring_size =
        envoy_extensions_load_balancing_policies_ring_hash_v3_RingHash_maximum_ring_size(
            resource);
    constexpr uint64_t kMaxRingSize = 8 * 1024 * 1024;
    return Json::Object{
        {"ring_hash_experimental",
         Json::Object{
             {"minRingSize",
              std::min(min_ring_size == nullptr
                           ? 1024
                           : google_protobuf_UInt64Value_value(min_ring_size),
                       kMaxRingSize)},
             {"maxRingSize",
              std::min(max_ring_size == nullptr
                           ? kMaxRingSize
                           : google_protobuf_UInt64Value_value(max_ring_size),
                       kMaxRingSize)}}}};
  }

  static const char* type() {
    return "type.googleapis.com/"
           "envoy.extensions.load_balancing_policies.ring_hash.v3.RingHash";
  }
};

class RoundRobinLbPolicy {
 public:
  static absl::optional<absl::StatusOr<Json::Object>> ToJson(
      upb_StringView /* configuration */, upb_Arena* /* arena */) {
    return Json::Object{{"round_robin", Json::Object()}};
  }

  static const char* type() {
    return "type.googleapis.com/"
           "envoy.extensions.load_balancing_policies.round_robin.v3.RoundRobin";
  }
};

class WrrLocalityLbPolicy {
 public:
  static absl::optional<absl::StatusOr<Json::Object>> ToJson(
      upb_StringView configuration, upb_Arena* arena, int recursion_depth) {
    const auto* resource =
        envoy_extensions_load_balancing_policies_wrr_locality_v3_WrrLocality_parse(
            configuration.data, configuration.size, arena);
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
    auto child_policy =
        ToJsonInternal(endpoint_picking_policy, arena, recursion_depth + 1);
    if (!child_policy.ok()) {
      return StatusCreate(absl::StatusCode::kInvalidArgument,
                          "Error parsing WrrLocality load balancing policy",
                          DEBUG_LOCATION, {child_policy.status()});
    }
    return Json::Object{
        {"xds_wrr_locality_experimental",
         Json::Object{{"child_policy", *std::move(child_policy)}}}};
  }

  static const char* type() {
    return "type.googleapis.com/"
           "envoy.extensions.load_balancing_policies.wrr_locality.v3."
           "WrrLocality";
  }
};

absl::StatusOr<Json> ParseStructToJson(const google_protobuf_Struct* resource);
absl::StatusOr<Json> ParseListValueToJson(
    const google_protobuf_ListValue* list_value);

absl::StatusOr<Json> ParseValueToJson(const google_protobuf_Value* value) {
  GPR_DEBUG_ASSERT(value != nullptr);
  if (google_protobuf_Value_has_null_value(value)) {
    return Json();
  } else if (google_protobuf_Value_has_number_value(value)) {
    return Json(std::to_string(google_protobuf_Value_number_value(value)),
                true /* is_number */);
  } else if (google_protobuf_Value_has_string_value(value)) {
    return Json(
        UpbStringToStdString(google_protobuf_Value_string_value(value)));
  } else if (google_protobuf_Value_has_bool_value(value)) {
    return (google_protobuf_Value_bool_value(value));
  } else if (google_protobuf_Value_has_struct_value(value)) {
    return ParseStructToJson(google_protobuf_Value_struct_value(value));
  } else if (google_protobuf_Value_has_list_value(value)) {
    return ParseListValueToJson(google_protobuf_Value_list_value(value));
  }
  return absl::InvalidArgumentError("Invalid value type");
}

absl::StatusOr<Json> ParseListValueToJson(
    const google_protobuf_ListValue* list_value) {
  GPR_DEBUG_ASSERT(list_value != nullptr);
  std::vector<absl::Status> errors;
  size_t size;
  Json::Array array;
  const auto* values = google_protobuf_ListValue_values(list_value, &size);
  for (size_t i = 0; i < size; ++i) {
    auto value = ParseValueToJson(values[i]);
    if (!value.ok()) {
      errors.push_back(value.status());
    } else {
      array.push_back(*std::move(value));
    }
  }
  if (!errors.empty()) {
    return StatusCreate(absl::StatusCode::kInvalidArgument,
                        "Error parsing ListValue", DEBUG_LOCATION,
                        std::move(errors));
  }
  return array;
}

absl::StatusOr<Json> ParseStructToJson(const google_protobuf_Struct* resource) {
  GPR_DEBUG_ASSERT(resource != nullptr);
  Json::Object fields;
  std::vector<absl::Status> errors;
  size_t fields_it = kUpb_Map_Begin;
  while (true) {
    const auto* field =
        google_protobuf_Struct_fields_next(resource, &fields_it);
    if (field == nullptr) break;
    absl::string_view key =
        UpbStringToAbsl(google_protobuf_Struct_FieldsEntry_key(field));
    const auto* value = google_protobuf_Struct_FieldsEntry_value(field);
    if (value == nullptr) {
      return absl::InvalidArgumentError(
          absl::StrCat("No value found for key:", key));
    }
    auto parsed_value = ParseValueToJson(value);
    if (!parsed_value.ok()) {
      errors.push_back(
          StatusCreate(absl::StatusCode::kInvalidArgument,
                       absl::StrCat("Failed to parse value for key:", key),
                       DEBUG_LOCATION, {parsed_value.status()}));
    } else {
      fields.emplace(key, *std::move(parsed_value));
    }
  }
  if (!errors.empty()) {
    return StatusCreate(absl::StatusCode::kInvalidArgument,
                        "Failed to parse Struct", DEBUG_LOCATION,
                        std::move(errors));
  }
  return fields;
}

class CustomLbPolicy {
 public:
  static absl::optional<absl::StatusOr<Json::Object>> ToJson(
      upb_StringView configuration, upb_Arena* arena) {
    const auto* resource = xds_type_v3_TypedStruct_parse(
        configuration.data, configuration.size, arena);
    if (resource == nullptr) {
      return absl::InvalidArgumentError(
          "Can't decode Custom loadbalancing policy");
    }
    absl::string_view type_url =
        UpbStringToAbsl(xds_type_v3_TypedStruct_type_url(resource));
    size_t pos = type_url.rfind('/');
    if (pos == absl::string_view::npos) {
      return absl::InvalidArgumentError(
          absl::StrCat("CustomLbPolicy: Invalid type_url ", type_url));
    }
    std::string name = std::string(type_url.substr(pos + 1));
    if (!LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(name.c_str(),
                                                                nullptr)) {
      return absl::optional<absl::StatusOr<Json::Object>>();
    }
    auto value = xds_type_v3_TypedStruct_value(resource);
    if (value == nullptr) {
      return Json::Object{{std::string(name), Json() /* null */}};
    }
    auto parsed_value = ParseStructToJson(value);
    if (!parsed_value.ok()) {
      return StatusCreate(
          absl::StatusCode::kInvalidArgument,
          absl::StrCat("Error parsing Custom load balancing policy ", type_url),
          DEBUG_LOCATION, {parsed_value.status()});
    }
    return Json::Object{{std::string(name), *(std::move(parsed_value))}};
  }

  static const char* type() {
    return "type.googleapis.com/xds.type.v3.TypedStruct";
  }
};

// Converts an xDS cluster load balancing policy message to gRPC's JSON format.
// An error is returned if none of the lb policies in the list are supported, or
// if a supported lb policy configuration conversion fails. \a recursion_depth
// indicates the current depth of the tree if lb_policy configuration
// recursively holds other lb policies.
absl::StatusOr<Json::Array> ToJsonInternal(
    const envoy_config_cluster_v3_LoadBalancingPolicy* lb_policy,
    upb_Arena* arena, int recursion_depth) {
  constexpr int kMaxRecursionDepth = 16;
  if (recursion_depth > kMaxRecursionDepth) {
    return absl::InvalidArgumentError(
        absl::StrFormat("LoadBalancingPolicy configuration has a recursion "
                        "depth of more than %d.",
                        kMaxRecursionDepth));
  }
  size_t size = 0;
  const auto* policies =
      envoy_config_cluster_v3_LoadBalancingPolicy_policies(lb_policy, &size);
  absl::optional<absl::StatusOr<Json::Object>> policy;
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
    absl::string_view type_url =
        UpbStringToAbsl(google_protobuf_Any_type_url(typed_config));
    upb_StringView value = google_protobuf_Any_value(typed_config);
    if (type_url == RingHashLbPolicy::type()) {
      policy = RingHashLbPolicy::ToJson(value, arena);
    } else if (type_url == RoundRobinLbPolicy::type()) {
      policy = RoundRobinLbPolicy::ToJson(value, arena);
    } else if (type_url == WrrLocalityLbPolicy::type()) {
      policy = WrrLocalityLbPolicy::ToJson(value, arena, recursion_depth);
    } else if (type_url == CustomLbPolicy::type()) {
      policy = CustomLbPolicy::ToJson(value, arena);
    } else {
      // Unsupported type. Skipping entry.
    }
    if (policy.has_value()) {
      break;
    }
  }
  if (!policy.has_value()) {
    return absl::InvalidArgumentError("No supported LoadBalancingPolicy found");
  }
  if (!policy->ok()) {
    return StatusCreate(absl::StatusCode::kInvalidArgument,
                        "Error parsing LoadBalancingPolicy", DEBUG_LOCATION,
                        {policy->status()});
  }
  return Json::Array{*std::move(policy.value())};
}

}  // namespace

absl::StatusOr<Json::Array> XdsLbPolicyRegistry::ToJson(
    const envoy_config_cluster_v3_LoadBalancingPolicy* lb_policy,
    upb_Arena* arena) {
  return ToJsonInternal(lb_policy, arena, 1);
}

}  // namespace grpc_core
