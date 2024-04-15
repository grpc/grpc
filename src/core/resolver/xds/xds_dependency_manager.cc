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

#include "src/core/resolver/xds/xds_dependency_manager.h"

#include <set>

#include "absl/strings/str_join.h"

#include "src/core/ext/xds/xds_routing.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/load_balancing/xds/xds_channel_args.h"
#include "src/core/resolver/fake/fake_resolver.h"
#include "src/core/resolver/xds/xds_resolver_trace.h"

namespace grpc_core {

namespace {

// Max depth of aggregate cluster dependency graph.
constexpr int kMaxXdsAggregateClusterRecursionDepth = 16;

}  // namespace

//
// XdsDependencyManager::XdsConfig::ClusterConfig
//

XdsDependencyManager::XdsConfig::ClusterConfig::ClusterConfig(
    std::shared_ptr<const XdsClusterResource> cluster,
    std::shared_ptr<const XdsEndpointResource> endpoints,
    std::string resolution_note)
    : cluster(std::move(cluster)),
      children(absl::in_place_type_t<EndpointConfig>(), std::move(endpoints),
               std::move(resolution_note)) {}

XdsDependencyManager::XdsConfig::ClusterConfig::ClusterConfig(
    std::shared_ptr<const XdsClusterResource> cluster,
    std::vector<absl::string_view> leaf_clusters)
    : cluster(std::move(cluster)),
      children(absl::in_place_type_t<AggregateConfig>(),
               std::move(leaf_clusters)) {}

//
// XdsDependencyManager::XdsConfig
//

std::string XdsDependencyManager::XdsConfig::ToString() const {
  std::vector<std::string> parts = {
      "{\n  listener: {",     listener->ToString(),
      "}\n  route_config: {", route_config->ToString(),
      "}\n  virtual_host: {", virtual_host->ToString(),
      "}\n  clusters: {\n"};
  for (const auto& p : clusters) {
    parts.push_back(absl::StrCat("    \"", p.first, "\": "));
    if (!p.second.ok()) {
      parts.push_back(p.second.status().ToString());
      parts.push_back("\n");
    } else {
      parts.push_back(
          absl::StrCat("      {\n"
                       "        cluster: {",
                       p.second->cluster->ToString(), "}\n"));
      Match(
          p.second->children,
          [&](const ClusterConfig::EndpointConfig& endpoint_config) {
            parts.push_back(
                absl::StrCat("        endpoints: {",
                             endpoint_config.endpoints == nullptr
                                 ? "<null>"
                                 : endpoint_config.endpoints->ToString(),
                             "}\n"
                             "        resolution_note: \"",
                             endpoint_config.resolution_note, "\"\n"));
          },
          [&](const ClusterConfig::AggregateConfig& aggregate_config) {
            parts.push_back(absl::StrCat(
                "        leaf_clusters: [",
                absl::StrJoin(aggregate_config.leaf_clusters, ", "), "]\n"));
          });
      parts.push_back(
          "      }\n"
          "    ]\n");
    }
  }
  parts.push_back("  }\n}");
  return absl::StrJoin(parts, "");
}

//
// XdsDependencyManager::ListenerWatcher
//

class XdsDependencyManager::ListenerWatcher final
    : public XdsListenerResourceType::WatcherInterface {
 public:
  explicit ListenerWatcher(RefCountedPtr<XdsDependencyManager> dependency_mgr)
      : dependency_mgr_(std::move(dependency_mgr)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsListenerResource> listener,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [dependency_mgr = dependency_mgr_, listener = std::move(listener),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          dependency_mgr->OnListenerUpdate(std::move(listener));
        },
        DEBUG_LOCATION);
  }

