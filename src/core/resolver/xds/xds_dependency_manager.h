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

#ifndef GRPC_SRC_CORE_RESOLVER_XDS_XDS_DEPENDENCY_MANAGER_H
#define GRPC_SRC_CORE_RESOLVER_XDS_XDS_DEPENDENCY_MANAGER_H

#include <grpc/support/port_platform.h>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_endpoint.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_route_config.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/resolver/resolver.h"

namespace grpc_core {

// Watches all xDS resources and handles dependencies between them.
// Reports updates only when all necessary resources have been obtained.
class XdsDependencyManager : public RefCounted<XdsDependencyManager>,
                             public Orphanable {
 public:
  struct XdsConfig : public RefCounted<XdsConfig> {
    // Listener resource.  Always non-null.
    std::shared_ptr<const XdsListenerResource> listener;
    // RouteConfig resource.  Will be populated even if RouteConfig is
    // inlined into the Listener resource.
    std::shared_ptr<const XdsRouteConfigResource> route_config;
    // Virtual host.  Points into route_config.  Will always be non-null.
    const XdsRouteConfigResource::VirtualHost* virtual_host;

    struct ClusterConfig {
      // Cluster resource.  Always non-null.
      std::shared_ptr<const XdsClusterResource> cluster;
      // Endpoint info for EDS and LOGICAL_DNS clusters.  If there was an
      // error, endpoints will be null and resolution_note will be set.
      struct EndpointConfig {
        std::shared_ptr<const XdsEndpointResource> endpoints;
        std::string resolution_note;

        EndpointConfig(std::shared_ptr<const XdsEndpointResource> endpoints,
                       std::string resolution_note)
            : endpoints(std::move(endpoints)),
              resolution_note(std::move(resolution_note)) {}
        bool operator==(const EndpointConfig& other) const {
          return endpoints == other.endpoints &&
                 resolution_note == other.resolution_note;
        }
      };
      // The list of leaf clusters for an aggregate cluster.
      struct AggregateConfig {
        std::vector<absl::string_view> leaf_clusters;

        explicit AggregateConfig(std::vector<absl::string_view> leaf_clusters)
            : leaf_clusters(std::move(leaf_clusters)) {}
        bool operator==(const AggregateConfig& other) const {
          return leaf_clusters == other.leaf_clusters;
        }
      };
      absl::variant<EndpointConfig, AggregateConfig> children;

      // Ctor for leaf clusters.
      ClusterConfig(std::shared_ptr<const XdsClusterResource> cluster,
                    std::shared_ptr<const XdsEndpointResource> endpoints,
                    std::string resolution_note);
      // Ctor for aggregate clusters.
      ClusterConfig(std::shared_ptr<const XdsClusterResource> cluster,
                    std::vector<absl::string_view> leaf_clusters);

      bool operator==(const ClusterConfig& other) const {
        return cluster == other.cluster && children == other.children;
      }
    };
    // Cluster map.  A cluster will have a non-OK status if either
    // (a) there was an error and we did not already have a valid
    // resource or (b) the resource does not exist.
    absl::flat_hash_map<std::string, absl::StatusOr<ClusterConfig>> clusters;

    std::string ToString() const;

    static absl::string_view ChannelArgName() {
      return GRPC_ARG_NO_SUBCHANNEL_PREFIX "xds_config";
    }
    static int ChannelArgsCompare(const XdsConfig* a, const XdsConfig* b) {
      return QsortCompare(a, b);
    }
    static constexpr bool ChannelArgUseConstPtr() { return true; }
  };

  class Watcher {
   public:
    virtual ~Watcher() = default;

    virtual void OnUpdate(RefCountedPtr<const XdsConfig> config) = 0;

    // These methods are invoked when there is an error or
    // does-not-exist on LDS or RDS only.
    virtual void OnError(absl::string_view context, absl::Status status) = 0;
    virtual void OnResourceDoesNotExist(std::string context) = 0;
  };

  class ClusterSubscription : public DualRefCounted<ClusterSubscription> {
   public:
    ClusterSubscription(absl::string_view cluster_name,
                        RefCountedPtr<XdsDependencyManager> dependency_mgr)
        : cluster_name_(cluster_name),
          dependency_mgr_(std::move(dependency_mgr)) {}

    void Orphan() override;

    absl::string_view cluster_name() const { return cluster_name_; }

   private:
    std::string cluster_name_;
    RefCountedPtr<XdsDependencyManager> dependency_mgr_;
  };

  XdsDependencyManager(RefCountedPtr<GrpcXdsClient> xds_client,
                       std::shared_ptr<WorkSerializer> work_serializer,
                       std::unique_ptr<Watcher> watcher,
                       std::string data_plane_authority,
                       std::string listener_resource_name, ChannelArgs args,
                       grpc_pollset_set* interested_parties);

  void Orphan() override;

  // Gets an external cluster subscription.  This allows us to include
  // clusters in the config that are referenced by something other than
  // the route config (e.g., RLS).  The cluster will be included in the
  // config as long as the returned object is still referenced.
  RefCountedPtr<ClusterSubscription> GetClusterSubscription(
      absl::string_view cluster_name);

  static absl::string_view ChannelArgName() {
    return GRPC_ARG_NO_SUBCHANNEL_PREFIX "xds_dependency_manager";
  }
  static int ChannelArgsCompare(const XdsDependencyManager* a,
                                const XdsDependencyManager* b) {
    return QsortCompare(a, b);
  }

 private:
  class ListenerWatcher;
  class RouteConfigWatcher;
  class ClusterWatcher;
  class EndpointWatcher;

  class DnsResultHandler;

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
  void PopulateDnsUpdate(const std::string& dns_name, Resolver::Result result,
                         DnsState* dns_state);

  // Starts CDS and EDS/DNS watches for the specified cluster if needed.
  // Adds an entry to cluster_config_map, which will contain the cluster
  // data if the data is available.
  // For each EDS cluster, adds the EDS resource to eds_resources_seen.
  // For each Logical DNS cluster, adds the DNS hostname to dns_names_seen.
  // For aggregate clusters, calls itself recursively.  If leaf_clusters is
  // non-null, populates it with a list of leaf clusters, or an error if
  // max depth is exceeded.
  // Returns true if all resources have been obtained.
  bool PopulateClusterConfigMap(
      absl::string_view name, int depth,
      absl::flat_hash_map<std::string,
                          absl::StatusOr<XdsConfig::ClusterConfig>>*
          cluster_config_map,
      std::set<absl::string_view>* eds_resources_seen,
      std::set<absl::string_view>* dns_names_seen,
      absl::StatusOr<std::vector<absl::string_view>>* leaf_clusters = nullptr);

  // Called when an external cluster subscription is unreffed.
  void OnClusterSubscriptionUnref(absl::string_view cluster_name,
                                  ClusterSubscription* subscription);

  // Checks whether all necessary resources have been obtained, and if
  // so reports an update to the watcher.
  void MaybeReportUpdate();

  // Parameters passed into ctor.
  RefCountedPtr<GrpcXdsClient> xds_client_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::unique_ptr<Watcher> watcher_;
  const std::string data_plane_authority_;
  const std::string listener_resource_name_;
  ChannelArgs args_;
  grpc_pollset_set* interested_parties_;

  // Listener state.
  ListenerWatcher* listener_watcher_ = nullptr;
  std::shared_ptr<const XdsListenerResource> current_listener_;
  std::string route_config_name_;

  // RouteConfig state.
  RouteConfigWatcher* route_config_watcher_ = nullptr;
  std::shared_ptr<const XdsRouteConfigResource> current_route_config_;
  const XdsRouteConfigResource::VirtualHost* current_virtual_host_ = nullptr;
  absl::flat_hash_set<absl::string_view> clusters_from_route_config_;

  // Cluster state.
  absl::flat_hash_map<std::string, ClusterWatcherState> cluster_watchers_;
  absl::flat_hash_map<absl::string_view, WeakRefCountedPtr<ClusterSubscription>>
      cluster_subscriptions_;

  // Endpoint state.
  absl::flat_hash_map<std::string, EndpointWatcherState> endpoint_watchers_;
  absl::flat_hash_map<std::string, DnsState> dns_resolvers_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_RESOLVER_XDS_XDS_DEPENDENCY_MANAGER_H
