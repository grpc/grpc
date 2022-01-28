//
// Copyright 2018 gRPC authors.
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

#include "src/core/ext/xds/xds_endpoint.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/health_check.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upbdefs.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/text_encode.h"
#include "upb/upb.h"
#include "upb/upb.hpp"

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"

namespace grpc_core {

//
// XdsEndpointResource
//

std::string XdsEndpointResource::Priority::Locality::ToString() const {
  std::vector<std::string> endpoint_strings;
  for (const ServerAddress& endpoint : endpoints) {
    endpoint_strings.emplace_back(endpoint.ToString());
  }
  return absl::StrCat("{name=", name->AsHumanReadableString(),
                      ", lb_weight=", lb_weight, ", endpoints=[",
                      absl::StrJoin(endpoint_strings, ", "), "]}");
}

bool XdsEndpointResource::Priority::operator==(const Priority& other) const {
  if (localities.size() != other.localities.size()) return false;
  auto it1 = localities.begin();
  auto it2 = other.localities.begin();
  while (it1 != localities.end()) {
    if (*it1->first != *it2->first) return false;
    if (it1->second != it2->second) return false;
    ++it1;
    ++it2;
  }
  return true;
}

std::string XdsEndpointResource::Priority::ToString() const {
  std::vector<std::string> locality_strings;
  for (const auto& p : localities) {
    locality_strings.emplace_back(p.second.ToString());
  }
  return absl::StrCat("[", absl::StrJoin(locality_strings, ", "), "]");
}

bool XdsEndpointResource::DropConfig::ShouldDrop(
    const std::string** category_name) const {
  for (size_t i = 0; i < drop_category_list_.size(); ++i) {
    const auto& drop_category = drop_category_list_[i];
    // Generate a random number in [0, 1000000).
    const uint32_t random = static_cast<uint32_t>(rand()) % 1000000;
    if (random < drop_category.parts_per_million) {
      *category_name = &drop_category.name;
      return true;
    }
  }
  return false;
}

std::string XdsEndpointResource::DropConfig::ToString() const {
  std::vector<std::string> category_strings;
  for (const DropCategory& category : drop_category_list_) {
    category_strings.emplace_back(
        absl::StrCat(category.name, "=", category.parts_per_million));
  }
  return absl::StrCat("{[", absl::StrJoin(category_strings, ", "),
                      "], drop_all=", drop_all_, "}");
}

std::string XdsEndpointResource::ToString() const {
  std::vector<std::string> priority_strings;
  for (size_t i = 0; i < priorities.size(); ++i) {
    const Priority& priority = priorities[i];
    priority_strings.emplace_back(
        absl::StrCat("priority ", i, ": ", priority.ToString()));
  }
  return absl::StrCat("priorities=[", absl::StrJoin(priority_strings, ", "),
                      "], drop_config=", drop_config->ToString());
}

//
// XdsEndpointResourceType
//

namespace {

void MaybeLogClusterLoadAssignment(
    const XdsEncodingContext& context,
    const envoy_config_endpoint_v3_ClusterLoadAssignment* cla) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_msgdef* msg_type =
        envoy_config_endpoint_v3_ClusterLoadAssignment_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_text_encode(cla, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] ClusterLoadAssignment: %s",
            context.client, buf);
  }
}