  void OnError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [dependency_mgr = dependency_mgr_, status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          dependency_mgr->OnError(dependency_mgr->listener_resource_name_,
                                  std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist(
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [dependency_mgr = dependency_mgr_,
         read_delay_handle = std::move(read_delay_handle)]() {
          dependency_mgr->OnResourceDoesNotExist(
              absl::StrCat(dependency_mgr->listener_resource_name_,
                           ": xDS listener resource does not exist"));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsDependencyManager> dependency_mgr_;
};

//
// XdsDependencyManager::RouteConfigWatcher
//

class XdsDependencyManager::RouteConfigWatcher final
    : public XdsRouteConfigResourceType::WatcherInterface {
 public:
  RouteConfigWatcher(RefCountedPtr<XdsDependencyManager> dependency_mgr,
                     std::string name)
      : dependency_mgr_(std::move(dependency_mgr)), name_(std::move(name)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsRouteConfigResource> route_config,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<RouteConfigWatcher>(),
         route_config = std::move(route_config),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnRouteConfigUpdate(self->name_,
                                                     std::move(route_config));
        },
        DEBUG_LOCATION);
  }

  void OnError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<RouteConfigWatcher>(), status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnError(self->name_, std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist(
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<RouteConfigWatcher>(),
         read_delay_handle = std::move(read_delay_handle)]() {
          self->dependency_mgr_->OnResourceDoesNotExist(absl::StrCat(
              self->name_,
              ": xDS route configuration resource does not exist"));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsDependencyManager> dependency_mgr_;
  std::string name_;
};

//
// XdsDependencyManager::ClusterWatcher
//

class XdsDependencyManager::ClusterWatcher final
    : public XdsClusterResourceType::WatcherInterface {
 public:
  ClusterWatcher(RefCountedPtr<XdsDependencyManager> dependency_mgr,
                 absl::string_view name)
      : dependency_mgr_(std::move(dependency_mgr)), name_(name) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsClusterResource> cluster,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<ClusterWatcher>(), cluster = std::move(cluster),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnClusterUpdate(self->name_,
                                                 std::move(cluster));
        },
        DEBUG_LOCATION);
  }

  void OnError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<ClusterWatcher>(), status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnClusterError(self->name_, std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist(
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<ClusterWatcher>(),
         read_delay_handle = std::move(read_delay_handle)]() {
          self->dependency_mgr_->OnClusterDoesNotExist(self->name_);
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsDependencyManager> dependency_mgr_;
  std::string name_;
};

//
// XdsDependencyManager::EndpointWatcher
//

class XdsDependencyManager::EndpointWatcher final
    : public XdsEndpointResourceType::WatcherInterface {
 public:
  EndpointWatcher(RefCountedPtr<XdsDependencyManager> dependency_mgr,
                  absl::string_view name)
      : dependency_mgr_(std::move(dependency_mgr)), name_(name) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsEndpointResource> endpoint,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<EndpointWatcher>(),
         endpoint = std::move(endpoint),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnEndpointUpdate(self->name_,
                                                  std::move(endpoint));
        },
        DEBUG_LOCATION);
  }

  void OnError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<EndpointWatcher>(), status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnEndpointError(self->name_,
                                                 std::move(status));
        },
        DEBUG_LOCATION);
  }

  void OnResourceDoesNotExist(
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<EndpointWatcher>(),
         read_delay_handle = std::move(read_delay_handle)]() {
          self->dependency_mgr_->OnEndpointDoesNotExist(self->name_);
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsDependencyManager> dependency_mgr_;
  std::string name_;
};

//
// XdsDependencyManager::DnsResultHandler
//

