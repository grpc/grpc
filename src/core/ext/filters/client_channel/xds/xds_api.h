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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_API_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_API_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <set>

#include "absl/types/optional.h"

#include <grpc/slice_buffer.h>

#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/xds/xds_bootstrap.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"

namespace grpc_core {

class XdsClient;

class XdsApi {
 public:
  static const char* kLdsTypeUrl;
  static const char* kRdsTypeUrl;
  static const char* kCdsTypeUrl;
  static const char* kEdsTypeUrl;

  struct RdsUpdate {
    // The name to use in the CDS request.
    std::string cluster_name;

    bool operator==(const RdsUpdate& other) const {
      return cluster_name == other.cluster_name;
    }
  };

  // TODO(roth): When we can use absl::variant<>, consider using that
  // here, to enforce the fact that only one of the two fields can be set.
  struct LdsUpdate {
    // The name to use in the RDS request.
    std::string route_config_name;
    // The name to use in the CDS request. Present if the LDS response has it
    // inlined.
    absl::optional<RdsUpdate> rds_update;

    bool operator==(const LdsUpdate& other) const {
      return route_config_name == other.route_config_name &&
             rds_update == other.rds_update;
    }
  };

  using LdsUpdateMap = std::map<std::string /*server_name*/, LdsUpdate>;

  using RdsUpdateMap = std::map<std::string /*route_config_name*/, RdsUpdate>;

  struct CdsUpdate {
    // The name to use in the EDS request.
    // If empty, the cluster name will be used.
    std::string eds_service_name;
    // The LRS server to use for load reporting.
    // If not set, load reporting will be disabled.
    // If set to the empty string, will use the same server we obtained the CDS
    // data from.
    absl::optional<std::string> lrs_load_reporting_server_name;
  };

  using CdsUpdateMap = std::map<std::string /*cluster_name*/, CdsUpdate>;

  class PriorityListUpdate {
   public:
    struct LocalityMap {
      struct Locality {
        bool operator==(const Locality& other) const {
          return *name == *other.name && serverlist == other.serverlist &&
                 lb_weight == other.lb_weight && priority == other.priority;
        }

        // This comparator only compares the locality names.
        struct Less {
          bool operator()(const Locality& lhs, const Locality& rhs) const {
            return XdsLocalityName::Less()(lhs.name, rhs.name);
          }
        };

        RefCountedPtr<XdsLocalityName> name;
        ServerAddressList serverlist;
        uint32_t lb_weight;
        uint32_t priority;
      };

      bool Contains(const RefCountedPtr<XdsLocalityName>& name) const {
        return localities.find(name) != localities.end();
      }

      size_t size() const { return localities.size(); }

      std::map<RefCountedPtr<XdsLocalityName>, Locality, XdsLocalityName::Less>
          localities;
    };

    bool operator==(const PriorityListUpdate& other) const;
    bool operator!=(const PriorityListUpdate& other) const {
      return !(*this == other);
    }

    void Add(LocalityMap::Locality locality);

    const LocalityMap* Find(uint32_t priority) const;

    bool Contains(uint32_t priority) const {
      return priority < priorities_.size();
    }
    bool Contains(const RefCountedPtr<XdsLocalityName>& name);

    bool empty() const { return priorities_.empty(); }
    size_t size() const { return priorities_.size(); }

    // Callers should make sure the priority list is non-empty.
    uint32_t LowestPriority() const {
      return static_cast<uint32_t>(priorities_.size()) - 1;
    }

   private:
    InlinedVector<LocalityMap, 2> priorities_;
  };

  // There are two phases of accessing this class's content:
  // 1. to initialize in the control plane combiner;
  // 2. to use in the data plane combiner.
  // So no additional synchronization is needed.
  class DropConfig : public RefCounted<DropConfig> {
   public:
    struct DropCategory {
      bool operator==(const DropCategory& other) const {
        return name == other.name &&
               parts_per_million == other.parts_per_million;
      }

      std::string name;
      const uint32_t parts_per_million;
    };

    using DropCategoryList = InlinedVector<DropCategory, 2>;

    void AddCategory(std::string name, uint32_t parts_per_million) {
      drop_category_list_.emplace_back(
          DropCategory{std::move(name), parts_per_million});
      if (parts_per_million == 1000000) drop_all_ = true;
    }

