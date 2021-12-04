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

#ifndef GRPC_CORE_EXT_XDS_XDS_API_H
#define GRPC_CORE_EXT_XDS_XDS_API_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <set>

#include "absl/container/inlined_vector.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "envoy/admin/v3/config_dump.upb.h"
#include "re2/re2.h"
#include "upb/def.hpp"

#include <grpc/slice_buffer.h>

#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_endpoint.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_route_config.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

class XdsClient;

class XdsApi {
 public:
  static const char* kLdsTypeUrl;
  static const char* kRdsTypeUrl;
  static const char* kCdsTypeUrl;
  static const char* kEdsTypeUrl;

  struct ResourceName {
    std::string authority;
    std::string id;

    bool operator<(const ResourceName& other) const {
      if (authority < other.authority) return true;
      if (id < other.id) return true;
      return false;
    }
  };

  struct LdsResourceData {
    XdsListenerResource resource;
    std::string serialized_proto;
  };
  using LdsUpdateMap = std::map<ResourceName, LdsResourceData>;

  struct RdsResourceData {
    XdsRouteConfigResource resource;
    std::string serialized_proto;
  };
  using RdsUpdateMap = std::map<ResourceName, RdsResourceData>;

  struct CdsResourceData {
    XdsClusterResource resource;
    std::string serialized_proto;
  };
  using CdsUpdateMap = std::map<ResourceName, CdsResourceData>;

  struct EdsResourceData {
    XdsEndpointResource resource;
    std::string serialized_proto;
  };
  using EdsUpdateMap = std::map<ResourceName, EdsResourceData>;

  struct ClusterLoadReport {
    XdsClusterDropStats::Snapshot dropped_requests;
    std::map<RefCountedPtr<XdsLocalityName>, XdsClusterLocalityStats::Snapshot,
             XdsLocalityName::Less>
        locality_stats;
    grpc_millis load_report_interval;
  };
  using ClusterLoadReportMap = std::map<
      std::pair<std::string /*cluster_name*/, std::string /*eds_service_name*/>,
      ClusterLoadReport>;

  // The metadata of the xDS resource; used by the xDS config dump.
  struct ResourceMetadata {
    // Resource status from the view of a xDS client, which tells the
    // synchronization status between the xDS client and the xDS server.
    enum ClientResourceStatus {
      // Client requested this resource but hasn't received any update from
      // management server. The client will not fail requests, but will queue
      // them
      // until update arrives or the client times out waiting for the resource.
      REQUESTED = 1,
      // This resource has been requested by the client but has either not been
      // delivered by the server or was previously delivered by the server and
      // then subsequently removed from resources provided by the server.
      DOES_NOT_EXIST,
      // Client received this resource and replied with ACK.
      ACKED,
      // Client received this resource and replied with NACK.
      NACKED
    };

    // The client status of this resource.
    ClientResourceStatus client_status = REQUESTED;
    // The serialized bytes of the last successfully updated raw xDS resource.
    std::string serialized_proto;
    // The timestamp when the resource was last successfully updated.
    grpc_millis update_time = 0;
    // The last successfully updated version of the resource.
    std::string version;
    // The rejected version string of the last failed update attempt.
    std::string failed_version;
    // Details about the last failed update attempt.
    std::string failed_details;
    // Timestamp of the last failed update attempt.
    grpc_millis failed_update_time = 0;
  };
  using ResourceMetadataMap =
      std::map<std::string /*resource_name*/, const ResourceMetadata*>;
  using ResourceTypeMetadataMap =
      std::map<absl::string_view /*type_url*/, ResourceMetadataMap>;
  static_assert(static_cast<ResourceMetadata::ClientResourceStatus>(
                    envoy_admin_v3_REQUESTED) ==
                    ResourceMetadata::ClientResourceStatus::REQUESTED,
                "");
  static_assert(static_cast<ResourceMetadata::ClientResourceStatus>(
                    envoy_admin_v3_DOES_NOT_EXIST) ==
                    ResourceMetadata::ClientResourceStatus::DOES_NOT_EXIST,
                "");
  static_assert(static_cast<ResourceMetadata::ClientResourceStatus>(
                    envoy_admin_v3_ACKED) ==
                    ResourceMetadata::ClientResourceStatus::ACKED,
                "");
  static_assert(static_cast<ResourceMetadata::ClientResourceStatus>(
                    envoy_admin_v3_NACKED) ==
                    ResourceMetadata::ClientResourceStatus::NACKED,
                "");

