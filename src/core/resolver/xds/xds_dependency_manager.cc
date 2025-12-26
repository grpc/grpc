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

#include "src/core/resolver/xds/xds_dependency_manager.h"

#include <set>

#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/xds/xds_channel_args.h"
#include "src/core/resolver/fake/fake_resolver.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/match.h"
#include "src/core/xds/grpc/xds_cluster_parser.h"
#include "src/core/xds/grpc/xds_endpoint_parser.h"
#include "src/core/xds/grpc/xds_listener_parser.h"
#include "src/core/xds/grpc/xds_route_config_parser.h"
#include "src/core/xds/grpc/xds_routing.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

namespace {

// Max depth of aggregate cluster dependency graph.
constexpr int kMaxXdsAggregateClusterRecursionDepth = 16;

}  // namespace

//
// XdsDependencyManager::ListenerWatcher
//

class XdsDependencyManager::ListenerWatcher final
    : public XdsListenerResourceType::WatcherInterface {
 public:
  explicit ListenerWatcher(RefCountedPtr<XdsDependencyManager> dependency_mgr)
      : dependency_mgr_(std::move(dependency_mgr)) {}

  void OnResourceChanged(
      absl::StatusOr<std::shared_ptr<const XdsListenerResource>> listener,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [dependency_mgr = dependency_mgr_, listener = std::move(listener),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          dependency_mgr->OnListenerUpdate(std::move(listener));
        },
        DEBUG_LOCATION);
  }

  void OnAmbientError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [dependency_mgr = dependency_mgr_, status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          dependency_mgr->OnListenerAmbientError(std::move(status));
        });
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
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          route_config,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<RouteConfigWatcher>(),
         route_config = std::move(route_config),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnRouteConfigUpdate(self->name_,
                                                     std::move(route_config));
        });
  }

  void OnAmbientError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<RouteConfigWatcher>(), status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnRouteConfigAmbientError(self->name_,
                                                           std::move(status));
        });
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
      absl::StatusOr<std::shared_ptr<const XdsClusterResource>> cluster,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<ClusterWatcher>(), cluster = std::move(cluster),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnClusterUpdate(self->name_,
                                                 std::move(cluster));
        });
  }

  void OnAmbientError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<ClusterWatcher>(), status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnClusterAmbientError(self->name_,
                                                       std::move(status));
        });
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
      absl::StatusOr<std::shared_ptr<const XdsEndpointResource>> endpoint,
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

  void OnAmbientError(
      absl::Status status,
      RefCountedPtr<XdsClient::ReadDelayHandle> read_delay_handle) override {
    dependency_mgr_->work_serializer_->Run(
        [self = RefAsSubclass<EndpointWatcher>(), status = std::move(status),
         read_delay_handle = std::move(read_delay_handle)]() mutable {
          self->dependency_mgr_->OnEndpointAmbientError(self->name_,
                                                        std::move(status));
        });
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
        });
  }

 private:
  RefCountedPtr<XdsDependencyManager> dependency_mgr_;
  std::string name_;
};

//
// XdsDependencyManager::ClusterSubscription
//

void XdsDependencyManager::ClusterSubscription::Orphaned() {
  dependency_mgr_->work_serializer_->Run([self = WeakRef()]() {
    self->dependency_mgr_->OnClusterSubscriptionUnref(self->cluster_name_,
                                                      self.get());
  });
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
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this << "] starting watch for listener "
      << listener_resource_name_;
  auto listener_watcher = MakeRefCounted<ListenerWatcher>(Ref());
  listener_watcher_ = listener_watcher.get();
  XdsListenerResourceType::StartWatch(
      xds_client_.get(), listener_resource_name_, std::move(listener_watcher));
}

void XdsDependencyManager::Orphan() {
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this << "] shutting down";
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
  for (const auto& [name, cluster_state] : cluster_watchers_) {
    XdsClusterResourceType::CancelWatch(xds_client_.get(), name,
                                        cluster_state.watcher,
                                        /*delay_unsubscription=*/false);
  }
  for (const auto& [name, endpoint_state] : endpoint_watchers_) {
    XdsEndpointResourceType::CancelWatch(xds_client_.get(), name,
                                         endpoint_state.watcher,
                                         /*delay_unsubscription=*/false);
  }
  cluster_subscriptions_.clear();
  xds_client_.reset();
  for (auto& [_, dns_state] : dns_resolvers_) {
    dns_state.resolver.reset();
  }
  Unref();
}

