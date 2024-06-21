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

#include "src/core/xds/xds_client/xds_api.h"

#include <stdint.h>
#include <stdlib.h>

#include <set>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/config/endpoint/v3/load_report.upb.h"
#include "envoy/service/discovery/v3/discovery.upb.h"
#include "envoy/service/discovery/v3/discovery.upbdefs.h"
#include "envoy/service/load_stats/v3/lrs.upb.h"
#include "envoy/service/load_stats/v3/lrs.upbdefs.h"
#include "envoy/service/status/v3/csds.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/rpc/status.upb.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.h"
#include "upb/text/encode.h"

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "src/core/util/json/json.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/xds_client/xds_client.h"

// IWYU pragma: no_include "upb/msg_internal.h"

namespace grpc_core {

XdsApi::XdsApi(XdsClient* client, TraceFlag* tracer,
               const XdsBootstrap::Node* node, upb::DefPool* def_pool,
               std::string user_agent_name, std::string user_agent_version)
    : client_(client),
      tracer_(tracer),
      node_(node),
      def_pool_(def_pool),
      user_agent_name_(std::move(user_agent_name)),
      user_agent_version_(std::move(user_agent_version)) {}

namespace {

struct XdsApiContext {
  XdsClient* client;
  TraceFlag* tracer;
  upb_DefPool* def_pool;
  upb_Arena* arena;
};

void PopulateMetadataValue(google_protobuf_Value* value_pb, const Json& value,
                           upb_Arena* arena);

void PopulateListValue(google_protobuf_ListValue* list_value,
                       const Json::Array& values, upb_Arena* arena) {
  for (const auto& value : values) {
    auto* value_pb = google_protobuf_ListValue_add_values(list_value, arena);
    PopulateMetadataValue(value_pb, value, arena);
  }
}

void PopulateMetadata(google_protobuf_Struct* metadata_pb,
                      const Json::Object& metadata, upb_Arena* arena) {
  for (const auto& p : metadata) {
    google_protobuf_Value* value = google_protobuf_Value_new(arena);
    PopulateMetadataValue(value, p.second, arena);
    google_protobuf_Struct_fields_set(
        metadata_pb, StdStringToUpbString(p.first), value, arena);
  }
}

void PopulateMetadataValue(google_protobuf_Value* value_pb, const Json& value,
                           upb_Arena* arena) {
  switch (value.type()) {
    case Json::Type::kNull:
      google_protobuf_Value_set_null_value(value_pb, 0);
      break;
    case Json::Type::kNumber:
      google_protobuf_Value_set_number_value(
          value_pb, strtod(value.string().c_str(), nullptr));
      break;
    case Json::Type::kString:
      google_protobuf_Value_set_string_value(
          value_pb, StdStringToUpbString(value.string()));
      break;
    case Json::Type::kBoolean:
      google_protobuf_Value_set_bool_value(value_pb, value.boolean());
      break;
    case Json::Type::kObject: {
      google_protobuf_Struct* struct_value =
          google_protobuf_Value_mutable_struct_value(value_pb, arena);
      PopulateMetadata(struct_value, value.object(), arena);
      break;
    }
    case Json::Type::kArray: {
      google_protobuf_ListValue* list_value =
          google_protobuf_Value_mutable_list_value(value_pb, arena);
      PopulateListValue(list_value, value.array(), arena);
      break;
    }
  }
}

void MaybeLogDiscoveryRequest(
    const XdsApiContext& context,
    const envoy_service_discovery_v3_DiscoveryRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_discovery_v3_DiscoveryRequest_getmsgdef(context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(request), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] constructed ADS request: " << buf;
  }
}

std::string SerializeDiscoveryRequest(
    const XdsApiContext& context,
    envoy_service_discovery_v3_DiscoveryRequest* request) {
  size_t output_length;
  char* output = envoy_service_discovery_v3_DiscoveryRequest_serialize(
      request, context.arena, &output_length);
  return std::string(output, output_length);
}

}  // namespace