class XdsDependencyManager::DnsResultHandler final
    : public Resolver::ResultHandler {
 public:
  DnsResultHandler(RefCountedPtr<XdsDependencyManager> dependency_mgr,
                   std::string name)
      : dependency_mgr_(std::move(dependency_mgr)), name_(std::move(name)) {}

  void ReportResult(Resolver::Result result) override {
    dependency_mgr_->work_serializer_->Run(
        [dependency_mgr = dependency_mgr_, name = name_,
         result = std::move(result)]() mutable {
          dependency_mgr->OnDnsResult(name, std::move(result));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<XdsDependencyManager> dependency_mgr_;
  std::string name_;
};

//
// XdsDependencyManager::ClusterSubscription
//

void XdsDependencyManager::ClusterSubscription::Orphaned() {
  dependency_mgr_->work_serializer_->Run(
      [self = WeakRef()]() {
        self->dependency_mgr_->OnClusterSubscriptionUnref(self->cluster_name_,
                                                          self.get());
      },
      DEBUG_LOCATION);
}

//
// XdsDependencyManager
//

XdsDependencyManager::XdsDependencyManager(
    RefCountedPtr<GrpcXdsClient> xds_client,
    std::shared_ptr<WorkSerializer> work_serializer,
    std::unique_ptr<Watcher> watcher, std::string data_plane_authority,
    std::string listener_resource_name, ChannelArgs args,
    grpc_pollset_set* interested_parties)
    : xds_client_(std::move(xds_client)),
      work_serializer_(std::move(work_serializer)),
      watcher_(std::move(watcher)),
      data_plane_authority_(std::move(data_plane_authority)),
      listener_resource_name_(std::move(listener_resource_name)),
      args_(std::move(args)),
      interested_parties_(interested_parties) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[XdsDependencyManager %p] starting watch for listener %s", this,
            listener_resource_name_.c_str());
  }
  auto listener_watcher = MakeRefCounted<ListenerWatcher>(Ref());
  listener_watcher_ = listener_watcher.get();
  XdsListenerResourceType::StartWatch(
      xds_client_.get(), listener_resource_name_, std::move(listener_watcher));
}

void XdsDependencyManager::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] shutting down", this);
  }
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
    XdsClusterResourceType::CancelWatch(xds_client_.get(), p.first,
                                        p.second.watcher,
                                        /*delay_unsubscription=*/false);
  }
  for (const auto& p : endpoint_watchers_) {
    XdsEndpointResourceType::CancelWatch(xds_client_.get(), p.first,
                                         p.second.watcher,
                                         /*delay_unsubscription=*/false);
  }
  cluster_subscriptions_.clear();
  xds_client_.reset();
  for (auto& p : dns_resolvers_) {
    p.second.resolver.reset();
  }
  Unref();
}

void XdsDependencyManager::OnListenerUpdate(
    std::shared_ptr<const XdsListenerResource> listener) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] received Listener update",
            this);
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
          if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
            gpr_log(
                GPR_INFO,
                "[XdsDependencyManager %p] starting watch for route config %s",
                this, route_config_name_.c_str());
          }
          auto watcher =
              MakeRefCounted<RouteConfigWatcher>(Ref(), route_config_name_);
          route_config_watcher_ = watcher.get();
          XdsRouteConfigResourceType::StartWatch(
              xds_client_.get(), route_config_name_, std::move(watcher));
        } else {
          // RDS resource name has not changed, so no watch needs to be
          // updated, but we still need to propagate any changes in the
          // HCM config (e.g., the list of HTTP filters).
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
        OnRouteConfigUpdate("", route_config);
      });
}

namespace {

class XdsVirtualHostListIterator final
    : public XdsRouting::VirtualHostListIterator {
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

// Gets the set of clusters referenced in the specified virtual host.
absl::flat_hash_set<absl::string_view> GetClustersFromVirtualHost(
    const XdsRouteConfigResource::VirtualHost& virtual_host) {
  absl::flat_hash_set<absl::string_view> clusters;
  for (auto& route : virtual_host.routes) {
    auto* route_action =
        absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
    if (route_action == nullptr) continue;
    Match(
        route_action->action,
        // cluster name
        [&](const XdsRouteConfigResource::Route::RouteAction::ClusterName&
                cluster_name) { clusters.insert(cluster_name.cluster_name); },
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
                ClusterSpecifierPluginName&) {
          // Clusters are determined dynamically in this case, so we
          // can't add any clusters here.
        });
  }
  return clusters;
}

}  // namespace

