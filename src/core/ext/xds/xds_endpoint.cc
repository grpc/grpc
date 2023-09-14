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
#include <string.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
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
#include "upb/text/encode.h"

#include <grpc/support/log.h>

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_core {

//
// XdsEndpointResource
//

std::string XdsEndpointResource::Priority::Locality::ToString() const {
  std::vector<std::string> endpoint_strings;
  for (const EndpointAddresses& endpoint : endpoints) {
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
  locality_strings.reserve(localities.size());
  for (const auto& p : localities) {
    locality_strings.emplace_back(p.second.ToString());
  }
  return absl::StrCat("[", absl::StrJoin(locality_strings, ", "), "]");
}

bool XdsEndpointResource::DropConfig::ShouldDrop(
    const std::string** category_name) {
  for (size_t i = 0; i < drop_category_list_.size(); ++i) {
    const auto& drop_category = drop_category_list_[i];
    // Generate a random number in [0, 1000000).
    const uint32_t random = [&]() {
      MutexLock lock(&mu_);
      return absl::Uniform<uint32_t>(bit_gen_, 0, 1000000);
    }();
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

absl::optional<EndpointAddresses> EndpointAddressesParse(
    const envoy_config_endpoint_v3_LbEndpoint* lb_endpoint,
    ValidationErrors* errors) {
  // health_status
  const int32_t health_status =
      envoy_config_endpoint_v3_LbEndpoint_health_status(lb_endpoint);
  if (!XdsOverrideHostEnabled() &&
      health_status != envoy_config_core_v3_UNKNOWN &&
      health_status != envoy_config_core_v3_HEALTHY) {
    return absl::nullopt;
  }
  auto status = XdsHealthStatus::FromUpb(health_status);
  if (!status.has_value()) return absl::nullopt;
  // load_balancing_weight
  uint32_t weight = 1;
  {
    ValidationErrors::ScopedField field(errors, ".load_balancing_weight");
    const google_protobuf_UInt32Value* load_balancing_weight =
        envoy_config_endpoint_v3_LbEndpoint_load_balancing_weight(lb_endpoint);
    if (load_balancing_weight != nullptr) {
      weight = google_protobuf_UInt32Value_value(load_balancing_weight);
      if (weight == 0) {
        errors->AddError("must be greater than 0");
      }
    }
  }
  // endpoint
  // TODO(roth): add support for multiple addresses per endpoint
  grpc_resolved_address grpc_address;
  {
    ValidationErrors::ScopedField field(errors, ".endpoint");
    const envoy_config_endpoint_v3_Endpoint* endpoint =
        envoy_config_endpoint_v3_LbEndpoint_endpoint(lb_endpoint);
    if (endpoint == nullptr) {
      errors->AddError("field not present");
      return absl::nullopt;
    }
    ValidationErrors::ScopedField field2(errors, ".address");
    const envoy_config_core_v3_Address* address =
        envoy_config_endpoint_v3_Endpoint_address(endpoint);
    if (address == nullptr) {
      errors->AddError("field not present");
      return absl::nullopt;
    }
    ValidationErrors::ScopedField field3(errors, ".socket_address");
    const envoy_config_core_v3_SocketAddress* socket_address =
        envoy_config_core_v3_Address_socket_address(address);
    if (socket_address == nullptr) {
      errors->AddError("field not present");
      return absl::nullopt;
    }
    std::string address_str = UpbStringToStdString(
        envoy_config_core_v3_SocketAddress_address(socket_address));
    uint32_t port;
    {
      ValidationErrors::ScopedField field(errors, ".port_value");
      port = envoy_config_core_v3_SocketAddress_port_value(socket_address);
      if (GPR_UNLIKELY(port >> 16) != 0) {
        errors->AddError("invalid port");
        return absl::nullopt;
      }
    }
    auto addr = StringToSockaddr(address_str, port);
    if (!addr.ok()) {
      errors->AddError(addr.status().message());
    } else {
      grpc_address = *addr;
    }
  }
  // Convert to EndpointAddresses.
  return EndpointAddresses(
      grpc_address,
      ChannelArgs()
          .Set(GRPC_ARG_ADDRESS_WEIGHT, weight)
          .Set(GRPC_ARG_XDS_HEALTH_STATUS, status->status()));
}

struct ParsedLocality {
  size_t priority;
  XdsEndpointResource::Priority::Locality locality;
};

struct ResolvedAddressLessThan {
  bool operator()(const grpc_resolved_address& a1,
                  const grpc_resolved_address& a2) const {
    if (a1.len != a2.len) return a1.len < a2.len;
    return memcmp(a1.addr, a2.addr, a1.len) < 0;
  }
};
using ResolvedAddressSet =
    std::set<grpc_resolved_address, ResolvedAddressLessThan>;

absl::optional<ParsedLocality> LocalityParse(
    const envoy_config_endpoint_v3_LocalityLbEndpoints* locality_lb_endpoints,
    ResolvedAddressSet* address_set, ValidationErrors* errors) {
  const size_t original_error_size = errors->size();
  ParsedLocality parsed_locality;
  // load_balancing_weight
  // If LB weight is not specified or 0, it means this locality is assigned
  // no load.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  parsed_locality.locality.lb_weight =
      lb_weight != nullptr ? google_protobuf_UInt32Value_value(lb_weight) : 0;
  if (parsed_locality.locality.lb_weight == 0) return absl::nullopt;
  // locality
  const envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_LocalityLbEndpoints_locality(
          locality_lb_endpoints);
  if (locality == nullptr) {
    ValidationErrors::ScopedField field(errors, ".locality");
    errors->AddError("field not present");
    return absl::nullopt;
  }
  // region
  std::string region =
      UpbStringToStdString(envoy_config_core_v3_Locality_region(locality));
  // zone
  std::string zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_zone(locality));
  // sub_zone
  std::string sub_zone =
      UpbStringToStdString(envoy_config_core_v3_Locality_sub_zone(locality));
  parsed_locality.locality.name = MakeRefCounted<XdsLocalityName>(
      std::move(region), std::move(zone), std::move(sub_zone));
  // lb_endpoints
  size_t size;
  const envoy_config_endpoint_v3_LbEndpoint* const* lb_endpoints =
      envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".lb_endpoints[", i, "]"));
    auto endpoint = EndpointAddressesParse(lb_endpoints[i], errors);
    if (endpoint.has_value()) {
      for (const auto& address : endpoint->addresses()) {
        bool inserted = address_set->insert(address).second;
        if (!inserted) {
          errors->AddError(absl::StrCat(
              "duplicate endpoint address \"",
              grpc_sockaddr_to_uri(&address).value_or("<unknown>"),
              "\""));
        }
      }
      parsed_locality.locality.endpoints.push_back(std::move(*endpoint));
    }
  }
  // priority
  parsed_locality.priority =
      envoy_config_endpoint_v3_LocalityLbEndpoints_priority(
          locality_lb_endpoints);
  // Return result.
  if (original_error_size != errors->size()) return absl::nullopt;
  return parsed_locality;
}