void XdsApi::PopulateNode(envoy_config_core_v3_Node* node_msg,
                          upb_Arena* arena) {
  if (node_ != nullptr) {
    if (!node_->id().empty()) {
      envoy_config_core_v3_Node_set_id(node_msg,
                                       StdStringToUpbString(node_->id()));
    }
    if (!node_->cluster().empty()) {
      envoy_config_core_v3_Node_set_cluster(
          node_msg, StdStringToUpbString(node_->cluster()));
    }
    if (!node_->metadata().empty()) {
      google_protobuf_Struct* metadata =
          envoy_config_core_v3_Node_mutable_metadata(node_msg, arena);
      PopulateMetadata(metadata, node_->metadata(), arena);
    }
    if (!node_->locality_region().empty() || !node_->locality_zone().empty() ||
        !node_->locality_sub_zone().empty()) {
      envoy_config_core_v3_Locality* locality =
          envoy_config_core_v3_Node_mutable_locality(node_msg, arena);
      if (!node_->locality_region().empty()) {
        envoy_config_core_v3_Locality_set_region(
            locality, StdStringToUpbString(node_->locality_region()));
      }
      if (!node_->locality_zone().empty()) {
        envoy_config_core_v3_Locality_set_zone(
            locality, StdStringToUpbString(node_->locality_zone()));
      }
      if (!node_->locality_sub_zone().empty()) {
        envoy_config_core_v3_Locality_set_sub_zone(
            locality, StdStringToUpbString(node_->locality_sub_zone()));
      }
    }
  }
  envoy_config_core_v3_Node_set_user_agent_name(
      node_msg, StdStringToUpbString(user_agent_name_));
  envoy_config_core_v3_Node_set_user_agent_version(
      node_msg, StdStringToUpbString(user_agent_version_));
  envoy_config_core_v3_Node_add_client_features(
      node_msg,
      upb_StringView_FromString("envoy.lb.does_not_support_overprovisioning"),
      arena);
}

std::string XdsApi::CreateAdsRequest(
    absl::string_view type_url, absl::string_view version,
    absl::string_view nonce, const std::vector<std::string>& resource_names,
    absl::Status status, bool populate_node) {
  upb::Arena arena;
  const XdsApiContext context = {client_, tracer_, def_pool_->ptr(),
                                 arena.ptr()};
  // Create a request.
  envoy_service_discovery_v3_DiscoveryRequest* request =
      envoy_service_discovery_v3_DiscoveryRequest_new(arena.ptr());
  // Set type_url.
  std::string type_url_str = absl::StrCat("type.googleapis.com/", type_url);
  envoy_service_discovery_v3_DiscoveryRequest_set_type_url(
      request, StdStringToUpbString(type_url_str));
  // Set version_info.
  if (!version.empty()) {
    envoy_service_discovery_v3_DiscoveryRequest_set_version_info(
        request, StdStringToUpbString(version));
  }
  // Set nonce.
  if (!nonce.empty()) {
    envoy_service_discovery_v3_DiscoveryRequest_set_response_nonce(
        request, StdStringToUpbString(nonce));
  }
  // Set error_detail if it's a NACK.
  std::string error_string_storage;
  if (!status.ok()) {
    google_rpc_Status* error_detail =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_error_detail(
            request, arena.ptr());
    // Hard-code INVALID_ARGUMENT as the status code.
    // TODO(roth): If at some point we decide we care about this value,
    // we could attach a status code to the individual errors where we
    // generate them in the parsing code, and then use that here.
    google_rpc_Status_set_code(error_detail, GRPC_STATUS_INVALID_ARGUMENT);
    // Error description comes from the status that was passed in.
    error_string_storage = std::string(status.message());
    upb_StringView error_description =
        StdStringToUpbString(error_string_storage);
    google_rpc_Status_set_message(error_detail, error_description);
  }
  // Populate node.
  if (populate_node) {
    envoy_config_core_v3_Node* node_msg =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_node(request,
                                                                 arena.ptr());
    PopulateNode(node_msg, arena.ptr());
    envoy_config_core_v3_Node_add_client_features(
        node_msg, upb_StringView_FromString("xds.config.resource-in-sotw"),
        context.arena);
  }
  // Add resource_names.
  for (const std::string& resource_name : resource_names) {
    envoy_service_discovery_v3_DiscoveryRequest_add_resource_names(
        request, StdStringToUpbString(resource_name), arena.ptr());
  }
  MaybeLogDiscoveryRequest(context, request);
  return SerializeDiscoveryRequest(context, request);
}

