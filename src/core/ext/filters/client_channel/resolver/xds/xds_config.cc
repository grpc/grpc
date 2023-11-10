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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/resolver/xds/xds_config.h"

#include "absl/strings/str_join.h"

#include "src/core/ext/filters/client_channel/resolver/xds/xds_vhost_iterator.h"

namespace grpc_core {

//
// XdsConfigWatcher::ListenerWatcher
//

class XdsConfigWatcher::ListenerWatcher
    : public XdsListenerResourceType::WatcherInterface {
 public:
  explicit ListenerWatcher(RefCountedPtr<XdsConfigWatcher> config_watcher)
      : config_watcher_(std::move(config_watcher)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsListenerResource> listener) override {
    config_watcher_->work_serializer_->Run(
        [config_watcher = config_watcher_,
         listener = std::move(listener)]() mutable {
          config_watcher->OnListenerUpdate(std::move(listener));
        },
        DEBUG_LOCATION);
  }

  void OnError(absl::Status status) override {
    config_watcher_->work_serializer_->Run(
        [config_watcher = config_watcher_,
         status = std::move(status)]() mutable {
          config_watcher->OnError(config_watcher->listener_resource_name_,
                                  std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist() override {
    config_watcher_->work_serializer_->Run(
        [config_watcher = config_watcher_]() {
          config_watcher->OnResourceDoesNotExist(
              absl::StrCat(config_watcher->listner_resource_name_,
                           ": xDS listener resource does not exist"));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsConfigWatcher> config_watcher_;
};

//
// XdsConfigWatcher::RouteConfigWatcher
//

class XdsConfigWatcher::RouteConfigWatcher
    : public XdsRouteConfigResourceType::WatcherInterface {
 public:
  RouteConfigWatcher(RefCountedPtr<XdsConfigWatcher> config_watcher,
                     std::string name)
      : config_watcher_(std::move(config_watcher)), name_(std::move(name)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsRouteConfigResource> route_config) override {
    config_watcher_->work_serializer_->Run(
        [self = Ref(), route_config = std::move(route_config)]() mutable {
          self->config_watcher_->OnRouteConfigUpdate(self->name_,
                                                     std::move(route_config));
        },
        DEBUG_LOCATION);
  }

  void OnError(absl::Status status) override {
    config_watcher_->work_serializer_->Run(
        [self = Ref(), status = std::move(status)]() mutable {
          self->config_watcher_->OnError(self->name_, std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist() override {
    config_watcher_->work_serializer_->Run(
        [self = Ref()]() {
          self->config_watcher_->OnResourceDoesNotExist(
              absl::StrCat(self->name_,
                           ": xDS route config resource does not exist"));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsConfigWatcher> config_watcher_;
};

//
// XdsConfigWatcher::ClusterWatcher
//

class XdsConfigWatcher::ClusterWatcher
    : public XdsClusterResourceType::WatcherInterface {
 public:
  ClusterWatcher(RefCountedPtr<XdsConfigWatcher> config_watcher,
                 std::string name)
      : config_watcher_(std::move(config_watcher)), name_(std::move(name)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsClusterResource> cluster) override {
    config_watcher_->work_serializer_->Run(
        [self = Ref(), cluster = std::move(cluster)]() mutable {
          self->config_watcher_->OnClusterUpdate(self->name_,
                                                 std::move(cluster));
        },
        DEBUG_LOCATION);
  }

  void OnError(absl::Status status) override {
    config_watcher_->work_serializer_->Run(
        [self = Ref(), status = std::move(status)]() mutable {
          self->config_watcher_->OnError(self->name_, std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist() override {
// FIXME: do we want to put the entire channel in TF in this case, or
// only fail RPCs for this cluster?  probably the latter.
    config_watcher_->work_serializer_->Run(
        [self = Ref()]() {
          self->config_watcher_->OnResourceDoesNotExist(
              absl::StrCat(self->name_,
                           ": xDS cluster resource does not exist"));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsConfigWatcher> config_watcher_;
};

//
// XdsConfigWatcher::EndpointWatcher
//

class XdsConfigWatcher::EndpointWatcher
    : public XdsEndpointResourceType::WatcherInterface {
 public:
  EndpointWatcher(RefCountedPtr<XdsConfigWatcher> config_watcher,
                 std::string name)
      : config_watcher_(std::move(config_watcher)), name_(std::move(name)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsEndpointResource> endpoint) override {
    config_watcher_->work_serializer_->Run(
        [self = Ref(), endpoint = std::move(endpoint)]() mutable {
          self->config_watcher_->OnEndpointUpdate(self->name_,
                                                  std::move(endpoint));
        },
        DEBUG_LOCATION);
  }

  void OnError(absl::Status status) override {
    config_watcher_->work_serializer_->Run(
        [self = Ref(), status = std::move(status)]() mutable {
          self->config_watcher_->OnError(self->name_, std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist() override {
// FIXME: do we want to put the entire channel in TF in this case, or
// only fail RPCs for this cluster?  probably the latter.
    config_watcher_->work_serializer_->Run(
        [self = Ref()]() {
          self->config_watcher_->OnResourceDoesNotExist(
              absl::StrCat(self->name_,
                           ": xDS endpoint resource does not exist"));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsConfigWatcher> config_watcher_;
};

//
// XdsConfigWatcher
//

XdsConfigWatcher::XdsConfigWatcher(
    RefCountedPtr<GrpcXdsClient> xds_client,
    std::shared_ptr<WorkSerializer> work_serializer,
    std::unique_ptr<Watcher> watcher, std::string data_plane_authority,
    std::string listener_resource_name)
    : xds_client_(std::move(xds_client)),
      work_serializer_(std::move(work_serializer)),
      watcher_(std::move(watcher)),
      data_plane_authority_(std::move(data_plane_authority)) {
      listener_resource_name_(std::move(listener_resource_name)) {
  auto watcher = MakeRefCounted<ListenerWatcher>(Ref());
  listener_watcher_ = watcher.get();
  XdsListenerResourceType::StartWatch(
      xds_client_.get(), listener_resource_name_, std::move(watcher));
}

void XdsConfigWatcher::Orphan() {
  if (listener_watcher_ != nullptr) {
    XdsListenerResourceType::CancelWatch(
        xds_client_.get(), listener_resource_name_, listener_watcher_,
        /*delay_unsubscription=*/false);
  }
  if (route_config_watcher_ != nullptr) {
    XdsRouteConfigResourceType::CancelWatch(
        xds_client_.get(), route_config_name_, route_config_watcher_,
        /*delay_unsubscription=*/false);
  }
  for (const auto& p : cluster_watchers_) {
    XdsClusterResourceType::CancelWatch(
        xds_client_.get(), p.first, p.second.watcher,
        /*delay_unsubscription=*/false);
  }
  for (const auto& p : endpoint_watchers_) {
    XdsEndpointResourceType::CancelWatch(
        xds_client_.get(), p.first, p.second.watcher,
        /*delay_unsubscription=*/false);
  }
  xds_client_.reset();
  for (const auto& p : dns_resolvers_) {
    p.second.resolver.reset();
  }
  Unref();
}

void XdsConfigWatcher::OnListenerUpdate(
    std::shared_ptr<const XdsListenerResource> listener) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[xds_resolver %p] received updated listener data", this);
  }
  if (xds_client_ == nullptr) return;
  const auto* hcm = absl::get_if<XdsListenerResource::HttpConnectionManager>(
      &listener->listener);
  if (hcm == nullptr) {
    return OnError(listener_resource_name_,
                   absl::UnavailableError("not an API listener"));
  }
  current_listener_ = std::move(listener);
  Match(
      hcm->route_config,
      // RDS resource name
      [&](const std::string& rds_name) {
        // If the RDS name changed, update the RDS watcher.
        // Note that this will be true on the initial update, because
        // route_config_name_ will be empty.
        if (route_config_name_ != rds_name) {
          // If we already had a watch (i.e., if the previous config had
          // a different RDS name), stop the previous watch.
          // There will be no previous watch if either (a) this is the
          // initial resource update or (b) the previous Listener had an
          // inlined RouteConfig.
          if (route_config_watcher_ != nullptr) {
            XdsRouteConfigResourceType::CancelWatch(
                xds_client_.get(), route_config_name_, route_config_watcher_,
                /*delay_unsubscription=*/true);
            route_config_watcher_ = nullptr;
          }
          // Start watch for the new RDS resource name.
          route_config_name_ = rds_name;
          auto watcher =
              MakeRefCounted<RouteConfigWatcher>(Ref(), route_config_name_);
          route_config_watcher_ = watcher.get();
          XdsRouteConfigResourceType::StartWatch(
              xds_client_.get(), route_config_name_, std::move(watcher));
        } else {
          // RDS resource name has not changed, so no watch needs to be
          // updated, but we still need to propagate any changes in the
          // HCM config (e.g., the list of HTTP filters).
          MaybeUpdateClusterAndEndpointWatches();
          MaybeReportUpdate();
        }
      },
      // inlined RouteConfig
      [&](const std::shared_ptr<const XdsRouteConfigResource>& route_config) {
        // If the previous update specified an RDS resource instead of
        // having an inlined RouteConfig, we need to cancel the RDS watch.
        if (route_config_watcher_ != nullptr) {
          XdsRouteConfigResourceType::CancelWatch(
              xds_client_.get(), route_config_name_, route_config_watcher_);
          route_config_watcher_ = nullptr;
          route_config_name_.clear();
        }
        OnRouteConfigUpdate(route_config);
      });
}

void XdsConfigWatcher::OnRouteConfigUpdate(
    const std::string& name,
    std::shared_ptr<const XdsRouteConfigResource> route_config) {
  if (xds_client_ == nullptr) return;
  // Find the relevant VirtualHost from the RouteConfiguration.
  // If the resource doesn't have the right vhost, fail without updating
  // our data.
  auto vhost_index = XdsRouting::FindVirtualHostForDomain(
      XdsVirtualHostListIterator(&route_config->virtual_hosts),
      data_plane_authority_);
  if (!vhost_index.has_value()) {
    OnError(
        route_config_name_.empty()
            ? listener_resource_name_
            : route_config_name_,
        absl::UnavailableError(absl::StrCat("could not find VirtualHost for ",
                                            data_plane_authority_,
                                            " in RouteConfiguration")));
    return;
  }
  // Update our data.
  current_route_config_ = std::move(route_config);
  clusters_from_route_config_ =
      GetClustersFromRouteConfig(*current_route_config_);
  // The set of clusters we need may have changed.
  MaybeUpdateClusterAndEndpointWatches();
  MaybeReportUpdate();
}

void XdsConfigWatcher::OnClusterUpdate(
    const std::string& name,
    std::shared_ptr<const XdsClusterResource> cluster) {
  if (xds_client_ == nullptr) return;
  auto it = cluster_watchers_.find(name);
  if (it == cluster_watchers_.end()) return;
  it->second.update = std::move(cluster);
  // The set of clusters we need may have changed if this was an
  // aggregate cluster.
  MaybeUpdateClusterAndEndpointWatches();
  MaybeReportUpdate();
}

void XdsConfigWatcher::OnEndpointUpdate(
    const std::string& name,
    std::shared_ptr<const XdsEndpointResource> endpoint,
    std::string resolution_note) {
  if (xds_client_ == nullptr) return;
  auto it = endpoint_watchers_.find(name);
  if (it == endpoint_watchers_.end()) return;
  it->second.update = std::move(endpoint);
  it->second.resolution_note = std::move(resolution_note);
  MaybeReportUpdate();
}

void XdsConfigWatcher::OnDnsResult(
    const std::string& dns_name, EndpointAddressesList addresses,
    std::string resolution_note) {
  if (xds_client_ == nullptr) return;
  auto it = dns_resolvers_.find(dns_name);
  if (it == dns_resolvers_.end()) return;
  it->second.addresses = std::move(addresses);
  it->second.resolution_note = std::move(resolution_note);
  MaybeReportUpdate();
}

void XdsConfigWatcher::OnError(std::string context,
                               absl::Status status) {
  if (xds_client_ == nullptr) return;
  watcher_->OnError(context, std::move(status));
}

void XdsConfigWatcher::OnResourceDoesNotExist(std::string context) {
  if (xds_client_ == nullptr) return;
  watcher_->OnResourceDoesNotExist(std::move(context));
}

std::set<std::string> XdsConfigWatcher::GetClustersFromRouteConfig(
    const XdsRouteConfigResource& route_config) const {
  std::set<std::string> clusters;
  for (auto& route
       : current_route_config_->virtual_hosts[*vhost_index].routes) {
    auto* route_action =
        absl::get_if<XdsRouteConfigResource::Route::RouteAction>(
            &route.route.action);
    if (route_action == nullptr) continue;
    Match(
        route_action->action,
        // cluster name
        [&](const XdsRouteConfigResource::Route::RouteAction::ClusterName&
                cluster_name) {
          clusters.insert(cluster_name.cluster_name);
        },
        // WeightedClusters
        [&](const std::vector<
            XdsRouteConfigResource::Route::RouteAction::ClusterWeight>&
                weighted_clusters) {
          for (const auto& weighted_cluster : weighted_clusters) {
            clusters.insert(weighted_cluster.name);
          }
        },
        // ClusterSpecifierPlugin
        [&](const XdsRouteConfigResource::Route::RouteAction::
                ClusterSpecifierPluginName& cluster_specifier_plugin_name) {
// FIXME: plugin needs to expose a method to get us the list of possible clusters
        });
  }
  return clusters;
}

absl::Status XdsConfigWatcher::MaybeStartClusterWatch(
    const std::string& name, int depth, std::set<std::string>* clusters_seen,
    std::set<std::string>* eds_resources_seen) {
  if (depth == kMaxXdsAggregateClusterRecursionDepth) {
    return absl::FailedPreconditionError(
        "aggregate cluster graph exceeds max depth");
  }
  // Don't process the cluster again if we've already seen it in some
  // other branch of the recursion tree.
  if (!clusters_seen->insert(name).second) return absl::OkStatus();
  auto& state = cluster_watchers_[name];
  // Create a new watcher if needed.
  if (state.watcher == nullptr) {
    auto watcher = MakeRefCounted<ClusterWatcher>(Ref(), name);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] starting watch for cluster %s", this,
              name.c_str());
    }
    state.watcher = watcher.get();
    XdsClusterResourceType::StartWatch(xds_client_.get(), name,
                                       std::move(watcher));
    return absl::OkStatus();
  }
  // If we don't have the resource yet, stop here.
  if (state.update == nullptr) return absl::OkStatus();
  // Check cluster type.
  return Match(
      state.update->type,
      // EDS cluster.  Start EDS watch if needed.
      [&](const XdsClusterResource::Eds& eds) {
        std::string eds_resource_name =
            eds.eds_service_name.empty() ? name : eds.eds_service_name;
        eds_resources_seen->insert(eds_resource_name);
        auto& state = endpoint_watchers_[eds_resource_name];
        if (state.watcher == nullptr) {
          auto watcher =
              MakeRefCounted<EndpointWatcher>(Ref(), eds_resource_name);
          state.watcher = watcher.get();
          XdsEndpointResource::StartWatch(xds_client_.get(), eds_resource_name,
                                          std::move(watcher));
        }
        return absl::OkStatus();
      },
      // LOGICAL_DNS cluster.
      [&](const XdsClusterResource::LogicalDns& logical_dns) {
// FIXME
        return absl::OkStatus();
      },
      // Aggregate cluster.  Recursively expand to child clusters.
      [&](const XdsClusterResource::Aggregate& aggregate) {
        for (const std::string& child_name
             : aggregate.prioritized_cluster_names) {
          auto result = MaybeStartClusterWatch(
              child_name, depth + 1, clusters_seen, eds_resources_seen);
          if (!result.ok()) return result;
        }
        return absl::OkStatus();
      });
}

void XdsConfigWatcher::MaybeUpdateClusterAndEndpointWatches() {
  std::set<std::string> clusters_seen;
  std::set<std::string> eds_resources_seen;
  // Start all necessary cluster and endpoint watches.
  for (const std::string& cluster : clusters_from_route_config_) {
    auto result = MaybeStartClusterWatch(
        cluster, 0, &clusters_seen, &eds_resources_seen);
    if (!result.ok()) return OnError(cluster, result.status());
  }
  // Remove entries in cluster_watchers_ for any clusters not in clusters_seen.
  for (auto it = cluster_watchers_.begin(); it != cluster_watchers_.end();) {
    const std::string& cluster_name = it->first;
    if (clusters_seen.find(cluster_name) != clusters_seen.end()) {
      ++it;
      continue;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for cluster %s", this,
              cluster_name.c_str());
    }
    CancelClusterDataWatch(cluster_name, it->second.watcher,
                           /*delay_unsubscription=*/false);
    it = cluster_watchers_.erase(it);
  }
  // Remove entries in endpoint_watchers_ for any EDS resources not in
  // eds_resources_seen.
  for (auto it = endpoint_watchers_.begin(); it != endpoint_watchers_.end();) {
    const std::string& eds_resource_name = it->first;
    if (eds_resources_seen.find(eds_resource_name) !=
        eds_resources_seen.end()) {
      ++it;
      continue;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_cds_lb_trace)) {
      gpr_log(GPR_INFO, "[cdslb %p] cancelling watch for EDS resource %s", this,
              eds_resource_name.c_str());
    }
    CancelEndpointDataWatch(eds_resource_name, it->second.watcher,
                            /*delay_unsubscription=*/false);
    it = endpoint_watchers_.erase(it);
  }
}

void XdsConfigWatcher::MaybeReportUpdate() {
  if (current_listener_ == nullptr) return;
  if (current_route_config_ == nullptr) return;
  XdsConfig config;
  config.listener = current_listener_;
  config.route_config = current_route_config_;
  for (const auto& p : cluster_watchers_) {
    if (p.second.update == nullptr) return;
    config.clusters.emplace(p.first, p.second.update);
  }
  std::vector<absl::string_view> resolution_notes;
  for (const auto& p : endpoint_watchers_) {
    if (p.second.update == nullptr) return;
    config.endpoints.emplace(p.first, p.second.update);
    resolution_notes.push_back(p.second.resolution_note);
  }
  for (const auto& p : dns_resolvers_) {
    if (p.second.addresses == nullptr) return;
    config.dns_results.emplace(p.first, p.second.addresses);
    resolution_notes.push_back(p.second.resolution_note);
  }
  config.resolution_notes = absl::StrJoin(resolution_notes, "; ");
  watcher_->OnUpdate(std::move(config));
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONFIG_XDS_XDS_CONFIG_H