grpc_error_handle ServerAddressParseAndAppend(
    const envoy_config_endpoint_v3_LbEndpoint* lb_endpoint,
    ServerAddressList* list) {
  // If health_status is not HEALTHY or UNKNOWN, skip this endpoint.
  const int32_t health_status =
      envoy_config_endpoint_v3_LbEndpoint_health_status(lb_endpoint);
  if (health_status != envoy_config_core_v3_UNKNOWN &&
      health_status != envoy_config_core_v3_HEALTHY) {
    return GRPC_ERROR_NONE;
  }
  // Find the ip:port.
  const envoy_config_endpoint_v3_Endpoint* endpoint =
      envoy_config_endpoint_v3_LbEndpoint_endpoint(lb_endpoint);
  const envoy_config_core_v3_Address* address =
      envoy_config_endpoint_v3_Endpoint_address(endpoint);
  const envoy_config_core_v3_SocketAddress* socket_address =
      envoy_config_core_v3_Address_socket_address(address);
  std::string address_str = UpbStringToStdString(
      envoy_config_core_v3_SocketAddress_address(socket_address));
  uint32_t port = envoy_config_core_v3_SocketAddress_port_value(socket_address);
  if (GPR_UNLIKELY(port >> 16) != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid port.");
  }
  // Find load_balancing_weight for the endpoint.
  const google_protobuf_UInt32Value* load_balancing_weight =
      envoy_config_endpoint_v3_LbEndpoint_load_balancing_weight(lb_endpoint);
  const int32_t weight =
      load_balancing_weight != nullptr
          ? google_protobuf_UInt32Value_value(load_balancing_weight)
          : 500;
  if (weight == 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid endpoint weight of 0.");
  }
  // Populate grpc_resolved_address.
  grpc_resolved_address addr;
  grpc_error_handle error =
      grpc_string_to_sockaddr(&addr, address_str.c_str(), port);
  if (error != GRPC_ERROR_NONE) return error;
  // Append the address to the list.
  std::map<const char*, std::unique_ptr<ServerAddress::AttributeInterface>>
      attributes;
  attributes[ServerAddressWeightAttribute::kServerAddressWeightAttributeKey] =
      absl::make_unique<ServerAddressWeightAttribute>(weight);
  list->emplace_back(addr, nullptr, std::move(attributes));
  return GRPC_ERROR_NONE;
}

grpc_error_handle LocalityParse(
    const envoy_config_endpoint_v3_LocalityLbEndpoints* locality_lb_endpoints,
    XdsEndpointResource::Priority::Locality* output_locality,
    size_t* priority) {
  // Parse LB weight.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  // If LB weight is not specified, it means this locality is assigned no load.
  // TODO(juanlishen): When we support CDS to configure the inter-locality
  // policy, we should change the LB weight handling.
  output_locality->lb_weight =
      lb_weight != nullptr ? google_protobuf_UInt32Value_value(lb_weight) : 0;
  if (output_locality->lb_weight == 0) return GRPC_ERROR_NONE;
  // Parse locality name.
  const envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_LocalityLbEndpoints_locality(
          locality_lb_endpoints);
  if (locality == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty locality.");
  }
  std::string region =
      UpbStringToStdString(envoy_config_core_v3_Locality_region(locality));
  std::string zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_region(locality));
  std::string sub_zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_sub_zone(locality));
  output_locality->name = MakeRefCounted<XdsLocalityName>(
      std::move(region), std::move(zone), std::move(sub_zone));
  // Parse the addresses.
  size_t size;
  const envoy_config_endpoint_v3_LbEndpoint* const* lb_endpoints =
      envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    grpc_error_handle error = ServerAddressParseAndAppend(
        lb_endpoints[i], &output_locality->endpoints);
    if (error != GRPC_ERROR_NONE) return error;
  }
  // Parse the priority.
  *priority = envoy_config_endpoint_v3_LocalityLbEndpoints_priority(
      locality_lb_endpoints);
  return GRPC_ERROR_NONE;
}

grpc_error_handle DropParseAndAppend(
    const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload*
        drop_overload,
    XdsEndpointResource::DropConfig* drop_config) {
  // Get the category.
  std::string category = UpbStringToStdString(
      envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_category(
          drop_overload));
  if (category.empty()) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty drop category name");
  }
  // Get the drop rate (per million).
  const envoy_type_v3_FractionalPercent* drop_percentage =
      envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_drop_percentage(
          drop_overload);
  uint32_t numerator =
      envoy_type_v3_FractionalPercent_numerator(drop_percentage);
  const auto denominator =
      static_cast<envoy_type_v3_FractionalPercent_DenominatorType>(
          envoy_type_v3_FractionalPercent_denominator(drop_percentage));
  // Normalize to million.
  switch (denominator) {
    case envoy_type_v3_FractionalPercent_HUNDRED:
      numerator *= 10000;
      break;
    case envoy_type_v3_FractionalPercent_TEN_THOUSAND:
      numerator *= 100;
      break;
    case envoy_type_v3_FractionalPercent_MILLION:
      break;
    default:
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unknown denominator type");
  }
  // Cap numerator to 1000000.
  numerator = std::min(numerator, 1000000u);
  drop_config->AddCategory(std::move(category), numerator);
  return GRPC_ERROR_NONE;
}