namespace {

void MaybeLogDiscoveryResponse(
    const XdsApiContext& context,
    const envoy_service_discovery_v3_DiscoveryResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_discovery_v3_DiscoveryResponse_getmsgdef(
            context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(response), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] received response: " << buf;
  }
}

}  // namespace

absl::Status XdsApi::ParseAdsResponse(absl::string_view encoded_response,
                                      AdsResponseParserInterface* parser) {
  upb::Arena arena;
  const XdsApiContext context = {client_, tracer_, def_pool_->ptr(),
                                 arena.ptr()};
  // Decode the response.
  const envoy_service_discovery_v3_DiscoveryResponse* response =
      envoy_service_discovery_v3_DiscoveryResponse_parse(
          encoded_response.data(), encoded_response.size(), arena.ptr());
  // If decoding fails, report a fatal error and return.
  if (response == nullptr) {
    return absl::InvalidArgumentError("Can't decode DiscoveryResponse.");
  }
  MaybeLogDiscoveryResponse(context, response);
  // Report the type_url, version, nonce, and number of resources to the parser.
  AdsResponseParserInterface::AdsResponseFields fields;
  fields.type_url = std::string(absl::StripPrefix(
      UpbStringToAbsl(
          envoy_service_discovery_v3_DiscoveryResponse_type_url(response)),
      "type.googleapis.com/"));
  fields.version = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_version_info(response));
  fields.nonce = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_nonce(response));
  size_t num_resources;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response,
                                                             &num_resources);
  fields.num_resources = num_resources;
  absl::Status status = parser->ProcessAdsResponseFields(std::move(fields));
  if (!status.ok()) return status;
  // Process each resource.
  for (size_t i = 0; i < num_resources; ++i) {
    absl::string_view type_url = absl::StripPrefix(
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i])),
        "type.googleapis.com/");
    absl::string_view serialized_resource =
        UpbStringToAbsl(google_protobuf_Any_value(resources[i]));
    // Unwrap Resource messages, if so wrapped.
    absl::string_view resource_name;
    if (type_url == "envoy.service.discovery.v3.Resource") {
      const auto* resource_wrapper = envoy_service_discovery_v3_Resource_parse(
          serialized_resource.data(), serialized_resource.size(), arena.ptr());
      if (resource_wrapper == nullptr) {
        parser->ResourceWrapperParsingFailed(
            i, "Can't decode Resource proto wrapper");
        continue;
      }
      const auto* resource =
          envoy_service_discovery_v3_Resource_resource(resource_wrapper);
      if (resource == nullptr) {
        parser->ResourceWrapperParsingFailed(
            i, "No resource present in Resource proto wrapper");
        continue;
      }
      type_url = absl::StripPrefix(
          UpbStringToAbsl(google_protobuf_Any_type_url(resource)),
          "type.googleapis.com/");
      serialized_resource =
          UpbStringToAbsl(google_protobuf_Any_value(resource));
      resource_name = UpbStringToAbsl(
          envoy_service_discovery_v3_Resource_name(resource_wrapper));
    }
    parser->ParseResource(context.arena, i, type_url, resource_name,
                          serialized_resource);
  }
  return absl::OkStatus();
}

namespace {

void MaybeLogLrsRequest(
    const XdsApiContext& context,
    const envoy_service_load_stats_v3_LoadStatsRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_load_stats_v3_LoadStatsRequest_getmsgdef(
            context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(request), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] constructed LRS request: " << buf;
  }
}

std::string SerializeLrsRequest(
    const XdsApiContext& context,
    const envoy_service_load_stats_v3_LoadStatsRequest* request) {
  size_t output_length;
  char* output = envoy_service_load_stats_v3_LoadStatsRequest_serialize(
      request, context.arena, &output_length);
  return std::string(output, output_length);
}

}  // namespace