void XdsDependencyManager::OnRouteConfigUpdate(
    const std::string& name,
    std::shared_ptr<const XdsRouteConfigResource> route_config) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[XdsDependencyManager %p] received RouteConfig update for %s",
            this, name.empty() ? "<inline>" : name.c_str());
  }
  if (xds_client_ == nullptr) return;
  // Ignore updates for stale names.
  if (name.empty()) {
    if (!route_config_name_.empty()) return;
  } else {
    if (name != route_config_name_) return;
  }
  // Find the relevant VirtualHost from the RouteConfiguration.
  // If the resource doesn't have the right vhost, fail without updating
  // our data.
  auto vhost_index = XdsRouting::FindVirtualHostForDomain(
      XdsVirtualHostListIterator(&route_config->virtual_hosts),
      data_plane_authority_);
  if (!vhost_index.has_value()) {
    OnError(route_config_name_.empty() ? listener_resource_name_
                                       : route_config_name_,
            absl::UnavailableError(
                absl::StrCat("could not find VirtualHost for ",
                             data_plane_authority_, " in RouteConfiguration")));
    return;
  }
  // Update our data.
  current_route_config_ = std::move(route_config);
  current_virtual_host_ = &current_route_config_->virtual_hosts[*vhost_index];
  clusters_from_route_config_ =
      GetClustersFromVirtualHost(*current_virtual_host_);
  MaybeReportUpdate();
}

void XdsDependencyManager::OnError(std::string context, absl::Status status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[XdsDependencyManager %p] received Listener or RouteConfig "
            "error: %s %s",
            this, context.c_str(), status.ToString().c_str());
  }
  if (xds_client_ == nullptr) return;
  if (current_virtual_host_ != nullptr) return;
  watcher_->OnError(context, std::move(status));
}

void XdsDependencyManager::OnResourceDoesNotExist(std::string context) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] %s", this, context.c_str());
  }
  if (xds_client_ == nullptr) return;
  current_virtual_host_ = nullptr;
  watcher_->OnResourceDoesNotExist(std::move(context));
}

void XdsDependencyManager::OnClusterUpdate(
    const std::string& name,
    std::shared_ptr<const XdsClusterResource> cluster) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] received Cluster update: %s",
            this, name.c_str());
  }
  if (xds_client_ == nullptr) return;
  auto it = cluster_watchers_.find(name);
  if (it == cluster_watchers_.end()) return;
  it->second.update = std::move(cluster);
  MaybeReportUpdate();
}

void XdsDependencyManager::OnClusterError(const std::string& name,
                                          absl::Status status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] received Cluster error: %s %s",
            this, name.c_str(), status.ToString().c_str());
  }
  if (xds_client_ == nullptr) return;
  auto it = cluster_watchers_.find(name);
  if (it == cluster_watchers_.end()) return;
  if (it->second.update.value_or(nullptr) == nullptr) {
    it->second.update =
        absl::Status(status.code(), absl::StrCat(name, ": ", status.message()));
  }
  MaybeReportUpdate();
}

void XdsDependencyManager::OnClusterDoesNotExist(const std::string& name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] Cluster does not exist: %s",
            this, name.c_str());
  }
  if (xds_client_ == nullptr) return;
  auto it = cluster_watchers_.find(name);
  if (it == cluster_watchers_.end()) return;
  it->second.update = absl::UnavailableError(
      absl::StrCat("CDS resource ", name, " does not exist"));
  MaybeReportUpdate();
}