grpc_error_handle EdsResourceParse(
    const XdsEncodingContext& /*context*/,
    const envoy_config_endpoint_v3_ClusterLoadAssignment*
        cluster_load_assignment,
    bool /*is_v2*/, XdsEndpointResource* eds_update) {
  std::vector<grpc_error_handle> errors;
  // Get the endpoints.
  size_t locality_size;
  const envoy_config_endpoint_v3_LocalityLbEndpoints* const* endpoints =
      envoy_config_endpoint_v3_ClusterLoadAssignment_endpoints(
          cluster_load_assignment, &locality_size);
  for (size_t j = 0; j < locality_size; ++j) {
    size_t priority;
    XdsEndpointResource::Priority::Locality locality;
    grpc_error_handle error = LocalityParse(endpoints[j], &locality, &priority);
    if (error != GRPC_ERROR_NONE) {
      errors.push_back(error);
      continue;
    }
    // Filter out locality with weight 0.
    if (locality.lb_weight == 0) continue;
    // Make sure prorities is big enough. Note that they might not
    // arrive in priority order.
    while (eds_update->priorities.size() < priority + 1) {
      eds_update->priorities.emplace_back();
    }
    eds_update->priorities[priority].localities.emplace(locality.name.get(),
                                                        std::move(locality));
  }
  for (const auto& priority : eds_update->priorities) {
    if (priority.localities.empty()) {
      errors.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("sparse priority list"));
    }
  }
  // Get the drop config.
  eds_update->drop_config = MakeRefCounted<XdsEndpointResource::DropConfig>();
  const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy* policy =
      envoy_config_endpoint_v3_ClusterLoadAssignment_policy(
          cluster_load_assignment);
  if (policy != nullptr) {
    size_t drop_size;
    const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload* const*
        drop_overload =
            envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_drop_overloads(
                policy, &drop_size);
    for (size_t j = 0; j < drop_size; ++j) {
      grpc_error_handle error =
          DropParseAndAppend(drop_overload[j], eds_update->drop_config.get());
      if (error != GRPC_ERROR_NONE) {
        errors.push_back(
            grpc_error_add_child(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                     "drop config validation error"),
                                 error));
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing EDS resource", &errors);
}

}  // namespace

absl::StatusOr<XdsResourceType::DecodeResult> XdsEndpointResourceType::Decode(
    const XdsEncodingContext& context, absl::string_view serialized_resource,
    bool is_v2) const {
  // Parse serialized proto.
  auto* resource = envoy_config_endpoint_v3_ClusterLoadAssignment_parse(
      serialized_resource.data(), serialized_resource.size(), context.arena);
  if (resource == nullptr) {
    return absl::InvalidArgumentError(
        "Can't parse ClusterLoadAssignment resource.");
  }
  MaybeLogClusterLoadAssignment(context, resource);
  // Validate resource.
  DecodeResult result;
  result.name = UpbStringToStdString(
      envoy_config_endpoint_v3_ClusterLoadAssignment_cluster_name(resource));
  auto endpoint_data = absl::make_unique<ResourceDataSubclass>();
  grpc_error_handle error =
      EdsResourceParse(context, resource, is_v2, &endpoint_data->resource);
  if (error != GRPC_ERROR_NONE) {
    std::string error_str = grpc_error_std_string(error);
    GRPC_ERROR_UNREF(error);
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid ClusterLoadAssignment %s: %s",
              context.client, result.name.c_str(), error_str.c_str());
    }
    result.resource = absl::InvalidArgumentError(error_str);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed ClusterLoadAssignment %s: %s",
              context.client, result.name.c_str(),
              endpoint_data->resource.ToString().c_str());
    }
    result.resource = std::move(endpoint_data);
  }
  return std::move(result);
}

}  // namespace grpc_core