std::string XdsApi::CreateLrsInitialRequest() {
  upb::Arena arena;
  const XdsApiContext context = {client_, tracer_, def_pool_->ptr(),
                                 arena.ptr()};
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  // Populate node.
  envoy_config_core_v3_Node* node_msg =
      envoy_service_load_stats_v3_LoadStatsRequest_mutable_node(request,
                                                                arena.ptr());
  PopulateNode(node_msg, arena.ptr());
  envoy_config_core_v3_Node_add_client_features(
      node_msg,
      upb_StringView_FromString("envoy.lrs.supports_send_all_clusters"),
      arena.ptr());
  MaybeLogLrsRequest(context, request);
  return SerializeLrsRequest(context, request);
}

namespace {

void LocalityStatsPopulate(
    const XdsApiContext& context,
    envoy_config_endpoint_v3_UpstreamLocalityStats* output,
    const XdsLocalityName& locality_name,
    const XdsClusterLocalityStats::Snapshot& snapshot) {
  // Set locality.
  envoy_config_core_v3_Locality* locality =
      envoy_config_endpoint_v3_UpstreamLocalityStats_mutable_locality(
          output, context.arena);
  if (!locality_name.region().empty()) {
    envoy_config_core_v3_Locality_set_region(
        locality, StdStringToUpbString(locality_name.region()));
  }
  if (!locality_name.zone().empty()) {
    envoy_config_core_v3_Locality_set_zone(
        locality, StdStringToUpbString(locality_name.zone()));
  }
  if (!locality_name.sub_zone().empty()) {
    envoy_config_core_v3_Locality_set_sub_zone(
        locality, StdStringToUpbString(locality_name.sub_zone()));
  }
  // Set total counts.
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_successful_requests(
      output, snapshot.total_successful_requests);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_requests_in_progress(
      output, snapshot.total_requests_in_progress);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_error_requests(
      output, snapshot.total_error_requests);
  envoy_config_endpoint_v3_UpstreamLocalityStats_set_total_issued_requests(
      output, snapshot.total_issued_requests);
  // Add backend metrics.
  for (const auto& p : snapshot.backend_metrics) {
    const std::string& metric_name = p.first;
    const XdsClusterLocalityStats::BackendMetric& metric_value = p.second;
    envoy_config_endpoint_v3_EndpointLoadMetricStats* load_metric =
        envoy_config_endpoint_v3_UpstreamLocalityStats_add_load_metric_stats(
            output, context.arena);
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_metric_name(
        load_metric, StdStringToUpbString(metric_name));
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_num_requests_finished_with_metric(
        load_metric, metric_value.num_requests_finished_with_metric);
    envoy_config_endpoint_v3_EndpointLoadMetricStats_set_total_metric_value(
        load_metric, metric_value.total_metric_value);
  }
}

}  // namespace

