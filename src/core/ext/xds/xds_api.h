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

#include <stddef.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "envoy/admin/v3/config_dump_shared.upb.h"
#include "upb/arena.h"
#include "upb/def.hpp"

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_core {

class XdsClient;

// TODO(roth): When we have time, split this into multiple pieces:
// - ADS request/response handling
// - LRS request/response handling
// - CSDS response generation
class XdsApi {
 public:
  // Interface defined by caller and passed to ParseAdsResponse().
  class AdsResponseParserInterface {
   public:
    struct AdsResponseFields {
      std::string type_url;
      std::string version;
      std::string nonce;
      size_t num_resources;
    };

    virtual ~AdsResponseParserInterface() = default;

    // Called when the top-level ADS fields are parsed.
    // If this returns non-OK, parsing will stop, and the individual
    // resources will not be processed.
    virtual absl::Status ProcessAdsResponseFields(AdsResponseFields fields) = 0;

    // Called to parse each individual resource in the ADS response.
    virtual void ParseResource(upb_Arena* arena, size_t idx,
                               absl::string_view type_url,
                               absl::string_view serialized_resource) = 0;
  };

  struct ClusterLoadReport {
    XdsClusterDropStats::Snapshot dropped_requests;
    std::map<RefCountedPtr<XdsLocalityName>, XdsClusterLocalityStats::Snapshot,
             XdsLocalityName::Less>
        locality_stats;
    Duration load_report_interval;
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
    Timestamp update_time;
    // The last successfully updated version of the resource.
    std::string version;
    // The rejected version string of the last failed update attempt.
    std::string failed_version;
    // Details about the last failed update attempt.
    std::string failed_details;
    // Timestamp of the last failed update attempt.
    Timestamp failed_update_time;
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

  XdsApi(XdsClient* client, TraceFlag* tracer, const XdsBootstrap::Node* node,
         upb::SymbolTable* symtab);

  // Creates an ADS request.
  // Takes ownership of \a error.
  std::string CreateAdsRequest(const XdsBootstrap::XdsServer& server,
                               absl::string_view type_url,
                               absl::string_view version,
                               absl::string_view nonce,
                               const std::vector<std::string>& resource_names,
                               absl::Status status, bool populate_node);

  // Returns non-OK when failing to deserialize response message.
  // Otherwise, all events are reported to the parser.
  absl::Status ParseAdsResponse(const XdsBootstrap::XdsServer& server,
                                absl::string_view encoded_response,
                                AdsResponseParserInterface* parser);

  // Creates an initial LRS request.
  std::string CreateLrsInitialRequest(const XdsBootstrap::XdsServer& server);

  // Creates an LRS request sending a client-side load report.
  std::string CreateLrsRequest(ClusterLoadReportMap cluster_load_report_map);

  // Parses the LRS response and populates send_all_clusters,
  // cluster_names, and load_reporting_interval.
  absl::Status ParseLrsResponse(absl::string_view encoded_response,
                                bool* send_all_clusters,
                                std::set<std::string>* cluster_names,
                                Duration* load_reporting_interval);

  // Assemble the client config proto message and return the serialized result.
  std::string AssembleClientConfig(
      const ResourceTypeMetadataMap& resource_type_metadata_map);

 private:
  XdsClient* client_;
  TraceFlag* tracer_;
  const XdsBootstrap::Node* node_;  // Do not own.
  upb::SymbolTable* symtab_;        // Do not own.
  const std::string build_version_;
  const std::string user_agent_name_;
  const std::string user_agent_version_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_API_H