    // The only method invoked from the data plane combiner.
    bool ShouldDrop(const std::string** category_name) const;

    const DropCategoryList& drop_category_list() const {
      return drop_category_list_;
    }

    bool drop_all() const { return drop_all_; }

    bool operator==(const DropConfig& other) const {
      return drop_category_list_ == other.drop_category_list_;
    }
    bool operator!=(const DropConfig& other) const { return !(*this == other); }

   private:
    DropCategoryList drop_category_list_;
    bool drop_all_ = false;
  };

  struct EdsUpdate {
    PriorityListUpdate priority_list_update;
    RefCountedPtr<DropConfig> drop_config;
  };

  using EdsUpdateMap = std::map<std::string /*eds_service_name*/, EdsUpdate>;

  struct ClusterLoadReport {
    XdsClusterDropStats::DroppedRequestsMap dropped_requests;
    std::map<RefCountedPtr<XdsLocalityName>, XdsClusterLocalityStats::Snapshot,
             XdsLocalityName::Less>
        locality_stats;
    grpc_millis load_report_interval;
  };
  using ClusterLoadReportMap = std::map<
      std::pair<std::string /*cluster_name*/, std::string /*eds_service_name*/>,
      ClusterLoadReport>;

  XdsApi(XdsClient* client, TraceFlag* tracer, const XdsBootstrap::Node* node);

  // Creates a request to nack an unsupported resource type.
  // Takes ownership of \a error.
  grpc_slice CreateUnsupportedTypeNackRequest(const std::string& type_url,
                                              const std::string& nonce,
                                              grpc_error* error);

  // Creates an LDS request querying \a server_name.
  // Takes ownership of \a error.
  grpc_slice CreateLdsRequest(const std::string& server_name,
                              const std::string& version,
                              const std::string& nonce, grpc_error* error,
                              bool populate_node);

  // Creates an RDS request querying \a route_config_name.
  // Takes ownership of \a error.
  grpc_slice CreateRdsRequest(const std::string& route_config_name,
                              const std::string& version,
                              const std::string& nonce, grpc_error* error,
                              bool populate_node);

  // Creates a CDS request querying \a cluster_names.
  // Takes ownership of \a error.
  grpc_slice CreateCdsRequest(const std::set<StringView>& cluster_names,
                              const std::string& version,
                              const std::string& nonce, grpc_error* error,
                              bool populate_node);

  // Creates an EDS request querying \a eds_service_names.
  // Takes ownership of \a error.
  grpc_slice CreateEdsRequest(const std::set<StringView>& eds_service_names,
                              const std::string& version,
                              const std::string& nonce, grpc_error* error,
                              bool populate_node);

  // Parses the ADS response and outputs the validated update for either CDS or
  // EDS. If the response can't be parsed at the top level, \a type_url will
  // point to an empty string; otherwise, it will point to the received data.
  grpc_error* ParseAdsResponse(
      const grpc_slice& encoded_response,
      const std::string& expected_server_name,
      const std::string& expected_route_config_name,
      const std::set<StringView>& expected_cluster_names,
      const std::set<StringView>& expected_eds_service_names,
      absl::optional<LdsUpdate>* lds_update,
      absl::optional<RdsUpdate>* rds_update, CdsUpdateMap* cds_update_map,
      EdsUpdateMap* eds_update_map, std::string* version, std::string* nonce,
      std::string* type_url);

  // Creates an LRS request querying \a server_name.
  grpc_slice CreateLrsInitialRequest(const std::string& server_name);

  // Creates an LRS request sending a client-side load report.
  grpc_slice CreateLrsRequest(ClusterLoadReportMap cluster_load_report_map);

  // Parses the LRS response and returns \a
  // load_reporting_interval for client-side load reporting. If there is any
  // error, the output config is invalid.
  grpc_error* ParseLrsResponse(const grpc_slice& encoded_response,
                               std::set<std::string>* cluster_names,
                               grpc_millis* load_reporting_interval);

 private:
  XdsClient* client_;
  TraceFlag* tracer_;
  const XdsBootstrap::Node* node_;
  const std::string build_version_;
  const std::string user_agent_name_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_API_H */
