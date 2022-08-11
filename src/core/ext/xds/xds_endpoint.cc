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

#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/core/v3/health_check.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upbdefs.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/text_encode.h"

#include <grpc/support/log.h>

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"

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
    const XdsResourceType::DecodeContext& context,
    const envoy_config_endpoint_v3_ClusterLoadAssignment* cla) {
  if (GRPC_TRACE_FLAG_ENABLED(*context.tracer) &&
      gpr_should_log(GPR_LOG_SEVERITY_DEBUG)) {
    const upb_MessageDef* msg_type =
        envoy_config_endpoint_v3_ClusterLoadAssignment_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_TextEncode(cla, msg_type, nullptr, 0, buf, sizeof(buf));
    gpr_log(GPR_DEBUG, "[xds_client %p] ClusterLoadAssignment: %s",
            context.client, buf);
  }
}

absl::StatusOr<absl::optional<ServerAddress>> ServerAddressParse(
    const envoy_config_endpoint_v3_LbEndpoint* lb_endpoint) {
  // If health_status is not HEALTHY or UNKNOWN, skip this endpoint.
  const int32_t health_status =
      envoy_config_endpoint_v3_LbEndpoint_health_status(lb_endpoint);
  if (health_status != envoy_config_core_v3_UNKNOWN &&
      health_status != envoy_config_core_v3_HEALTHY) {
    return absl::nullopt;
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
    return absl::InvalidArgumentError("Invalid port.");
  }
  // Find load_balancing_weight for the endpoint.
  uint32_t weight = 1;
  const google_protobuf_UInt32Value* load_balancing_weight =
      envoy_config_endpoint_v3_LbEndpoint_load_balancing_weight(lb_endpoint);
  if (load_balancing_weight != nullptr) {
    weight = google_protobuf_UInt32Value_value(load_balancing_weight);
    if (weight == 0) {
      return absl::InvalidArgumentError("Invalid endpoint weight of 0.");
    }
  }
  // Populate grpc_resolved_address.
  auto addr = StringToSockaddr(address_str, port);
  if (!addr.ok()) return addr.status();
  // Append the address to the list.
  std::map<const char*, std::unique_ptr<ServerAddress::AttributeInterface>>
      attributes;
  attributes[ServerAddressWeightAttribute::kServerAddressWeightAttributeKey] =
      absl::make_unique<ServerAddressWeightAttribute>(weight);
  return ServerAddress(*addr, ChannelArgs(), std::move(attributes));
}

struct ParsedLocality {
  size_t priority;
  XdsEndpointResource::Priority::Locality locality;
};

absl::StatusOr<ParsedLocality> LocalityParse(
    const envoy_config_endpoint_v3_LocalityLbEndpoints* locality_lb_endpoints) {
  ParsedLocality parsed_locality;
  // Parse LB weight.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  // If LB weight is not specified, it means this locality is assigned no load.
  parsed_locality.locality.lb_weight =
      lb_weight != nullptr ? google_protobuf_UInt32Value_value(lb_weight) : 0;
  if (parsed_locality.locality.lb_weight == 0) return parsed_locality;
  // Parse locality name.
  const envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_LocalityLbEndpoints_locality(
          locality_lb_endpoints);
  if (locality == nullptr) {
    return absl::InvalidArgumentError("Empty locality.");
  }
  std::string region =
      UpbStringToStdString(envoy_config_core_v3_Locality_region(locality));
  std::string zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_zone(locality));
  std::string sub_zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_sub_zone(locality));
  parsed_locality.locality.name = MakeRefCounted<XdsLocalityName>(
      std::move(region), std::move(zone), std::move(sub_zone));
  // Parse the addresses.
  size_t size;
  const envoy_config_endpoint_v3_LbEndpoint* const* lb_endpoints =
      envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    auto address = ServerAddressParse(lb_endpoints[i]);
    if (!address.ok()) return address.status();
    if (address->has_value()) {
      parsed_locality.locality.endpoints.push_back(std::move(**address));
    }
  }
  // Parse the priority.
  parsed_locality.priority =
      envoy_config_endpoint_v3_LocalityLbEndpoints_priority(
          locality_lb_endpoints);
  return parsed_locality;
}

