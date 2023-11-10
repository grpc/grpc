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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_XDS_XDS_CONFIG_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_XDS_XDS_CONFIG_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_route_config.h"
#include "src/core/ext/xds/xds_endpoint.h"

namespace grpc_core {

// Maximum depth of aggregate cluster tree.
constexpr int kMaxXdsAggregateClusterRecursionDepth = 16;

// Watches all xDS resources and handles dependencies between them.
// Reports updates only when all necessary resources have been obtained.
class XdsConfigWatcher : public InternallyRefCounted<XdsConfigWatcher> {
 public:
  // FIXME: Need a way to return values when only a subset of clusters are
  // missing data, because if the channel is first starting up, we might
  // prefer that to putting the channel in TRANSIENT_FAILURE.
  // FIXME: These don't need to be shared_ptr, because they're not going
  // to be held by the xds resolver between updates.
  struct XdsConfig {
    std::shared_ptr<const XdsListenerResource> listener;
    std::shared_ptr<const XdsRouteConfigResource> route_config;
    std::map<std::string, std::shared_ptr<const XdsClusterResource>> clusters;
    std::map<std::string, std::shared_ptr<const XdsEndpointResource>> endpoints;
    std::map<std::string, std::shared_ptr<const EndpointAddressesList>>
        dns_results;
    std::string resolution_note;
  };

  class Watcher {
   public:
    virtual ~Watcher() = default;
    virtual void OnUpdate(XdsConfig config) = 0;
    virtual void OnError(absl::string_view context, absl::Status status) = 0;
    virtual void OnResourceDoesNotExist(std::string context) = 0;
  };

  XdsConfigWatcher(
      RefCountedPtr<GrpcXdsClient> xds_client,
      std::shared_ptr<WorkSerializer> work_serializer,
      std::unique_ptr<Watcher> watcher, std::string data_plane_authority,
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
    std::shared_ptr<const XdsClusterResource> update;
  };

  struct EndpointWatcherState {
    // Pointer to watcher, to be used when cancelling.
    // Not owned, so do not dereference.
    EndpointWatcher* watcher = nullptr;
    // Most recent update obtained from this watcher.
    std::shared_ptr<const XdsEndpointResource> update;
    std::string resolution_note;
  };

  struct DnsState {
    OrphanablePtr<Resolver> resolver;
    // Most recent result from the resolver.
    std::shared_ptr<const EndpointAddressesList> addresses;
    std::string resolution_note;
  };

  // Event handlers.
  void OnListenerUpdate(std::shared_ptr<const XdsListenerResource> listener);
  void OnRouteConfigUpdate(
      std::shared_ptr<const XdsRouteConfigResource> route_config);
  void OnClusterUpdate(const std::string& name,
                       std::shared_ptr<const XdsClusterResource> cluster);
  void OnEndpointUpdate(const std::string& name,
                        std::shared_ptr<const XdsEndpointResource> endpoint,
                        std::string resolution_note);
  void OnDnsResult(const std::string& dns_name, EndpointAddressesList addresses,
                   std::string resolution_note);
  void OnError(std::string context, absl::Status status);
  void OnResourceDoesNotExist(std::string context);

  // Gets the set of clusters referenced in the specified route config.
  std::set<std::string> GetClustersFromRouteConfig(
      const XdsRouteConfigResource& route_config) const;

  // Returns true if the cluster resource is present; otherwise, returns
  // false.  If no watcher has been started for the cluster, starts it.
  // Adds each cluster to clusters_seen.
  // For each EDS cluster, adds the EDS resource to eds_resources_seen.
  // For aggregate clusters, calls itself recursively.  Returns an error if
  // max depth is exceeded.
  absl::StatusOr<bool> MaybeStartClusterWatch(
      const std::string& name, int depth, std::set<std::string>* clusters_seen,
      std::set<std::string>* eds_resources_seen);

  // Updates CDS and EDS watchers and DNS resolvers as needed.
  void MaybeUpdateClusterAndEndpointWatches();

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
  std::set<std::string> clusters_from_route_config_;

  // Cluster state.
  std::map<std::string, ClusterWatcherState> cluster_watchers_;

  // Endpoint state.
  std::map<std::string, EndpointWatcherState> endpoint_watchers_;
  std::map<std::string, DnsState> dns_resolvers_;
};

// A VirtualHost list iterator for RouteConfig resources.
class XdsVirtualHostListIterator : public XdsRouting::VirtualHostListIterator {
 public:
  explicit XdsVirtualHostListIterator(
      const std::vector<XdsRouteConfigResource::VirtualHost>* virtual_hosts)
      : virtual_hosts_(virtual_hosts) {}

  size_t Size() const override { return virtual_hosts_->size(); }

  const std::vector<std::string>& GetDomainsForVirtualHost(
      size_t index) const override {
    return (*virtual_hosts_)[index].domains;
  }

 private:
  const std::vector<XdsRouteConfigResource::VirtualHost>* virtual_hosts_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_XDS_XDS_CONFIG_H
