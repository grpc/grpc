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

#include "src/core/xds/grpc/xds_endpoint_parser.h"

#include <grpc/support/port_platform.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upb.h"
#include "envoy/config/endpoint/v3/endpoint.upbdefs.h"
#include "envoy/config/endpoint/v3/endpoint_components.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/load_balancing/ring_hash/ring_hash.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/env.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/string.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_cluster_parser.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_health_status.h"
#include "src/core/xds/grpc/xds_metadata_parser.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "upb/text/encode.h"

// IWYU pragma: no_include "absl/meta/type_traits.h"

namespace grpc_core {

namespace {

// TODO(roth): Remove this after 1.67 is released.
bool XdsDualstackEndpointsEnabled() {
  auto value = GetEnv("GRPC_EXPERIMENTAL_XDS_DUALSTACK_ENDPOINTS");
  if (!value.has_value()) return true;
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value->c_str(), &parsed_value);
  return parse_succeeded && parsed_value;
}

// TODO(roth): Flip the default to false once this proves stable, then
// remove it entirely at some point in the future.
bool XdsEndpointHashKeyBackwardCompatEnabled() {
  auto value = GetEnv("GRPC_XDS_ENDPOINT_HASH_KEY_BACKWARD_COMPAT");
  if (!value.has_value()) return true;
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value->c_str(), &parsed_value);
  return parse_succeeded && parsed_value;
}

void MaybeLogClusterLoadAssignment(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_endpoint_v3_ClusterLoadAssignment* cla) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_client) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_config_endpoint_v3_ClusterLoadAssignment_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(cla), msg_type, nullptr,
                   0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] ClusterLoadAssignment: " << buf;
  }
}

std::string GetProxyAddressFromMetadata(const XdsMetadataMap& metadata_map) {
  auto* proxy_address_entry = metadata_map.FindType<XdsAddressMetadataValue>(
      "envoy.http11_proxy_transport_socket.proxy_address");
  if (proxy_address_entry == nullptr) return "";
  return proxy_address_entry->address();
}

std::string GetHashKeyFromMetadata(const XdsMetadataMap& metadata_map) {
  auto* hash_key_entry =
      metadata_map.FindType<XdsStructMetadataValue>("envoy.lb");
  if (hash_key_entry == nullptr) return "";
  ValidationErrors unused_errors;
  return LoadJsonObjectField<std::string>(hash_key_entry->json().object(),
                                          JsonArgs(), "hash_key",
                                          &unused_errors)
      .value_or("");
}