  // If the response can't be parsed at the top level, the resulting
  // type_url will be empty.
  // If there is any other type of validation error, the parse_error
  // field will be set to something other than GRPC_ERROR_NONE and the
  // resource_names_failed field will be populated.
  // Otherwise, one of the *_update_map fields will be populated, based
  // on the type_url field.
  struct AdsParseResult {
    grpc_error_handle parse_error = GRPC_ERROR_NONE;
    std::string version;
    std::string nonce;
    std::string type_url;
    LdsUpdateMap lds_update_map;
    RdsUpdateMap rds_update_map;
    CdsUpdateMap cds_update_map;
    EdsUpdateMap eds_update_map;
    std::set<ResourceName> resource_names_failed;
  };

  XdsApi(XdsClient* client, TraceFlag* tracer, const XdsBootstrap::Node* node,
         const CertificateProviderStore::PluginDefinitionMap* map);

  static bool IsLds(absl::string_view type_url);
  static bool IsRds(absl::string_view type_url);
  static bool IsCds(absl::string_view type_url);
  static bool IsEds(absl::string_view type_url);

  // A helper method to parse the resource name and return back a ResourceName
  // struct.  Optionally the parser can check the resource type portion of the
  // resource name.
  static absl::StatusOr<ResourceName> ParseResourceName(
      absl::string_view name,
      bool (*is_expected_type)(absl::string_view) = nullptr);

  // A helper method to construct the resource name from parts.
  static std::string ConstructFullResourceName(absl::string_view authority,
                                               absl::string_view resource_type,
                                               absl::string_view name);

  // Creates an ADS request.
  // Takes ownership of \a error.
  grpc_slice CreateAdsRequest(
      const XdsBootstrap::XdsServer& server, const std::string& type_url,
      const std::map<absl::string_view /*authority*/,
                     std::set<absl::string_view /*name*/>>& resource_names,
      const std::string& version, const std::string& nonce,
      grpc_error_handle error, bool populate_node);

  // Parses an ADS response.
  AdsParseResult ParseAdsResponse(
      const XdsBootstrap::XdsServer& server, const grpc_slice& encoded_response,
      const std::map<absl::string_view /*authority*/,
                     std::set<absl::string_view /*name*/>>&
          subscribed_listener_names,
      const std::map<absl::string_view /*authority*/,
                     std::set<absl::string_view /*name*/>>&
          subscribed_route_config_names,
      const std::map<absl::string_view /*authority*/,
                     std::set<absl::string_view /*name*/>>&
          subscribed_cluster_names,
      const std::map<absl::string_view /*authority*/,
                     std::set<absl::string_view /*name*/>>&
          subscribed_eds_service_names);

  // Creates an initial LRS request.
  grpc_slice CreateLrsInitialRequest(const XdsBootstrap::XdsServer& server);

  // Creates an LRS request sending a client-side load report.
  grpc_slice CreateLrsRequest(ClusterLoadReportMap cluster_load_report_map);

  // Parses the LRS response and returns \a
  // load_reporting_interval for client-side load reporting. If there is any
  // error, the output config is invalid.
  grpc_error_handle ParseLrsResponse(const grpc_slice& encoded_response,
                                     bool* send_all_clusters,
                                     std::set<std::string>* cluster_names,
                                     grpc_millis* load_reporting_interval);

  // Assemble the client config proto message and return the serialized result.
  std::string AssembleClientConfig(
      const ResourceTypeMetadataMap& resource_type_metadata_map);

 private:
  XdsClient* client_;
  TraceFlag* tracer_;
  const XdsBootstrap::Node* node_;  // Do not own.
  const CertificateProviderStore::PluginDefinitionMap*
      certificate_provider_definition_map_;  // Do not own.
  upb::SymbolTable symtab_;
  const std::string build_version_;
  const std::string user_agent_name_;
  const std::string user_agent_version_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_API_H