void XdsDependencyManager::OnEndpointUpdate(
    const std::string& name,
    std::shared_ptr<const XdsEndpointResource> endpoint) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] received Endpoint update: %s",
            this, name.c_str());
  }
  if (xds_client_ == nullptr) return;
  auto it = endpoint_watchers_.find(name);
  if (it == endpoint_watchers_.end()) return;
  if (endpoint->priorities.empty()) {
    it->second.update.resolution_note =
        absl::StrCat("EDS resource ", name, " contains no localities");
  } else {
    std::set<absl::string_view> empty_localities;
    for (const auto& priority : endpoint->priorities) {
      for (const auto& p : priority.localities) {
        if (p.second.endpoints.empty()) {
          empty_localities.insert(
              p.first->human_readable_string().as_string_view());
        }
      }
    }
    if (!empty_localities.empty()) {
      it->second.update.resolution_note =
          absl::StrCat("EDS resource ", name, " contains empty localities: [",
                       absl::StrJoin(empty_localities, "; "), "]");
    }
  }
  it->second.update.endpoints = std::move(endpoint);
  MaybeReportUpdate();
}

void XdsDependencyManager::OnEndpointError(const std::string& name,
                                           absl::Status status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO,
            "[XdsDependencyManager %p] received Endpoint error: %s %s", this,
            name.c_str(), status.ToString().c_str());
  }
  if (xds_client_ == nullptr) return;
  auto it = endpoint_watchers_.find(name);
  if (it == endpoint_watchers_.end()) return;
  if (it->second.update.endpoints == nullptr) {
    it->second.update.resolution_note =
        absl::StrCat("EDS resource ", name, ": ", status.ToString());
    MaybeReportUpdate();
  }
}

void XdsDependencyManager::OnEndpointDoesNotExist(const std::string& name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] Endpoint does not exist: %s",
            this, name.c_str());
  }
  if (xds_client_ == nullptr) return;
  auto it = endpoint_watchers_.find(name);
  if (it == endpoint_watchers_.end()) return;
  it->second.update.endpoints.reset();
  it->second.update.resolution_note =
      absl::StrCat("EDS resource ", name, " does not exist");
  MaybeReportUpdate();
}

void XdsDependencyManager::OnDnsResult(const std::string& dns_name,
                                       Resolver::Result result) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] received DNS update: %s", this,
            dns_name.c_str());
  }
  if (xds_client_ == nullptr) return;
  auto it = dns_resolvers_.find(dns_name);
  if (it == dns_resolvers_.end()) return;
  PopulateDnsUpdate(dns_name, std::move(result), &it->second);
  MaybeReportUpdate();
}

void XdsDependencyManager::PopulateDnsUpdate(const std::string& dns_name,
                                             Resolver::Result result,
                                             DnsState* dns_state) {
  // Convert resolver result to EDS update.
  XdsEndpointResource::Priority::Locality locality;
  locality.name = MakeRefCounted<XdsLocalityName>("", "", "");
  locality.lb_weight = 1;
  if (result.addresses.ok()) {
    locality.endpoints = std::move(*result.addresses);
    dns_state->update.resolution_note = std::move(result.resolution_note);
  } else if (result.resolution_note.empty()) {
    dns_state->update.resolution_note =
        absl::StrCat("DNS resolution failed for ", dns_name, ": ",
                     result.addresses.status().ToString());
  }
  XdsEndpointResource::Priority priority;
  priority.localities.emplace(locality.name.get(), std::move(locality));
  auto resource = std::make_shared<XdsEndpointResource>();
  resource->priorities.emplace_back(std::move(priority));
  dns_state->update.endpoints = std::move(resource);
}