std::optional<EndpointAddresses> EndpointAddressesParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_endpoint_v3_LbEndpoint* lb_endpoint,
    absl::string_view locality_proxy_address, ValidationErrors* errors) {
  // health_status
  const int32_t health_status =
      envoy_config_endpoint_v3_LbEndpoint_health_status(lb_endpoint);
  auto status = XdsHealthStatus::FromUpb(health_status);
  if (!status.has_value()) return std::nullopt;
  // load_balancing_weight
  uint32_t weight;
  {
    ValidationErrors::ScopedField field(errors, ".load_balancing_weight");
    weight = ParseUInt32Value(
                 envoy_config_endpoint_v3_LbEndpoint_load_balancing_weight(
                     lb_endpoint))
                 .value_or(1);
    if (weight == 0) {
      errors->AddError("must be greater than 0");
    }
  }
  // metadata
  std::string proxy_address;
  std::string hash_key;
  if (XdsHttpConnectEnabled() || !XdsEndpointHashKeyBackwardCompatEnabled()) {
    XdsMetadataMap metadata_map = ParseXdsMetadataMap(
        context, envoy_config_endpoint_v3_LbEndpoint_metadata(lb_endpoint),
        errors);
    if (XdsHttpConnectEnabled()) {
      proxy_address = GetProxyAddressFromMetadata(metadata_map);
    }
    if (!XdsEndpointHashKeyBackwardCompatEnabled()) {
      hash_key = GetHashKeyFromMetadata(metadata_map);
    }
  }
  // endpoint
  std::vector<grpc_resolved_address> addresses;
  absl::string_view hostname;
  {
    ValidationErrors::ScopedField field(errors, ".endpoint");
    const envoy_config_endpoint_v3_Endpoint* endpoint =
        envoy_config_endpoint_v3_LbEndpoint_endpoint(lb_endpoint);
    if (endpoint == nullptr) {
      errors->AddError("field not present");
      return std::nullopt;
    }
    {
      ValidationErrors::ScopedField field(errors, ".address");
      auto address = ParseXdsAddress(
          envoy_config_endpoint_v3_Endpoint_address(endpoint), errors);
      if (address.has_value()) addresses.push_back(*address);
    }
    if (XdsDualstackEndpointsEnabled()) {
      size_t size;
      auto* additional_addresses =
          envoy_config_endpoint_v3_Endpoint_additional_addresses(endpoint,
                                                                 &size);
      for (size_t i = 0; i < size; ++i) {
        ValidationErrors::ScopedField field(
            errors, absl::StrCat(".additional_addresses[", i, "].address"));
        auto address = ParseXdsAddress(
            envoy_config_endpoint_v3_Endpoint_AdditionalAddress_address(
                additional_addresses[i]),
            errors);
        if (address.has_value()) addresses.push_back(*address);
      }
    }
    hostname =
        UpbStringToAbsl(envoy_config_endpoint_v3_Endpoint_hostname(endpoint));
  }
  if (addresses.empty()) return std::nullopt;
  // Convert to EndpointAddresses.
  auto args = ChannelArgs()
                  .Set(GRPC_ARG_ADDRESS_WEIGHT, weight)
                  .Set(GRPC_ARG_XDS_HEALTH_STATUS, status->status());
  if (!hostname.empty()) {
    args = args.Set(GRPC_ARG_ADDRESS_NAME, hostname);
  }
  if (!proxy_address.empty()) {
    args = args.Set(GRPC_ARG_XDS_HTTP_PROXY, proxy_address);
  } else if (!locality_proxy_address.empty()) {
    args = args.Set(GRPC_ARG_XDS_HTTP_PROXY, locality_proxy_address);
  }
  if (!hash_key.empty()) {
    args = args.Set(GRPC_ARG_RING_HASH_ENDPOINT_HASH_KEY, hash_key);
  }
  return EndpointAddresses(addresses, args);
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

std::optional<ParsedLocality> LocalityParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_endpoint_v3_LocalityLbEndpoints* locality_lb_endpoints,
    ResolvedAddressSet* address_set, ValidationErrors* errors) {
  const size_t original_error_size = errors->size();
  ParsedLocality parsed_locality;
  // load_balancing_weight
  // If LB weight is not specified or 0, it means this locality is assigned
  // no load.
  parsed_locality.locality.lb_weight =
      ParseUInt32Value(
          envoy_config_endpoint_v3_LocalityLbEndpoints_load_balancing_weight(
              locality_lb_endpoints))
          .value_or(0);
  if (parsed_locality.locality.lb_weight == 0) return std::nullopt;
  // locality
  const envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_LocalityLbEndpoints_locality(
          locality_lb_endpoints);
  if (locality == nullptr) {
    ValidationErrors::ScopedField field(errors, ".locality");
    errors->AddError("field not present");
    return std::nullopt;
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
  // metadata
  std::string proxy_address;
  if (XdsHttpConnectEnabled()) {
    XdsMetadataMap metadata_map = ParseXdsMetadataMap(
        context,
        envoy_config_endpoint_v3_LocalityLbEndpoints_metadata(
            locality_lb_endpoints),
        errors);
    proxy_address = GetProxyAddressFromMetadata(metadata_map);
  }
  // lb_endpoints
  size_t size;
  const envoy_config_endpoint_v3_LbEndpoint* const* lb_endpoints =
      envoy_config_endpoint_v3_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".lb_endpoints[", i, "]"));
    auto endpoint =
        EndpointAddressesParse(context, lb_endpoints[i], proxy_address, errors);
    if (endpoint.has_value()) {
      for (const auto& address : endpoint->addresses()) {
        bool inserted = address_set->insert(address).second;
        if (!inserted) {
          errors->AddError(absl::StrCat(
              "duplicate endpoint address \"",
              grpc_sockaddr_to_uri(&address).value_or("<unknown>"), "\""));
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
  if (original_error_size != errors->size()) return std::nullopt;
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
    const XdsResourceType::DecodeContext& context,
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
      auto parsed_locality =
          LocalityParse(context, endpoints[i], &address_set, &errors);
      if (parsed_locality.has_value()) {
        CHECK_NE(parsed_locality->locality.lb_weight, 0u);
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
              parsed_locality->locality.name->human_readable_string()
                  .as_string_view(),
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
        for (const auto& [_, locality] : priority.localities) {
          total_weight += locality.lb_weight;
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
  const auto* policy = envoy_config_endpoint_v3_ClusterLoadAssignment_policy(
      cluster_load_assignment);
  if (policy != nullptr) {
    ValidationErrors::ScopedField field(&errors, "policy");
    size_t drop_size;
    const auto* const* drop_overload =
        envoy_config_endpoint_v3_ClusterLoadAssignment_Policy_drop_overloads(
            policy, &drop_size);
    if (drop_size > 0) {
      eds_resource->drop_config =
          MakeRefCounted<XdsEndpointResource::DropConfig>();
    }
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
    if (GRPC_TRACE_FLAG_ENABLED(xds_client)) {
      LOG(ERROR) << "[xds_client " << context.client
                 << "] invalid ClusterLoadAssignment " << *result.name << ": "
                 << eds_resource.status();
    }
    result.resource = eds_resource.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(xds_client)) {
      LOG(INFO) << "[xds_client " << context.client
                << "] parsed ClusterLoadAssignment " << *result.name << ": "
                << (*eds_resource)->ToString();
    }
    result.resource = std::move(*eds_resource);
  }
  return result;
}

}  // namespace grpc_core