void DropParseAndAppend(
    const envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload*
        drop_overload,
    XdsEndpointResource::DropConfig* drop_config, ValidationErrors* errors) {
  // category
  std::string category = UpbStringToStdString(
      envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_category(
          drop_overload));
  if (category.empty()) {
    ValidationErrors::ScopedField field(errors, ".category");
    errors->AddError("empty drop category name");
  }
  // drop_percentage
  uint32_t numerator;
  {
    ValidationErrors::ScopedField field(errors, ".drop_percentage");
    const envoy_type_v3_FractionalPercent* drop_percentage =
        envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_DropOverload_drop_percentage(
            drop_overload);
    if (drop_percentage == nullptr) {
      errors->AddError("field not present");
      return;
    }
    numerator = envoy_type_v3_FractionalPercent_numerator(drop_percentage);
    {
      ValidationErrors::ScopedField field(errors, ".denominator");
      const int denominator =
          envoy_type_v3_FractionalPercent_denominator(drop_percentage);
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
          errors->AddError("unknown denominator type");
      }
    }
    // Cap numerator to 1000000.
    numerator = std::min(numerator, 1000000u);
  }
  // Add category.
  drop_config->AddCategory(std::move(category), numerator);
}

absl::StatusOr<std::shared_ptr<const XdsEndpointResource>> EdsResourceParse(
    const XdsResourceType::DecodeContext& /*context*/,
    const envoy_config_endpoint_v3_ClusterLoadAssignment*
        cluster_load_assignment) {
  ValidationErrors errors;
  auto eds_resource = std::make_shared<XdsEndpointResource>();
  // endpoints
  {
    ValidationErrors::ScopedField field(&errors, "endpoints");
    ResolvedAddressSet address_set;
    size_t locality_size;
    const envoy_config_endpoint_v3_LocalityLbEndpoints* const* endpoints =
        envoy_config_endpoint_v3_ClusterLoadAssignment_endpoints(
            cluster_load_assignment, &locality_size);
    for (size_t i = 0; i < locality_size; ++i) {
      ValidationErrors::ScopedField field(&errors, absl::StrCat("[", i, "]"));
      auto parsed_locality = LocalityParse(endpoints[i], &address_set, &errors);
      if (parsed_locality.has_value()) {
        GPR_ASSERT(parsed_locality->locality.lb_weight != 0);
        // Make sure prorities is big enough. Note that they might not
        // arrive in priority order.
        if (eds_resource->priorities.size() < parsed_locality->priority + 1) {
          eds_resource->priorities.resize(parsed_locality->priority + 1);
        }
        auto& locality_map =
            eds_resource->priorities[parsed_locality->priority].localities;
        auto it = locality_map.find(parsed_locality->locality.name.get());
        if (it != locality_map.end()) {
          errors.AddError(absl::StrCat(
              "duplicate locality ",
              parsed_locality->locality.name->AsHumanReadableString(),
              " found in priority ", parsed_locality->priority));
        } else {
          locality_map.emplace(parsed_locality->locality.name.get(),
                               std::move(parsed_locality->locality));
        }
      }
    }
    for (size_t i = 0; i < eds_resource->priorities.size(); ++i) {
      const auto& priority = eds_resource->priorities[i];
      if (priority.localities.empty()) {
        errors.AddError(absl::StrCat("priority ", i, " empty"));
      } else {
        // Check that the sum of the locality weights in this priority
        // does not exceed the max value for a uint32.
        uint64_t total_weight = 0;
        for (const auto& p : priority.localities) {
          total_weight += p.second.lb_weight;
          if (total_weight > std::numeric_limits<uint32_t>::max()) {
            errors.AddError(
                absl::StrCat("sum of locality weights for priority ", i,
                             " exceeds uint32 max"));
            break;
          }
        }
      }
    }
  }
  // policy
  eds_resource->drop_config = MakeRefCounted<XdsEndpointResource::DropConfig>();
  const auto* policy = envoy_config_endpoint_v3_ClusterLoadAssignment_policy(
      cluster_load_assignment);
  if (policy != nullptr) {
    ValidationErrors::ScopedField field(&errors, "policy");
    size_t drop_size;
    const auto* const* drop_overload =
        envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_drop_overloads(
            policy, &drop_size);
    for (size_t i = 0; i < drop_size; ++i) {
      ValidationErrors::ScopedField field(
          &errors, absl::StrCat(".drop_overloads[", i, "]"));
      DropParseAndAppend(drop_overload[i], eds_resource->drop_config.get(),
                         &errors);
    }
  }
  // Return result.
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "errors parsing EDS resource");
  }
  return eds_resource;
}

}  // namespace