bool XdsDependencyManager::PopulateClusterConfigMap(
    absl::string_view name, int depth,
    absl::flat_hash_map<std::string, absl::StatusOr<XdsConfig::ClusterConfig>>*
        cluster_config_map,
    std::set<absl::string_view>* eds_resources_seen,
    std::set<absl::string_view>* dns_names_seen,
    absl::StatusOr<std::vector<absl::string_view>>* leaf_clusters) {
  if (depth > 0) GPR_ASSERT(leaf_clusters != nullptr);
  if (depth == kMaxXdsAggregateClusterRecursionDepth) {
    *leaf_clusters =
        absl::UnavailableError("aggregate cluster graph exceeds max depth");
    return true;
  }
  // Don't process the cluster again if we've already seen it in some
  // other branch of the recursion tree.  We populate it with a non-OK
  // status here, since we need an entry in the map to avoid incorrectly
  // stopping the CDS watch, but we'll overwrite this below if we actually
  // have the data for the cluster.
  auto p = cluster_config_map->emplace(
      name, absl::InternalError("cluster data not yet available"));
  if (!p.second) return true;
  auto& cluster_config = p.first->second;
  auto& state = cluster_watchers_[name];
  // Create a new watcher if needed.
  if (state.watcher == nullptr) {
    auto watcher = MakeRefCounted<ClusterWatcher>(Ref(), name);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO,
              "[XdsDependencyManager %p] starting watch for cluster %s", this,
              std::string(name).c_str());
    }
    state.watcher = watcher.get();
    XdsClusterResourceType::StartWatch(xds_client_.get(), name,
                                       std::move(watcher));
    return false;
  }
  // If there was an error fetching the CDS resource, report the error.
  if (!state.update.ok()) {
    cluster_config = state.update.status();
    return true;
  }
  // If we don't have the resource yet, we can't return a config yet.
  if (*state.update == nullptr) return false;
  // Populate endpoint info based on cluster type.
  return Match(
      (*state.update)->type,
      // EDS cluster.
      [&](const XdsClusterResource::Eds& eds) {
        absl::string_view eds_resource_name =
            eds.eds_service_name.empty() ? name : eds.eds_service_name;
        eds_resources_seen->insert(eds_resource_name);
        // Start EDS watch if needed.
        auto& eds_state = endpoint_watchers_[eds_resource_name];
        if (eds_state.watcher == nullptr) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
            gpr_log(GPR_INFO,
                    "[XdsDependencyManager %p] starting watch for endpoint %s",
                    this, std::string(eds_resource_name).c_str());
          }
          auto watcher =
              MakeRefCounted<EndpointWatcher>(Ref(), eds_resource_name);
          eds_state.watcher = watcher.get();
          XdsEndpointResourceType::StartWatch(
              xds_client_.get(), eds_resource_name, std::move(watcher));
          return false;
        }
        // Check if EDS resource has been returned.
        if (eds_state.update.endpoints == nullptr &&
            eds_state.update.resolution_note.empty()) {
          return false;
        }
        // Populate cluster config.
        cluster_config.emplace(*state.update, eds_state.update.endpoints,
                               eds_state.update.resolution_note);
        if (leaf_clusters != nullptr) (*leaf_clusters)->push_back(name);
        return true;
      },
      // LOGICAL_DNS cluster.
      [&](const XdsClusterResource::LogicalDns& logical_dns) {
        dns_names_seen->insert(logical_dns.hostname);
        // Start DNS resolver if needed.
        auto& dns_state = dns_resolvers_[logical_dns.hostname];
        if (dns_state.resolver == nullptr) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
            gpr_log(GPR_INFO,
                    "[XdsDependencyManager %p] starting DNS resolver for %s",
                    this, logical_dns.hostname.c_str());
          }
          auto* fake_resolver_response_generator = args_.GetPointer<
              FakeResolverResponseGenerator>(
              GRPC_ARG_XDS_LOGICAL_DNS_CLUSTER_FAKE_RESOLVER_RESPONSE_GENERATOR);
          ChannelArgs args = args_;
          std::string target;
          if (fake_resolver_response_generator != nullptr) {
            target = absl::StrCat("fake:", logical_dns.hostname);
            args = args.SetObject(fake_resolver_response_generator->Ref());
          } else {
            target = absl::StrCat("dns:", logical_dns.hostname);
          }
          dns_state.resolver =
              CoreConfiguration::Get().resolver_registry().CreateResolver(
                  target, args, interested_parties_, work_serializer_,
                  std::make_unique<DnsResultHandler>(Ref(),
                                                     logical_dns.hostname));
          if (dns_state.resolver == nullptr) {
            Resolver::Result result;
            result.addresses.emplace();  // Empty list.
            result.resolution_note = absl::StrCat(
                "failed to create DNS resolver for ", logical_dns.hostname);
            PopulateDnsUpdate(logical_dns.hostname, std::move(result),
                              &dns_state);
          } else {
            dns_state.resolver->StartLocked();
            return false;
          }
        }
        // Check if result has been returned.
        if (dns_state.update.endpoints == nullptr &&
            dns_state.update.resolution_note.empty()) {
          return false;
        }
        // Populate cluster config.
        cluster_config.emplace(*state.update, dns_state.update.endpoints,
                               dns_state.update.resolution_note);
        if (leaf_clusters != nullptr) (*leaf_clusters)->push_back(name);
        return true;
      },
      // Aggregate cluster.  Recursively expand to child clusters.
      [&](const XdsClusterResource::Aggregate& aggregate) {
        // Grab a ref to the CDS resource for the aggregate cluster here,
        // since our reference into cluster_watchers_ will be invalidated
        // when we recursively call ourselves and add entries to the
        // map for underlying clusters.
        auto cluster_resource = *state.update;
        // Recursively expand leaf clusters.
        absl::StatusOr<std::vector<absl::string_view>> child_leaf_clusters;
        child_leaf_clusters.emplace();
        bool have_all_resources = true;
        for (const std::string& child_name :
             aggregate.prioritized_cluster_names) {
          have_all_resources &= PopulateClusterConfigMap(
              child_name, depth + 1, cluster_config_map, eds_resources_seen,
              dns_names_seen, &child_leaf_clusters);
          if (!child_leaf_clusters.ok()) break;
        }
        // Note that we cannot use the cluster_config reference we
        // created above, because it may have been invalidated by map
        // insertions when we recursively called ourselves, so we have
        // to do the lookup in cluster_config_map again.
        auto& aggregate_cluster_config = (*cluster_config_map)[name];
        // If we exceeded max recursion depth, report an error for the
        // cluster, and propagate the error up if needed.
        if (!child_leaf_clusters.ok()) {
          aggregate_cluster_config = child_leaf_clusters.status();
          if (leaf_clusters != nullptr) {
            *leaf_clusters = child_leaf_clusters.status();
          }
          return true;
        }
        // If needed, propagate leaf cluster list up the tree.
        if (leaf_clusters != nullptr) {
          (*leaf_clusters)
              ->insert((*leaf_clusters)->end(), child_leaf_clusters->begin(),
                       child_leaf_clusters->end());
        }
        // If there are no leaf clusters, report an error for the cluster.
        if (have_all_resources && child_leaf_clusters->empty()) {
          aggregate_cluster_config = absl::UnavailableError(
              absl::StrCat("aggregate cluster dependency graph for ", name,
                           " has no leaf clusters"));
          return true;
        }
        // Populate cluster config.
        // Note that we do this even for aggregate clusters that are not
        // at the root of the tree, because we need to make sure the list
        // of underlying cluster names stays alive so that the leaf cluster
        // list of the root aggregate cluster can point to those strings.
        aggregate_cluster_config.emplace(std::move(cluster_resource),
                                         std::move(*child_leaf_clusters));
        return have_all_resources;
      });
}