std::string XdsApi::CreateLrsRequest(
    ClusterLoadReportMap cluster_load_report_map) {
  upb::Arena arena;
  const XdsApiContext context = {client_, tracer_, def_pool_->ptr(),
                                 arena.ptr()};
  // Create a request.
  envoy_service_load_stats_v3_LoadStatsRequest* request =
      envoy_service_load_stats_v3_LoadStatsRequest_new(arena.ptr());
  for (auto& p : cluster_load_report_map) {
    const std::string& cluster_name = p.first.first;
    const std::string& eds_service_name = p.first.second;
    const ClusterLoadReport& load_report = p.second;
    // Add cluster stats.
    envoy_config_endpoint_v3_ClusterStats* cluster_stats =
        envoy_service_load_stats_v3_LoadStatsRequest_add_cluster_stats(
            request, arena.ptr());
    // Set the cluster name.
    envoy_config_endpoint_v3_ClusterStats_set_cluster_name(
        cluster_stats, StdStringToUpbString(cluster_name));
    // Set EDS service name, if non-empty.
    if (!eds_service_name.empty()) {
      envoy_config_endpoint_v3_ClusterStats_set_cluster_service_name(
          cluster_stats, StdStringToUpbString(eds_service_name));
    }
    // Add locality stats.
    for (const auto& p : load_report.locality_stats) {
      const XdsLocalityName& locality_name = *p.first;
      const auto& snapshot = p.second;
      envoy_config_endpoint_v3_UpstreamLocalityStats* locality_stats =
          envoy_config_endpoint_v3_ClusterStats_add_upstream_locality_stats(
              cluster_stats, arena.ptr());
      LocalityStatsPopulate(context, locality_stats, locality_name, snapshot);
    }
    // Add dropped requests.
    uint64_t total_dropped_requests = 0;
    for (const auto& p : load_report.dropped_requests.categorized_drops) {
      const std::string& category = p.first;
      const uint64_t count = p.second;
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests* dropped_requests =
          envoy_config_endpoint_v3_ClusterStats_add_dropped_requests(
              cluster_stats, arena.ptr());
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests_set_category(
          dropped_requests, StdStringToUpbString(category));
      envoy_config_endpoint_v3_ClusterStats_DroppedRequests_set_dropped_count(
          dropped_requests, count);
      total_dropped_requests += count;
    }
    total_dropped_requests += load_report.dropped_requests.uncategorized_drops;
    // Set total dropped requests.
    envoy_config_endpoint_v3_ClusterStats_set_total_dropped_requests(
        cluster_stats, total_dropped_requests);
    // Set real load report interval.
    gpr_timespec timespec = load_report.load_report_interval.as_timespec();
    google_protobuf_Duration* load_report_interval =
        envoy_config_endpoint_v3_ClusterStats_mutable_load_report_interval(
            cluster_stats, arena.ptr());
    google_protobuf_Duration_set_seconds(load_report_interval, timespec.tv_sec);
    google_protobuf_Duration_set_nanos(load_report_interval, timespec.tv_nsec);
  }
  MaybeLogLrsRequest(context, request);
  return SerializeLrsRequest(context, request);
}

namespace {

void MaybeLogLrsResponse(
    const XdsApiContext& context,
    const envoy_service_load_stats_v3_LoadStatsResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_load_stats_v3_LoadStatsResponse_getmsgdef(
            context.def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(response), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] received LRS response: " << buf;
  }
}

}  // namespace

absl::Status XdsApi::ParseLrsResponse(absl::string_view encoded_response,
                                      bool* send_all_clusters,
                                      std::set<std::string>* cluster_names,
                                      Duration* load_reporting_interval) {
  upb::Arena arena;
  // Decode the response.
  const envoy_service_load_stats_v3_LoadStatsResponse* decoded_response =
      envoy_service_load_stats_v3_LoadStatsResponse_parse(
          encoded_response.data(), encoded_response.size(), arena.ptr());
  // Parse the response.
  if (decoded_response == nullptr) {
    return absl::UnavailableError("Can't decode response.");
  }
  const XdsApiContext context = {client_, tracer_, def_pool_->ptr(),
                                 arena.ptr()};
  MaybeLogLrsResponse(context, decoded_response);
  // Check send_all_clusters.
  if (envoy_service_load_stats_v3_LoadStatsResponse_send_all_clusters(
          decoded_response)) {
    *send_all_clusters = true;
  } else {
    // Store the cluster names.
    size_t size;
    const upb_StringView* clusters =
        envoy_service_load_stats_v3_LoadStatsResponse_clusters(decoded_response,
                                                               &size);
    for (size_t i = 0; i < size; ++i) {
      cluster_names->emplace(UpbStringToStdString(clusters[i]));
    }
  }
  // Get the load report interval.
  const google_protobuf_Duration* load_reporting_interval_duration =
      envoy_service_load_stats_v3_LoadStatsResponse_load_reporting_interval(
          decoded_response);
  *load_reporting_interval = Duration::FromSecondsAndNanoseconds(
      google_protobuf_Duration_seconds(load_reporting_interval_duration),
      google_protobuf_Duration_nanos(load_reporting_interval_duration));
  return absl::OkStatus();
}

}  // namespace grpc_core