absl::Status DropParseAndAppend(
    const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload*
        drop_overload,
    XdsEndpointResource::DropConfig* drop_config) {
  // Get the category.
  std::string category = UpbStringToStdString(
      envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_category(
          drop_overload));
  if (category.empty()) {
    return absl::InvalidArgumentError("Empty drop category name");
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
      return absl::InvalidArgumentError(
          "drop config: unknown denominator type");
  }
  // Cap numerator to 1000000.
  numerator = std::min(numerator, 1000000u);
  drop_config->AddCategory(std::move(category), numerator);
  return absl::OkStatus();
}

absl::StatusOr<XdsEndpointResource> EdsResourceParse(
    const XdsResourceType::DecodeContext& /*context*/,
    const envoy_config_endpoint_v3_ClusterLoadAssignment*
        cluster_load_assignment,
    bool /*is_v2*/) {
  XdsEndpointResource eds_resource;
  std::vector<std::string> errors;
  // Get the endpoints.
  size_t locality_size;
  const envoy_config_endpoint_v3_LocalityLbEndpoints* const* endpoints =
      envoy_config_endpoint_v3_ClusterLoadAssignment_endpoints(
          cluster_load_assignment, &locality_size);
  for (size_t j = 0; j < locality_size; ++j) {
    auto parsed_locality = LocalityParse(endpoints[j]);
    if (!parsed_locality.ok()) {
      errors.emplace_back(parsed_locality.status().message());
      continue;
    }
    // Filter out locality with weight 0.
    if (parsed_locality->locality.lb_weight == 0) continue;
    // Make sure prorities is big enough. Note that they might not
    // arrive in priority order.
    if (eds_resource.priorities.size() < parsed_locality->priority + 1) {
      eds_resource.priorities.resize(parsed_locality->priority + 1);
    }
    auto& locality_map =
        eds_resource.priorities[parsed_locality->priority].localities;
    auto it = locality_map.find(parsed_locality->locality.name.get());
    if (it != locality_map.end()) {
      errors.emplace_back(
          absl::StrCat("duplicate locality ",
                       parsed_locality->locality.name->AsHumanReadableString(),
                       " found in priority ", parsed_locality->priority));
    } else {
      locality_map.emplace(parsed_locality->locality.name.get(),
                           std::move(parsed_locality->locality));
    }
  }
  for (const auto& priority : eds_resource.priorities) {
    if (priority.localities.empty()) {
      errors.emplace_back("sparse priority list");
    }
  }
  // Get the drop config.
  eds_resource.drop_config = MakeRefCounted<XdsEndpointResource::DropConfig>();
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
      absl::Status status =
          DropParseAndAppend(drop_overload[j], eds_resource.drop_config.get());
      if (!status.ok()) errors.emplace_back(status.message());
    }
  }
  // Return result.
  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "errors parsing EDS resource: [", absl::StrJoin(errors, "; "), "]"));
  }
  return eds_resource;
}

}  // namespace

absl::StatusOr<XdsResourceType::DecodeResult> XdsEndpointResourceType::Decode(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_resource, bool is_v2) const {
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
  auto eds_resource = EdsResourceParse(context, resource, is_v2);
  if (!eds_resource.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid ClusterLoadAssignment %s: %s",
              context.client, result.name.c_str(),
              eds_resource.status().ToString().c_str());
    }
    result.resource = eds_resource.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed ClusterLoadAssignment %s: %s",
              context.client, result.name.c_str(),
              eds_resource->ToString().c_str());
    }
    auto resource = absl::make_unique<ResourceDataSubclass>();
    resource->resource = std::move(*eds_resource);
    result.resource = std::move(resource);
  }
  return std::move(result);
}

}  // namespace grpc_core