RefCountedPtr<XdsDependencyManager::ClusterSubscription>
XdsDependencyManager::GetClusterSubscription(absl::string_view cluster_name) {
  auto it = cluster_subscriptions_.find(cluster_name);
  if (it != cluster_subscriptions_.end()) {
    auto subscription = it->second->RefIfNonZero();
    if (subscription != nullptr) return subscription;
  }
  auto subscription = MakeRefCounted<ClusterSubscription>(cluster_name, Ref());
  cluster_subscriptions_.emplace(subscription->cluster_name(),
                                 subscription->WeakRef());
  // If the cluster is not already subscribed to by virtue of being
  // referenced in the route config, then trigger the CDS watch.
  if (!clusters_from_route_config_.contains(cluster_name)) {
    MaybeReportUpdate();
  }
  return subscription;
}

void XdsDependencyManager::OnClusterSubscriptionUnref(
    absl::string_view cluster_name, ClusterSubscription* subscription) {
  auto it = cluster_subscriptions_.find(cluster_name);
  // Shouldn't happen, but ignore if it does.
  if (it == cluster_subscriptions_.end()) return;
  // Do nothing if the subscription has already been replaced.
  if (it->second != subscription) return;
  // Remove the entry.
  cluster_subscriptions_.erase(it);
  // If this cluster is not already subscribed to by virtue of being
  // referenced in the route config, then update watches and generate a
  // new update.
  if (!clusters_from_route_config_.contains(cluster_name)) {
    MaybeReportUpdate();
  }
}