void XdsDependencyManager::RequestReresolution() {
  for (const auto& [_, dns_state] : dns_resolvers_) {
    dns_state.resolver->RequestReresolutionLocked();
  }
}

void XdsDependencyManager::ResetBackoff() {
  for (const auto& [_, dns_state] : dns_resolvers_) {
    dns_state.resolver->ResetBackoffLocked();
  }
}

void XdsDependencyManager::OnListenerUpdate(
    absl::StatusOr<std::shared_ptr<const XdsListenerResource>> listener) {
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this << "] received Listener update";
  if (xds_client_ == nullptr) return;
  if (!listener.ok()) {
    current_listener_.reset();
    return ReportError("LDS", listener_resource_name_,
                       listener.status().message());
  }
  const auto* hcm = std::get_if<XdsListenerResource::HttpConnectionManager>(
      &(*listener)->listener);
  if (hcm == nullptr) {
    current_listener_.reset();
    return ReportError("LDS", listener_resource_name_, "not an API listener");
  }
  current_listener_ = *std::move(listener);
  lds_resolution_note_.clear();
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
          GRPC_TRACE_LOG(xds_resolver, INFO)
              << "[XdsDependencyManager " << this
              << "] starting watch for route config " << route_config_name_;
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

void XdsDependencyManager::OnListenerAmbientError(absl::Status status) {
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this
      << "] received Listener error: " << listener_resource_name_ << ": "
      << status;
  if (xds_client_ == nullptr) return;
  if (status.ok()) {
    lds_resolution_note_.clear();
  } else {
    lds_resolution_note_ = absl::StrCat(
        "LDS resource ", listener_resource_name_, ": ", status.message());
  }
  MaybeReportUpdate();
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
        std::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
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
    absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
        route_config) {
  GRPC_TRACE_LOG(xds_resolver, INFO) << "[XdsDependencyManager " << this
                                     << "] received RouteConfig update for "
                                     << (name.empty() ? "<inline>" : name);
  if (xds_client_ == nullptr) return;
  if (!route_config.ok()) {
    current_virtual_host_ = nullptr;
    ReportError(route_config_name_.empty() ? "LDS" : "RDS",
                route_config_name_.empty() ? listener_resource_name_
                                           : route_config_name_,
                route_config.status().message());
    return;
  }
  // Ignore updates for stale names.
  if (name.empty()) {
    if (!route_config_name_.empty()) return;
  } else {
    if (name != route_config_name_) return;
  }
  // Find the relevant VirtualHost from the RouteConfiguration.
  // If the resource doesn't have the right vhost, fail.
  auto vhost_index = XdsRouting::FindVirtualHostForDomain(
      XdsVirtualHostListIterator(&(*route_config)->virtual_hosts),
      data_plane_authority_);
  if (!vhost_index.has_value()) {
    current_virtual_host_ = nullptr;
    ReportError(route_config_name_.empty() ? "LDS" : "RDS",
                route_config_name_.empty() ? listener_resource_name_
                                           : route_config_name_,
                absl::StrCat("could not find VirtualHost for ",
                             data_plane_authority_, " in RouteConfiguration"));
    return;
  }
  // Update our data.
  current_route_config_ = std::move(*route_config);
  current_virtual_host_ = &current_route_config_->virtual_hosts[*vhost_index];
  clusters_from_route_config_ =
      GetClustersFromVirtualHost(*current_virtual_host_);
  rds_resolution_note_.clear();
  MaybeReportUpdate();
}

void XdsDependencyManager::OnRouteConfigAmbientError(std::string resource_name,
                                                     absl::Status status) {
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this
      << "] received RouteConfig error: " << resource_name << ": " << status;
  if (xds_client_ == nullptr) return;
  if (status.ok()) {
    rds_resolution_note_.clear();
  } else {
    rds_resolution_note_ =
        absl::StrCat("RDS resource ", resource_name, ": ", status.message());
  }
  MaybeReportUpdate();
}

void XdsDependencyManager::OnClusterUpdate(
    const std::string& name,
    absl::StatusOr<std::shared_ptr<const XdsClusterResource>> cluster) {
  GRPC_TRACE_LOG(xds_resolver, INFO) << "[XdsDependencyManager " << this
                                     << "] received Cluster update: " << name;
  if (xds_client_ == nullptr) return;
  if (!cluster.ok()) {
    cluster = absl::UnavailableError(
        absl::StrCat("CDS resource ", name, ": ", cluster.status().message()));
  }
  auto it = cluster_watchers_.find(name);
  if (it == cluster_watchers_.end()) return;
  it->second.update = std::move(cluster);
  it->second.resolution_note.clear();
  MaybeReportUpdate();
}

void XdsDependencyManager::OnClusterAmbientError(const std::string& name,
                                                 absl::Status status) {
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this
      << "] received Cluster error: " << name << " " << status;
  if (xds_client_ == nullptr) return;
  auto it = cluster_watchers_.find(name);
  if (it == cluster_watchers_.end()) return;
  auto& cluster_watcher = it->second;
  if (status.ok()) {
    cluster_watcher.resolution_note.clear();
  } else {
    cluster_watcher.resolution_note =
        absl::StrCat("CDS resource ", name, ": ", status.message());
  }
  MaybeReportUpdate();
}

void XdsDependencyManager::OnEndpointUpdate(
    const std::string& name,
    absl::StatusOr<std::shared_ptr<const XdsEndpointResource>> endpoint) {
  GRPC_TRACE_LOG(xds_resolver, INFO) << "[XdsDependencyManager " << this
                                     << "] received Endpoint update: " << name;
  if (xds_client_ == nullptr) return;
  auto it = endpoint_watchers_.find(name);
  if (it == endpoint_watchers_.end()) return;
  auto& endpoint_watcher = it->second;
  if (!endpoint.ok()) {
    endpoint_watcher.update.endpoints.reset();
    endpoint_watcher.update.resolution_note =
        absl::StrCat("EDS resource ", name, ": ", endpoint.status().message());
  } else {
    if ((*endpoint)->priorities.empty()) {
      endpoint_watcher.update.resolution_note =
          absl::StrCat("EDS resource ", name, ": contains no localities");
    } else {
      std::set<absl::string_view> empty_localities;
      for (const auto& priority : (*endpoint)->priorities) {
        for (const auto& [name, locality] : priority.localities) {
          if (locality.endpoints.empty()) {
            empty_localities.insert(
                name->human_readable_string().as_string_view());
          }
        }
      }
      if (!empty_localities.empty()) {
        endpoint_watcher.update.resolution_note = absl::StrCat(
            "EDS resource ", name, ": contains empty localities: [",
            absl::StrJoin(empty_localities, "; "), "]");
      } else {
        endpoint_watcher.update.resolution_note.clear();
      }
    }
    endpoint_watcher.update.endpoints = std::move(*endpoint);
  }
  MaybeReportUpdate();
}

void XdsDependencyManager::OnEndpointAmbientError(const std::string& name,
                                                  absl::Status status) {
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this
      << "] received Endpoint error: " << name << " " << status;
  if (xds_client_ == nullptr) return;
  auto it = endpoint_watchers_.find(name);
  if (it == endpoint_watchers_.end()) return;
  auto& endpoint_watcher = it->second;
  if (status.ok()) {
    endpoint_watcher.update.resolution_note.clear();
  } else {
    endpoint_watcher.update.resolution_note =
        absl::StrCat("EDS resource ", name, ": ", status.message());
  }
  MaybeReportUpdate();
}

void XdsDependencyManager::OnDnsResult(const std::string& dns_name,
                                       Resolver::Result result) {
  GRPC_TRACE_LOG(xds_resolver, INFO) << "[XdsDependencyManager " << this
                                     << "] received DNS update: " << dns_name;
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
    std::vector<std::string> addresses;
    ChannelArgs address_args = result.addresses->front().args();
    for (const auto& address : *result.addresses) {
      GRPC_CHECK_EQ(address.addresses().size(), 1UL);
      addresses.emplace_back(address.addresses().front());
      GRPC_CHECK_EQ(address_args, address.args());
    }
    locality.endpoints.emplace_back(
        std::move(addresses),
        address_args.Set(GRPC_ARG_ADDRESS_NAME, dns_name));
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

std::string XdsDependencyManager::GenerateResolutionNoteForCluster(
    absl::string_view cluster_resolution_note,
    absl::string_view endpoint_resolution_note) const {
  std::array<absl::string_view, 4> notes = {
      lds_resolution_note_, rds_resolution_note_, cluster_resolution_note,
      endpoint_resolution_note};
  std::vector<absl::string_view> resolution_notes;
  for (const auto& note : notes) {
    if (!note.empty()) resolution_notes.push_back(note);
  }
  std::string node_id_buffer;
  if (resolution_notes.empty()) {
    const XdsBootstrap::Node* node =
        DownCast<const GrpcXdsBootstrap&>(xds_client_->bootstrap()).node();
    if (node != nullptr) {
      node_id_buffer = absl::StrCat("xDS node ID:", node->id());
      resolution_notes.push_back(node_id_buffer);
    }
  }
  return absl::StrJoin(resolution_notes, "; ");
}

bool XdsDependencyManager::PopulateClusterConfigMap(
    absl::string_view name, int depth,
    absl::flat_hash_map<std::string, absl::StatusOr<XdsConfig::ClusterConfig>>*
        cluster_config_map,
    std::set<absl::string_view>* eds_resources_seen,
    std::set<absl::string_view>* dns_names_seen,
    absl::StatusOr<std::vector<absl::string_view>>* leaf_clusters,
    std::vector<std::string>* resolution_notes) {
  if (depth > 0) {
    GRPC_CHECK_NE(leaf_clusters, nullptr);
    GRPC_CHECK_NE(resolution_notes, nullptr);
  }
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
  auto [it, inserted] = cluster_config_map->emplace(
      name, absl::InternalError("cluster data not yet available"));
  if (!inserted) return true;
  auto& cluster_config = it->second;
  auto& state = cluster_watchers_[name];
  // Create a new watcher if needed.
  if (state.watcher == nullptr) {
    auto watcher = MakeRefCounted<ClusterWatcher>(Ref(), name);
    GRPC_TRACE_LOG(xds_resolver, INFO)
        << "[XdsDependencyManager " << this << "] starting watch for cluster "
        << name;
    state.watcher = watcher.get();
    XdsClusterResourceType::StartWatch(xds_client_.get(), name,
                                       std::move(watcher));
    return false;
  }
  // If there was an error fetching the CDS resource, report the error.
  if (!state.update.ok()) {
    cluster_config = state.update.status();
    if (resolution_notes != nullptr) {
      resolution_notes->emplace_back(state.update.status().message());
    }
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
          GRPC_TRACE_LOG(xds_resolver, INFO)
              << "[XdsDependencyManager " << this
              << "] starting watch for endpoint " << eds_resource_name;
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
        cluster_config.emplace(
            *state.update, eds_state.update.endpoints,
            GenerateResolutionNoteForCluster(state.resolution_note,
                                             eds_state.update.resolution_note));
        if (leaf_clusters != nullptr) (*leaf_clusters)->push_back(name);
        return true;
      },
      // LOGICAL_DNS cluster.
      [&](const XdsClusterResource::LogicalDns& logical_dns) {
        dns_names_seen->insert(logical_dns.hostname);
        // Start DNS resolver if needed.
        auto& dns_state = dns_resolvers_[logical_dns.hostname];
        if (dns_state.resolver == nullptr) {
          GRPC_TRACE_LOG(xds_resolver, INFO)
              << "[XdsDependencyManager " << this
              << "] starting DNS resolver for " << logical_dns.hostname;
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
        cluster_config.emplace(
            *state.update, dns_state.update.endpoints,
            GenerateResolutionNoteForCluster(state.resolution_note,
                                             dns_state.update.resolution_note));
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
        std::vector<std::string> child_resolution_notes;
        bool have_all_resources = true;
        for (const std::string& child_name :
             aggregate.prioritized_cluster_names) {
          have_all_resources &= PopulateClusterConfigMap(
              child_name, depth + 1, cluster_config_map, eds_resources_seen,
              dns_names_seen, &child_leaf_clusters, &child_resolution_notes);
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
        // If needed, propagate leaf cluster list and resolution note
        // lists up the tree.
        if (leaf_clusters != nullptr) {
          (*leaf_clusters)
              ->insert((*leaf_clusters)->end(), child_leaf_clusters->begin(),
                       child_leaf_clusters->end());
        }
        if (resolution_notes != nullptr) {
          resolution_notes->insert(resolution_notes->end(),
                                   child_resolution_notes.begin(),
                                   child_resolution_notes.end());
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
        aggregate_cluster_config.emplace(
            std::move(cluster_resource), std::move(*child_leaf_clusters),
            absl::StrJoin(child_resolution_notes, "; "));
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
  if (current_listener_ == nullptr || current_virtual_host_ == nullptr) return;
  auto config = MakeRefCounted<XdsConfig>();
  config->listener = current_listener_;
  config->route_config = current_route_config_;
  config->virtual_host = current_virtual_host_;
  // Determine the set of clusters we should be watching.
  std::set<absl::string_view> clusters_to_watch;
  for (const absl::string_view& cluster : clusters_from_route_config_) {
    clusters_to_watch.insert(cluster);
  }
  for (const auto& [name, _] : cluster_subscriptions_) {
    clusters_to_watch.insert(name);
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
    auto& [cluster_name, cluster_watcher] = *it;
    if (config->clusters.contains(cluster_name)) {
      ++it;
      continue;
    }
    GRPC_TRACE_LOG(xds_resolver, INFO)
        << "[XdsDependencyManager " << this << "] cancelling watch for cluster "
        << cluster_name;
    XdsClusterResourceType::CancelWatch(xds_client_.get(), cluster_name,
                                        cluster_watcher.watcher,
                                        /*delay_unsubscription=*/false);
    cluster_watchers_.erase(it++);
  }
  // Remove entries in endpoint_watchers_ for any EDS resources not in
  // eds_resources_seen.
  for (auto it = endpoint_watchers_.begin(); it != endpoint_watchers_.end();) {
    auto& [eds_resource_name, endpoint_watcher] = *it;
    if (eds_resources_seen.find(eds_resource_name) !=
        eds_resources_seen.end()) {
      ++it;
      continue;
    }
    GRPC_TRACE_LOG(xds_resolver, INFO)
        << "[XdsDependencyManager " << this
        << "] cancelling watch for EDS resource " << eds_resource_name;
    XdsEndpointResourceType::CancelWatch(xds_client_.get(), eds_resource_name,
                                         endpoint_watcher.watcher,
                                         /*delay_unsubscription=*/false);
    endpoint_watchers_.erase(it++);
  }
  // Remove entries in dns_resolvers_ for any DNS name not in
  // dns_names_seen.
  for (auto it = dns_resolvers_.begin(); it != dns_resolvers_.end();) {
    const std::string& dns_name = it->first;
    if (dns_names_seen.find(dns_name) != dns_names_seen.end()) {
      ++it;
      continue;
    }
    GRPC_TRACE_LOG(xds_resolver, INFO)
        << "[XdsDependencyManager " << this
        << "] shutting down DNS resolver for " << dns_name;
    dns_resolvers_.erase(it++);
  }
  // If we have all the data we need, then send an update.
  if (!have_all_resources) {
    GRPC_TRACE_LOG(xds_resolver, INFO)
        << "[XdsDependencyManager " << this
        << "] missing data -- NOT returning config";
    return;
  }
  GRPC_TRACE_LOG(xds_resolver, INFO)
      << "[XdsDependencyManager " << this
      << "] returning config: " << config->ToString();
  watcher_->OnUpdate(std::move(config));
}

void XdsDependencyManager::ReportError(absl::string_view resource_type,
                                       absl::string_view resource_name,
                                       absl::string_view error) {
  watcher_->OnUpdate(absl::UnavailableError(
      absl::StrCat(resource_type, " resource ", resource_name, ": ", error)));
}

}  // namespace grpc_core
