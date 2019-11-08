/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <algorithm>

#include <grpc/impl/codegen/log.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

#include "envoy/api/v2/core/address.upb.h"
#include "envoy/api/v2/core/base.upb.h"
#include "envoy/api/v2/core/health_check.upb.h"
#include "envoy/api/v2/discovery.upb.h"
#include "envoy/api/v2/eds.upb.h"
#include "envoy/api/v2/endpoint/endpoint.upb.h"
#include "envoy/api/v2/endpoint/load_report.upb.h"
#include "envoy/service/load_stats/v2/lrs.upb.h"
#include "envoy/type/percent.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/upb.h"

namespace grpc_core {

namespace {

constexpr char kEdsTypeUrl[] =
    "type.googleapis.com/envoy.api.v2.ClusterLoadAssignment";

}  // namespace

bool XdsPriorityListUpdate::operator==(
    const XdsPriorityListUpdate& other) const {
  if (priorities_.size() != other.priorities_.size()) return false;
  for (size_t i = 0; i < priorities_.size(); ++i) {
    if (priorities_[i].localities != other.priorities_[i].localities) {
      return false;
    }
  }
  return true;
}

void XdsPriorityListUpdate::Add(
    XdsPriorityListUpdate::LocalityMap::Locality locality) {
  // Pad the missing priorities in case the localities are not ordered by
  // priority.
  if (!Contains(locality.priority)) priorities_.resize(locality.priority + 1);
  LocalityMap& locality_map = priorities_[locality.priority];
  locality_map.localities.emplace(locality.name, std::move(locality));
}

const XdsPriorityListUpdate::LocalityMap* XdsPriorityListUpdate::Find(
    uint32_t priority) const {
  if (!Contains(priority)) return nullptr;
  return &priorities_[priority];
}

bool XdsPriorityListUpdate::Contains(
    const RefCountedPtr<XdsLocalityName>& name) {
  for (size_t i = 0; i < priorities_.size(); ++i) {
    const LocalityMap& locality_map = priorities_[i];
    if (locality_map.Contains(name)) return true;
  }
  return false;
}

bool XdsDropConfig::ShouldDrop(
    const grpc_core::UniquePtr<char>** category_name) const {
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

namespace {

void PopulateMetadataValue(upb_arena* arena, google_protobuf_Value* value_pb,
                           const XdsBootstrap::MetadataValue& value);

void PopulateListValue(upb_arena* arena, google_protobuf_ListValue* list_value,
                       const std::vector<XdsBootstrap::MetadataValue>& values) {
  for (const auto& value : values) {
    auto* value_pb = google_protobuf_ListValue_add_values(list_value, arena);
    PopulateMetadataValue(arena, value_pb, value);
  }
}

void PopulateMetadata(upb_arena* arena, google_protobuf_Struct* metadata_pb,
                      const std::map<const char*, XdsBootstrap::MetadataValue,
                                     StringLess>& metadata) {
  for (const auto& p : metadata) {
    google_protobuf_Struct_FieldsEntry* field =
        google_protobuf_Struct_add_fields(metadata_pb, arena);
    google_protobuf_Struct_FieldsEntry_set_key(field,
                                               upb_strview_makez(p.first));
    google_protobuf_Value* value =
        google_protobuf_Struct_FieldsEntry_mutable_value(field, arena);
    PopulateMetadataValue(arena, value, p.second);
  }
}

void PopulateMetadataValue(upb_arena* arena, google_protobuf_Value* value_pb,
                           const XdsBootstrap::MetadataValue& value) {
  switch (value.type) {
    case XdsBootstrap::MetadataValue::Type::MD_NULL:
      google_protobuf_Value_set_null_value(value_pb, 0);
      break;
    case XdsBootstrap::MetadataValue::Type::DOUBLE:
      google_protobuf_Value_set_number_value(value_pb, value.double_value);
      break;
    case XdsBootstrap::MetadataValue::Type::STRING:
      google_protobuf_Value_set_string_value(
          value_pb, upb_strview_makez(value.string_value));
      break;
    case XdsBootstrap::MetadataValue::Type::BOOL:
      google_protobuf_Value_set_bool_value(value_pb, value.bool_value);
      break;
    case XdsBootstrap::MetadataValue::Type::STRUCT: {
      google_protobuf_Struct* struct_value =
          google_protobuf_Value_mutable_struct_value(value_pb, arena);
      PopulateMetadata(arena, struct_value, value.struct_value);
      break;
    }
    case XdsBootstrap::MetadataValue::Type::LIST: {
      google_protobuf_ListValue* list_value =
          google_protobuf_Value_mutable_list_value(value_pb, arena);
      PopulateListValue(arena, list_value, value.list_value);
      break;
    }
  }
}

void PopulateNode(upb_arena* arena, const XdsBootstrap::Node* node,
                  const char* build_version, envoy_api_v2_core_Node* node_msg) {
  if (node != nullptr) {
    if (node->id != nullptr) {
      envoy_api_v2_core_Node_set_id(node_msg, upb_strview_makez(node->id));
    }
    if (node->cluster != nullptr) {
      envoy_api_v2_core_Node_set_cluster(node_msg,
                                         upb_strview_makez(node->cluster));
    }
    if (!node->metadata.empty()) {
      google_protobuf_Struct* metadata =
          envoy_api_v2_core_Node_mutable_metadata(node_msg, arena);
      PopulateMetadata(arena, metadata, node->metadata);
    }
    if (node->locality_region != nullptr || node->locality_zone != nullptr ||
        node->locality_subzone != nullptr) {
      envoy_api_v2_core_Locality* locality =
          envoy_api_v2_core_Node_mutable_locality(node_msg, arena);
      if (node->locality_region != nullptr) {
        envoy_api_v2_core_Locality_set_region(
            locality, upb_strview_makez(node->locality_region));
      }
      if (node->locality_zone != nullptr) {
        envoy_api_v2_core_Locality_set_zone(
            locality, upb_strview_makez(node->locality_zone));
      }
      if (node->locality_subzone != nullptr) {
        envoy_api_v2_core_Locality_set_sub_zone(
            locality, upb_strview_makez(node->locality_subzone));
      }
    }
  }
  envoy_api_v2_core_Node_set_build_version(node_msg,
                                           upb_strview_makez(build_version));
}

}  // namespace

grpc_slice XdsEdsRequestCreateAndEncode(const char* server_name,
                                        const XdsBootstrap::Node* node,
                                        const char* build_version) {
  upb::Arena arena;
  // Create a request.
  envoy_api_v2_DiscoveryRequest* request =
      envoy_api_v2_DiscoveryRequest_new(arena.ptr());
  envoy_api_v2_core_Node* node_msg =
      envoy_api_v2_DiscoveryRequest_mutable_node(request, arena.ptr());
  PopulateNode(arena.ptr(), node, build_version, node_msg);
  envoy_api_v2_DiscoveryRequest_add_resource_names(
      request, upb_strview_makez(server_name), arena.ptr());
  envoy_api_v2_DiscoveryRequest_set_type_url(request,
                                             upb_strview_makez(kEdsTypeUrl));
  // Encode the request.
  size_t output_length;
  char* output = envoy_api_v2_DiscoveryRequest_serialize(request, arena.ptr(),
                                                         &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

namespace {

grpc_error* ServerAddressParseAndAppend(
    const envoy_api_v2_endpoint_LbEndpoint* lb_endpoint,
    ServerAddressList* list) {
  // If health_status is not HEALTHY or UNKNOWN, skip this endpoint.
  const int32_t health_status =
      envoy_api_v2_endpoint_LbEndpoint_health_status(lb_endpoint);
  if (health_status != envoy_api_v2_core_UNKNOWN &&
      health_status != envoy_api_v2_core_HEALTHY) {
    return GRPC_ERROR_NONE;
  }
  // Find the ip:port.
  const envoy_api_v2_endpoint_Endpoint* endpoint =
      envoy_api_v2_endpoint_LbEndpoint_endpoint(lb_endpoint);
  const envoy_api_v2_core_Address* address =
      envoy_api_v2_endpoint_Endpoint_address(endpoint);
  const envoy_api_v2_core_SocketAddress* socket_address =
      envoy_api_v2_core_Address_socket_address(address);
  upb_strview address_strview =
      envoy_api_v2_core_SocketAddress_address(socket_address);
  uint32_t port = envoy_api_v2_core_SocketAddress_port_value(socket_address);
  if (GPR_UNLIKELY(port >> 16) != 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid port.");
  }
  // Populate grpc_resolved_address.
  grpc_resolved_address addr;
  char* address_str = static_cast<char*>(gpr_malloc(address_strview.size + 1));
  memcpy(address_str, address_strview.data, address_strview.size);
  address_str[address_strview.size] = '\0';
  grpc_string_to_sockaddr(&addr, address_str, port);
  gpr_free(address_str);
  // Append the address to the list.
  list->emplace_back(addr, nullptr);
  return GRPC_ERROR_NONE;
}

namespace {

grpc_core::UniquePtr<char> StringCopy(const upb_strview& strview) {
  char* str = static_cast<char*>(gpr_malloc(strview.size + 1));
  memcpy(str, strview.data, strview.size);
  str[strview.size] = '\0';
  return grpc_core::UniquePtr<char>(str);
}

}  // namespace

grpc_error* LocalityParse(
    const envoy_api_v2_endpoint_LocalityLbEndpoints* locality_lb_endpoints,
    XdsPriorityListUpdate::LocalityMap::Locality* output_locality) {
  // Parse LB weight.
  const google_protobuf_UInt32Value* lb_weight =
      envoy_api_v2_endpoint_LocalityLbEndpoints_load_balancing_weight(
          locality_lb_endpoints);
  // If LB weight is not specified, it means this locality is assigned no load.
  // TODO(juanlishen): When we support CDS to configure the inter-locality
  // policy, we should change the LB weight handling.
  output_locality->lb_weight =
      lb_weight != nullptr ? google_protobuf_UInt32Value_value(lb_weight) : 0;
  if (output_locality->lb_weight == 0) return GRPC_ERROR_NONE;
  // Parse locality name.
  const envoy_api_v2_core_Locality* locality =
      envoy_api_v2_endpoint_LocalityLbEndpoints_locality(locality_lb_endpoints);
  output_locality->name = MakeRefCounted<XdsLocalityName>(
      StringCopy(envoy_api_v2_core_Locality_region(locality)),
      StringCopy(envoy_api_v2_core_Locality_zone(locality)),
      StringCopy(envoy_api_v2_core_Locality_sub_zone(locality)));
  // Parse the addresses.
  size_t size;
  const envoy_api_v2_endpoint_LbEndpoint* const* lb_endpoints =
      envoy_api_v2_endpoint_LocalityLbEndpoints_lb_endpoints(
          locality_lb_endpoints, &size);
  for (size_t i = 0; i < size; ++i) {
    grpc_error* error = ServerAddressParseAndAppend(
        lb_endpoints[i], &output_locality->serverlist);
    if (error != GRPC_ERROR_NONE) return error;
  }
  // Parse the priority.
  output_locality->priority =
      envoy_api_v2_endpoint_LocalityLbEndpoints_priority(locality_lb_endpoints);
  return GRPC_ERROR_NONE;
}

grpc_error* DropParseAndAppend(
    const envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload* drop_overload,
    XdsDropConfig* drop_config, bool* drop_all) {
  // Get the category.
  upb_strview category =
      envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload_category(
          drop_overload);
  if (category.size == 0) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty drop category name");
  }
  // Get the drop rate (per million).
  const envoy_type_FractionalPercent* drop_percentage =
      envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload_drop_percentage(
          drop_overload);
  uint32_t numerator = envoy_type_FractionalPercent_numerator(drop_percentage);
  const auto denominator =
      static_cast<envoy_type_FractionalPercent_DenominatorType>(
          envoy_type_FractionalPercent_denominator(drop_percentage));
  // Normalize to million.
  switch (denominator) {
    case envoy_type_FractionalPercent_HUNDRED:
      numerator *= 10000;
      break;
    case envoy_type_FractionalPercent_TEN_THOUSAND:
      numerator *= 100;
      break;
    case envoy_type_FractionalPercent_MILLION:
      break;
    default:
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unknown denominator type");
  }
  // Cap numerator to 1000000.
  numerator = GPR_MIN(numerator, 1000000);
  if (numerator == 1000000) *drop_all = true;
  drop_config->AddCategory(StringCopy(category), numerator);
  return GRPC_ERROR_NONE;
}

}  // namespace

grpc_error* XdsEdsResponseDecodeAndParse(const grpc_slice& encoded_response,
                                         EdsUpdate* update) {
  upb::Arena arena;
  // Decode the response.
  const envoy_api_v2_DiscoveryResponse* response =
      envoy_api_v2_DiscoveryResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
          GRPC_SLICE_LENGTH(encoded_response), arena.ptr());
  // Parse the response.
  if (response == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No response found.");
  }
  // Check the type_url of the response.
  upb_strview type_url = envoy_api_v2_DiscoveryResponse_type_url(response);
  upb_strview expected_type_url = upb_strview_makez(kEdsTypeUrl);
  if (!upb_strview_eql(type_url, expected_type_url)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not EDS.");
  }
  // Get the resources from the response.
  size_t size;
  const google_protobuf_Any* const* resources =
      envoy_api_v2_DiscoveryResponse_resources(response, &size);
  if (size < 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "EDS response contains 0 resource.");
  }
  // Check the type_url of the resource.
  type_url = google_protobuf_Any_type_url(resources[0]);
  if (!upb_strview_eql(type_url, expected_type_url)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resource is not EDS.");
  }
  // Get the cluster_load_assignment.
  upb_strview encoded_cluster_load_assignment =
      google_protobuf_Any_value(resources[0]);
  envoy_api_v2_ClusterLoadAssignment* cluster_load_assignment =
      envoy_api_v2_ClusterLoadAssignment_parse(
          encoded_cluster_load_assignment.data,
          encoded_cluster_load_assignment.size, arena.ptr());
  // Get the endpoints.
  const envoy_api_v2_endpoint_LocalityLbEndpoints* const* endpoints =
      envoy_api_v2_ClusterLoadAssignment_endpoints(cluster_load_assignment,
                                                   &size);
  for (size_t i = 0; i < size; ++i) {
    XdsPriorityListUpdate::LocalityMap::Locality locality;
    grpc_error* error = LocalityParse(endpoints[i], &locality);
    if (error != GRPC_ERROR_NONE) return error;
    // Filter out locality with weight 0.
    if (locality.lb_weight == 0) continue;
    update->priority_list_update.Add(locality);
  }
  // Get the drop config.
  update->drop_config = MakeRefCounted<XdsDropConfig>();
  const envoy_api_v2_ClusterLoadAssignment_Policy* policy =
      envoy_api_v2_ClusterLoadAssignment_policy(cluster_load_assignment);
  if (policy != nullptr) {
    const envoy_api_v2_ClusterLoadAssignment_Policy_DropOverload* const*
        drop_overload =
            envoy_api_v2_ClusterLoadAssignment_Policy_drop_overloads(policy,
                                                                     &size);
    for (size_t i = 0; i < size; ++i) {
      grpc_error* error = DropParseAndAppend(
          drop_overload[i], update->drop_config.get(), &update->drop_all);
      if (error != GRPC_ERROR_NONE) return error;
    }
  }
  return GRPC_ERROR_NONE;
}

namespace {

grpc_slice LrsRequestEncode(
    const envoy_service_load_stats_v2_LoadStatsRequest* request,
    upb_arena* arena) {
  size_t output_length;
  char* output = envoy_service_load_stats_v2_LoadStatsRequest_serialize(
      request, arena, &output_length);
  return grpc_slice_from_copied_buffer(output, output_length);
}

}  // namespace

grpc_slice XdsLrsRequestCreateAndEncode(const char* server_name,
                                        const XdsBootstrap::Node* node,
                                        const char* build_version) {
  upb::Arena arena;
  // Create a request.
  envoy_service_load_stats_v2_LoadStatsRequest* request =
      envoy_service_load_stats_v2_LoadStatsRequest_new(arena.ptr());
  // Populate node.
  envoy_api_v2_core_Node* node_msg =
      envoy_service_load_stats_v2_LoadStatsRequest_mutable_node(request,
                                                                arena.ptr());
  PopulateNode(arena.ptr(), node, build_version, node_msg);
  // Add cluster stats. There is only one because we only use one server name in
  // one channel.
  envoy_api_v2_endpoint_ClusterStats* cluster_stats =
      envoy_service_load_stats_v2_LoadStatsRequest_add_cluster_stats(
          request, arena.ptr());
  // Set the cluster name.
  envoy_api_v2_endpoint_ClusterStats_set_cluster_name(
      cluster_stats, upb_strview_makez(server_name));
  return LrsRequestEncode(request, arena.ptr());
}

namespace {

void LocalityStatsPopulate(
    envoy_api_v2_endpoint_UpstreamLocalityStats* output,
    std::pair<const RefCountedPtr<XdsLocalityName>,
              XdsClientStats::LocalityStats::Snapshot>& input,
    upb_arena* arena) {
  // Set sub_zone.
  envoy_api_v2_core_Locality* locality =
      envoy_api_v2_endpoint_UpstreamLocalityStats_mutable_locality(output,
                                                                   arena);
  envoy_api_v2_core_Locality_set_sub_zone(
      locality, upb_strview_makez(input.first->sub_zone()));
  // Set total counts.
  XdsClientStats::LocalityStats::Snapshot& snapshot = input.second;
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_successful_requests(
      output, snapshot.total_successful_requests);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_requests_in_progress(
      output, snapshot.total_requests_in_progress);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_error_requests(
      output, snapshot.total_error_requests);
  envoy_api_v2_endpoint_UpstreamLocalityStats_set_total_issued_requests(
      output, snapshot.total_issued_requests);
  // Add load metric stats.
  for (auto& p : snapshot.load_metric_stats) {
    const char* metric_name = p.first.get();
    const XdsClientStats::LocalityStats::LoadMetric::Snapshot& metric_value =
        p.second;
    envoy_api_v2_endpoint_EndpointLoadMetricStats* load_metric =
        envoy_api_v2_endpoint_UpstreamLocalityStats_add_load_metric_stats(
            output, arena);
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_metric_name(
        load_metric, upb_strview_makez(metric_name));
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_num_requests_finished_with_metric(
        load_metric, metric_value.num_requests_finished_with_metric);
    envoy_api_v2_endpoint_EndpointLoadMetricStats_set_total_metric_value(
        load_metric, metric_value.total_metric_value);
  }
}

}  // namespace

grpc_slice XdsLrsRequestCreateAndEncode(const char* server_name,
                                        XdsClientStats* client_stats) {
  upb::Arena arena;
  XdsClientStats::Snapshot snapshot = client_stats->GetSnapshotAndReset();
  // Prune unused locality stats.
  client_stats->PruneLocalityStats();
  // When all the counts are zero, return empty slice.
  if (snapshot.IsAllZero()) return grpc_empty_slice();
  // Create a request.
  envoy_service_load_stats_v2_LoadStatsRequest* request =
      envoy_service_load_stats_v2_LoadStatsRequest_new(arena.ptr());
  // Add cluster stats. There is only one because we only use one server name in
  // one channel.
  envoy_api_v2_endpoint_ClusterStats* cluster_stats =
      envoy_service_load_stats_v2_LoadStatsRequest_add_cluster_stats(
          request, arena.ptr());
  // Set the cluster name.
  envoy_api_v2_endpoint_ClusterStats_set_cluster_name(
      cluster_stats, upb_strview_makez(server_name));
  // Add locality stats.
  for (auto& p : snapshot.upstream_locality_stats) {
    envoy_api_v2_endpoint_UpstreamLocalityStats* locality_stats =
        envoy_api_v2_endpoint_ClusterStats_add_upstream_locality_stats(
            cluster_stats, arena.ptr());
    LocalityStatsPopulate(locality_stats, p, arena.ptr());
  }
  // Add dropped requests.
  for (auto& p : snapshot.dropped_requests) {
    const char* category = p.first.get();
    const uint64_t count = p.second;
    envoy_api_v2_endpoint_ClusterStats_DroppedRequests* dropped_requests =
        envoy_api_v2_endpoint_ClusterStats_add_dropped_requests(cluster_stats,
                                                                arena.ptr());
    envoy_api_v2_endpoint_ClusterStats_DroppedRequests_set_category(
        dropped_requests, upb_strview_makez(category));
    envoy_api_v2_endpoint_ClusterStats_DroppedRequests_set_dropped_count(
        dropped_requests, count);
  }
  // Set total dropped requests.
  envoy_api_v2_endpoint_ClusterStats_set_total_dropped_requests(
      cluster_stats, snapshot.total_dropped_requests);
  // Set real load report interval.
  gpr_timespec timespec =
      grpc_millis_to_timespec(snapshot.load_report_interval, GPR_TIMESPAN);
  google_protobuf_Duration* load_report_interval =
      envoy_api_v2_endpoint_ClusterStats_mutable_load_report_interval(
          cluster_stats, arena.ptr());
  google_protobuf_Duration_set_seconds(load_report_interval, timespec.tv_sec);
  google_protobuf_Duration_set_nanos(load_report_interval, timespec.tv_nsec);
  return LrsRequestEncode(request, arena.ptr());
}

grpc_error* XdsLrsResponseDecodeAndParse(
    const grpc_slice& encoded_response,
    grpc_core::UniquePtr<char>* cluster_name,
    grpc_millis* load_reporting_interval) {
  upb::Arena arena;
  // Decode the response.
  const envoy_service_load_stats_v2_LoadStatsResponse* decoded_response =
      envoy_service_load_stats_v2_LoadStatsResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(encoded_response)),
          GRPC_SLICE_LENGTH(encoded_response), arena.ptr());
  // Parse the response.
  if (decoded_response == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No response found.");
  }
  // Check the cluster size in the response.
  size_t size;
  const upb_strview* clusters =
      envoy_service_load_stats_v2_LoadStatsResponse_clusters(decoded_response,
                                                             &size);
  if (size != 1) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "The number of clusters (server names) is not 1.");
  }
  // Get the cluster name for reporting loads.
  *cluster_name = StringCopy(clusters[0]);
  // Get the load report interval.
  const google_protobuf_Duration* load_reporting_interval_duration =
      envoy_service_load_stats_v2_LoadStatsResponse_load_reporting_interval(
          decoded_response);
  gpr_timespec timespec{
      google_protobuf_Duration_seconds(load_reporting_interval_duration),
      google_protobuf_Duration_nanos(load_reporting_interval_duration),
      GPR_TIMESPAN};
  *load_reporting_interval = gpr_time_to_millis(timespec);
  return GRPC_ERROR_NONE;
}

}  // namespace grpc_core