void XdsDependencyManager::MaybeReportUpdate() {
  // Populate Listener and RouteConfig fields.
  if (current_virtual_host_ == nullptr) return;
  auto config = MakeRefCounted<XdsConfig>();
  config->listener = current_listener_;
  config->route_config = current_route_config_;
  config->virtual_host = current_virtual_host_;
  // Determine the set of clusters we should be watching.
  std::set<absl::string_view> clusters_to_watch;
  for (const absl::string_view& cluster : clusters_from_route_config_) {
    clusters_to_watch.insert(cluster);
  }
  for (const auto& p : cluster_subscriptions_) {
    clusters_to_watch.insert(p.first);
  }
  // Populate Cluster map.
  // We traverse the entire graph even if we don't yet have all of the
  // resources we need to ensure that the right set of watches are active.
  std::set<absl::string_view> eds_resources_seen;
  std::set<absl::string_view> dns_names_seen;
  bool have_all_resources = true;
  for (const absl::string_view& cluster : clusters_to_watch) {
    have_all_resources &= PopulateClusterConfigMap(
        cluster, 0, &config->clusters, &eds_resources_seen, &dns_names_seen);
  }
  // Remove entries in cluster_watchers_ for any clusters not in
  // config->clusters.
  for (auto it = cluster_watchers_.begin(); it != cluster_watchers_.end();) {
    const std::string& cluster_name = it->first;
    if (config->clusters.contains(cluster_name)) {
      ++it;
      continue;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO,
              "[XdsDependencyManager %p] cancelling watch for cluster %s", this,
              cluster_name.c_str());
    }
    XdsClusterResourceType::CancelWatch(xds_client_.get(), cluster_name,
                                        it->second.watcher,
                                        /*delay_unsubscription=*/false);
    cluster_watchers_.erase(it++);
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO,
              "[XdsDependencyManager %p] cancelling watch for EDS resource %s",
              this, eds_resource_name.c_str());
    }
    XdsEndpointResourceType::CancelWatch(xds_client_.get(), eds_resource_name,
                                         it->second.watcher,
                                         /*delay_unsubscription=*/false);
    endpoint_watchers_.erase(it++);
  }
  // Remove entries in dns_resolvers_ for any DNS name not in
  // eds_resources_seen.
  for (auto it = dns_resolvers_.begin(); it != dns_resolvers_.end();) {
    const std::string& dns_name = it->first;
    if (dns_names_seen.find(dns_name) != dns_names_seen.end()) {
      ++it;
      continue;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO,
              "[XdsDependencyManager %p] shutting down DNS resolver for %s",
              this, dns_name.c_str());
    }
    dns_resolvers_.erase(it++);
  }
  // If we have all the data we need, then send an update.
  if (!have_all_resources) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
      gpr_log(GPR_INFO,
              "[XdsDependencyManager %p] missing data -- NOT returning config",
              this);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_resolver_trace)) {
    gpr_log(GPR_INFO, "[XdsDependencyManager %p] returning config: %s", this,
            config->ToString().c_str());
  }
  watcher_->OnUpdate(std::move(config));
}

}  // namespace grpc_core
