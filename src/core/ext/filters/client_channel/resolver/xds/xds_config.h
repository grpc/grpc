//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_XDS_CONFIG_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_XDS_CONFIG_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_endpoint.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_route_config.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/resolver/resolver.h"

namespace grpc_core {

// Watches all xDS resources and handles dependencies between them.
// Reports updates only when all necessary resources have been obtained.
class XdsDependencyManager : public InternallyRefCounted<XdsDependencyManager> {
 public:
  struct XdsConfig : public RefCounted<XdsConfig> {
    // Listener resource.
    std::shared_ptr<const XdsListenerResource> listener;
    // RouteConfig resource.  Will be populated even if RouteConfig is
    // inlined into the Listener resource.
    std::shared_ptr<const XdsRouteConfigResource> route_config;
    // Virtual host.  Points into route_config.  Will always be non-null.
    const XdsRouteConfigResource::VirtualHost* virtual_host;

    // Cluster map.  A cluster will have a non-OK status if either
    // (a) there was an error and we did not already have a valid
    // resource or (b) the resource does not exist.
    // For aggregate clusters, the aggregate cluster will be first in
    // the list, followed by the list of leaf clusters.
    struct ClusterConfig {
      // Cluster name and resource.
      std::string cluster_name;
      std::shared_ptr<const XdsClusterResource> cluster;
      // Endpoint info.  If there was an error, endpoints will be null
      // and resolution_note will be set.
      std::shared_ptr<const XdsEndpointResource> endpoints;
      std::string resolution_note;

      bool operator==(const ClusterConfig& other) const {
        return cluster_name == other.cluster_name && cluster == other.cluster &&
               endpoints == other.endpoints &&
               resolution_note == other.resolution_note;
      }
    };
    std::map<std::string, absl::StatusOr<std::vector<ClusterConfig>>> clusters;

    static absl::string_view ChannelArgName() {
      return GRPC_ARG_NO_SUBCHANNEL_PREFIX "xds_config";
    }
    static int ChannelArgsCompare(const XdsConfig* a, const XdsConfig* b) {
      return QsortCompare(a, b);
    }
  };

  class Watcher {
   public:
    virtual ~Watcher() = default;

    // FIXME: see if we can make this const
    virtual void OnUpdate(RefCountedPtr<XdsConfig> config) = 0;

    // These methods are invoked when there is an error or
    // does-not-exist on LDS or RDS only.
    virtual void OnError(absl::string_view context, absl::Status status) = 0;
    virtual void OnResourceDoesNotExist(std::string context) = 0;
  };

  XdsDependencyManager(RefCountedPtr<GrpcXdsClient> xds_client,
                       std::shared_ptr<WorkSerializer> work_serializer,
                       std::unique_ptr<Watcher> watcher,
                       std::string data_plane_authority,
                       std::string listener_resource_name);

  void Orphan() override;

 private:
  class ListenerWatcher;
  class RouteConfigWatcher;
  class ClusterWatcher;
  class EndpointWatcher;

  struct ClusterWatcherState {
    // Pointer to watcher, to be used when cancelling.
    // Not owned, so do not dereference.
    ClusterWatcher* watcher = nullptr;
    // Most recent update obtained from this watcher.
    absl::StatusOr<std::shared_ptr<const XdsClusterResource>> update = nullptr;
  };

  struct EndpointConfig {
    // If there was an error, update will be null and resolution_note
    // will be non-empty.
    std::shared_ptr<const XdsEndpointResource> endpoints;
    std::string resolution_note;
  };

  struct EndpointWatcherState {
    // Pointer to watcher, to be used when cancelling.
    // Not owned, so do not dereference.
    EndpointWatcher* watcher = nullptr;
    // Most recent update obtained from this watcher.
    EndpointConfig update;
  };

  struct DnsState {
    OrphanablePtr<Resolver> resolver;
    // Most recent result from the resolver.
    EndpointConfig update;
  };

  // Event handlers.
  void OnListenerUpdate(std::shared_ptr<const XdsListenerResource> listener);
  void OnRouteConfigUpdate(
      const std::string& name,
      std::shared_ptr<const XdsRouteConfigResource> route_config);
  void OnError(std::string context, absl::Status status);
  void OnResourceDoesNotExist(std::string context);

  void OnClusterUpdate(const std::string& name,
                       std::shared_ptr<const XdsClusterResource> cluster);
  void OnClusterError(const std::string& name, absl::Status status);
  void OnClusterDoesNotExist(const std::string& name);

  void OnEndpointUpdate(const std::string& name,
                        std::shared_ptr<const XdsEndpointResource> endpoint);
  void OnEndpointError(const std::string& name, absl::Status status);
  void OnEndpointDoesNotExist(const std::string& name);

  void OnDnsResult(const std::string& dns_name, Resolver::Result result);

  // Gets the set of clusters referenced in the specified route config.
  std::set<std::string> GetClustersFromRouteConfig(
      const XdsRouteConfigResource& route_config) const;

  // Starts CDS and EDS/DNS watches for the specified cluster if needed.
  // If the resource is available, adds an entry to cluster_list.
  // Adds each cluster to clusters_seen.
  // For each EDS cluster, adds the EDS resource to eds_resources_seen.
  // For aggregate clusters, calls itself recursively.
  // Returns an error if max depth is exceeded or if any of the clusters
  // in the graph report an error.
  // Returns true if all resources have been obtained.
  absl::StatusOr<bool> PopulateClusterConfigList(
      const std::string& name,
      std::vector<XdsConfig::ClusterConfig>* cluster_list, int depth,
      std::set<std::string>* clusters_seen,
      std::set<std::string>* eds_resources_seen);

  // Checks whether all necessary resources have been obtained, and if
  // so reports an update to the watcher.
  void MaybeReportUpdate();

  // Parameters passed into ctor.
  RefCountedPtr<GrpcXdsClient> xds_client_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<Watcher> watcher_;
  const std::string data_plane_authority_;
  const std::string listener_resource_name_;

  // Listener state.
  ListenerWatcher* listener_watcher_ = nullptr;
  std::shared_ptr<const XdsListenerResource> current_listener_;
  std::string route_config_name_;

  // RouteConfig state.
  RouteConfigWatcher* route_config_watcher_ = nullptr;
  std::shared_ptr<const XdsRouteConfigResource> current_route_config_;
  const XdsRouteConfigResource::VirtualHost* current_virtual_host_ = nullptr;
  std::set<std::string> clusters_from_route_config_;

  // Cluster state.
  std::map<std::string, ClusterWatcherState> cluster_watchers_;

  // Endpoint state.
  std::map<std::string, EndpointWatcherState> endpoint_watchers_;
  std::map<std::string, DnsState> dns_resolvers_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_XDS_CONFIG_H