XdsResourceType::DecodeResult XdsEndpointResourceType::Decode(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_resource) const {
  DecodeResult result;
  // Parse serialized proto.
  auto* resource = envoy_config_endpoint_v3_ClusterLoadAssignment_parse(
      serialized_resource.data(), serialized_resource.size(), context.arena);
  if (resource == nullptr) {
    result.resource = absl::InvalidArgumentError(
        "Can't parse ClusterLoadAssignment resource.");
    return result;
  }
  MaybeLogClusterLoadAssignment(context, resource);
  // Validate resource.
  result.name = UpbStringToStdString(
      envoy_config_endpoint_v3_ClusterLoadAssignment_cluster_name(resource));
  auto eds_resource = EdsResourceParse(context, resource);
  if (!eds_resource.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_ERROR, "[xds_client %p] invalid ClusterLoadAssignment %s: %s",
              context.client, result.name->c_str(),
              eds_resource.status().ToString().c_str());
    }
    result.resource = eds_resource.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(*context.tracer)) {
      gpr_log(GPR_INFO, "[xds_client %p] parsed ClusterLoadAssignment %s: %s",
              context.client, result.name->c_str(),
              (*eds_resource)->ToString().c_str());
    }
    result.resource = std::move(*eds_resource);
  }
  return result;
}

}  // namespace grpc_core
