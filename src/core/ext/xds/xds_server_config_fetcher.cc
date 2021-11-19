//
//
// Copyright 2020 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"

#include "src/core/ext/filters/server_config_selector/server_config_selector.h"
#include "src/core/ext/filters/server_config_selector/server_config_selector_filter.h"
#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/ext/xds/xds_channel_stack_modifier.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_routing.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {
namespace {

TraceFlag grpc_xds_server_config_fetcher_trace(false,
                                               "xds_server_config_fetcher");

// A server config fetcher that fetches the information for configuring server
// listeners from the xDS control plane.
class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  XdsServerConfigFetcher(RefCountedPtr<XdsClient> xds_client,
                         grpc_server_xds_status_notifier notifier);

  void StartWatch(std::string listening_address,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override;

  void CancelWatch(
      grpc_server_config_fetcher::WatcherInterface* watcher) override;

  // Return the interested parties from the xds client so that it can be polled.
  grpc_pollset_set* interested_parties() override {
    return xds_client_->interested_parties();
  }

 private:
  class ListenerWatcher;

  RefCountedPtr<XdsClient> xds_client_;
  grpc_server_xds_status_notifier serving_status_notifier_;
  Mutex mu_;
  std::map<grpc_server_config_fetcher::WatcherInterface*, ListenerWatcher*>
      listener_watchers_ ABSL_GUARDED_BY(mu_);
};

// A watcher implementation for listening on LDS updates from the xDS control
// plane. When a good LDS update is received, it creates a
// FilterChainMatchManager object that would replace the existing (if any)
// FilterChainMatchManager object after all referenced RDS resources are
// fetched. Note that a good update also causes the server listener to start
// listening if it isn't already. If an error LDS update is received (NACKed
// resource, timeouts), the previous good FilterChainMatchManager, if any,
// continues to be used. If there isn't any previous good update or if the
// update received was a fatal error (resource does not exist), the server
// listener is made to stop listening.
class XdsServerConfigFetcher::ListenerWatcher
    : public XdsClient::ListenerWatcherInterface {
 public:
  ListenerWatcher(RefCountedPtr<XdsClient> xds_client,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      server_config_watcher,
                  grpc_server_xds_status_notifier serving_status_notifier,
                  std::string listening_address);

  void OnListenerChanged(XdsListenerResource listener) override;

  void OnError(grpc_error_handle error) override;

  void OnResourceDoesNotExist() override;

  const std::string& listening_address() const { return listening_address_; }

 private:
  class FilterChainMatchManager;

  void OnFatalError(absl::Status status) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Invoked by FilterChainMatchManager that is done fetching all referenced RDS
  // resources. If the calling FilterChainMatchManager is the
  // pending_filter_chain_match_manager_, it is promoted to be the
  // filter_chain_match_manager_ in use.
  void PendingFilterChainMatchManagerReady(
      FilterChainMatchManager* filter_chain_match_manager) {
    MutexLock lock(&mu_);
    PendingFilterChainMatchManagerReadyLocked(filter_chain_match_manager);
  }
  void PendingFilterChainMatchManagerReadyLocked(
      FilterChainMatchManager* filter_chain_match_manager)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  const RefCountedPtr<XdsClient> xds_client_;
  const std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
      server_config_watcher_;
  const grpc_server_xds_status_notifier serving_status_notifier_;
  const std::string listening_address_;
  Mutex mu_;
  RefCountedPtr<FilterChainMatchManager> filter_chain_match_manager_
      ABSL_GUARDED_BY(mu_);
  RefCountedPtr<FilterChainMatchManager> pending_filter_chain_match_manager_
      ABSL_GUARDED_BY(mu_);
};

// A connection manager used by the server listener code to inject channel args
// to be used for each incoming connection. This implementation chooses the
// appropriate filter chain from the xDS Listener resource and injects channel
// args that configure the right mTLS certs and cause the right set of HTTP
// filters to be injected.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager
    : public grpc_server_config_fetcher::ConnectionManager {
 public:
  FilterChainMatchManager(
      RefCountedPtr<XdsClient> xds_client,
      XdsListenerResource::FilterChainMap filter_chain_map,
      absl::optional<XdsListenerResource::FilterChainData> default_filter_chain);

  absl::StatusOr<grpc_channel_args*> UpdateChannelArgsForConnection(
      grpc_channel_args* args, grpc_endpoint* tcp) override;

  void Orphan() override;

  // Invoked by ListenerWatcher to start fetching referenced RDS resources.
  void StartRdsWatch(RefCountedPtr<ListenerWatcher> listener_watcher)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&ListenerWatcher::mu_);

  const XdsListenerResource::FilterChainMap& filter_chain_map() const {
    return filter_chain_map_;
  }

  const absl::optional<XdsListenerResource::FilterChainData>&
  default_filter_chain() const {
    return default_filter_chain_;
  }

 private:
  struct CertificateProviders {
    // We need to save our own refs to the root and instance certificate
    // providers since the xds certificate provider just stores a ref to their
    // distributors.
    RefCountedPtr<grpc_tls_certificate_provider> root;
    RefCountedPtr<grpc_tls_certificate_provider> instance;
    RefCountedPtr<XdsCertificateProvider> xds;
  };

  class RouteConfigWatcher;
  struct RdsUpdateState {
    RouteConfigWatcher* watcher;
    absl::optional<absl::StatusOr<XdsRouteConfigResource>> rds_update;
  };

  class XdsServerConfigSelector;
  class StaticXdsServerConfigSelectorProvider;
  class DynamicXdsServerConfigSelectorProvider;

  absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
  CreateOrGetXdsCertificateProviderFromFilterChainData(
      const XdsListenerResource::FilterChainData* filter_chain);

  // Helper functions invoked by RouteConfigWatcher when there are updates to
  // RDS resources.
  void OnRouteConfigChanged(const std::string& resource_name,
                            XdsRouteConfigResource route_config);
  void OnError(const std::string& resource_name, grpc_error_handle error);
  void OnResourceDoesNotExist(const std::string& resource_name);

  RefCountedPtr<XdsClient> xds_client_;
  // This ref is only kept around till the FilterChainMatchManager becomes
  // ready.
  RefCountedPtr<ListenerWatcher> listener_watcher_;
  const XdsListenerResource::FilterChainMap filter_chain_map_;
  const absl::optional<XdsListenerResource::FilterChainData>
      default_filter_chain_;
  Mutex mu_;
  size_t rds_resources_yet_to_fetch_ ABSL_GUARDED_BY(mu_) = 0;
  std::map<std::string /* resource_name */, RdsUpdateState> rds_map_
      ABSL_GUARDED_BY(mu_);
  std::map<const XdsListenerResource::FilterChainData*, CertificateProviders>
      certificate_providers_map_ ABSL_GUARDED_BY(mu_);
};

// A watcher implementation for listening on RDS updates referenced to by a
// FilterChainMatchManager object. After all referenced RDS resources are
// fetched (errors are allowed), the FilterChainMatchManager tries to replace
// the current object. The watcher continues to update the referenced RDS
// resources so that new XdsServerConfigSelectorProvider objects are created
// with the latest updates and new connections do not need to wait for the RDS
// resources to be fetched.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    RouteConfigWatcher : public XdsClient::RouteConfigWatcherInterface {
 public:
  RouteConfigWatcher(
      std::string resource_name,
      WeakRefCountedPtr<FilterChainMatchManager> filter_chain_match_manager)
      : resource_name_(std::move(resource_name)),
        filter_chain_match_manager_(std::move(filter_chain_match_manager)) {}

  void OnRouteConfigChanged(XdsRouteConfigResource route_config) override {
    filter_chain_match_manager_->OnRouteConfigChanged(resource_name_,
                                                      std::move(route_config));
  }

  void OnError(grpc_error_handle error) override {
    filter_chain_match_manager_->OnError(resource_name_, error);
  }

  void OnResourceDoesNotExist() override {
    filter_chain_match_manager_->OnResourceDoesNotExist(resource_name_);
  }

 private:
  std::string resource_name_;
  WeakRefCountedPtr<FilterChainMatchManager> filter_chain_match_manager_;
};

// An implementation of ServerConfigSelector used by
// StaticXdsServerConfigSelectorProvider and
// DynamicXdsServerConfigSelectorProvider to parse the RDS update and get
// per-call configuration based on incoming metadata.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    XdsServerConfigSelector : public ServerConfigSelector {
 public:
  static absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>> Create(
      XdsRouteConfigResource rds_update,
      const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
          http_filters);
  ~XdsServerConfigSelector() override = default;

  CallConfig GetCallConfig(grpc_metadata_batch* metadata) override;

 private:
  struct VirtualHost {
    struct Route {
      // true if an action other than kNonForwardingAction is configured.
      bool unsupported_action;
      XdsRouteConfigResource::Route::Matchers matchers;
      RefCountedPtr<ServiceConfig> method_config;
    };

    class RouteListIterator : public XdsRouting::RouteListIterator {
     public:
      explicit RouteListIterator(const std::vector<Route>* routes)
          : routes_(routes) {}

      size_t Size() const override { return routes_->size(); }

      const XdsRouteConfigResource::Route::Matchers& GetMatchersForRoute(
          size_t index) const override {
        return (*routes_)[index].matchers;
      }

     private:
      const std::vector<Route>* routes_;
    };

    std::vector<std::string> domains;
    std::vector<Route> routes;
  };

  class VirtualHostListIterator : public XdsRouting::VirtualHostListIterator {
   public:
    explicit VirtualHostListIterator(
        const std::vector<VirtualHost>* virtual_hosts)
        : virtual_hosts_(virtual_hosts) {}

    size_t Size() const override { return virtual_hosts_->size(); }

    const std::vector<std::string>& GetDomainsForVirtualHost(
        size_t index) const override {
      return (*virtual_hosts_)[index].domains;
    }

   private:
    const std::vector<VirtualHost>* virtual_hosts_;
  };

  std::vector<VirtualHost> virtual_hosts_;
};

// An XdsServerConfigSelectorProvider implementation for when the
// RouteConfiguration is available inline.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    StaticXdsServerConfigSelectorProvider
    : public ServerConfigSelectorProvider {
 public:
  StaticXdsServerConfigSelectorProvider(
      absl::StatusOr<XdsRouteConfigResource> static_resource,
      std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
          http_filters)
      : static_resource_(std::move(static_resource)),
        http_filters_(std::move(http_filters)) {}

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> Watch(
      std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
          watcher) override {
    GPR_ASSERT(watcher_ == nullptr);
    watcher_ = std::move(watcher);
    if (!static_resource_.ok()) {
      return static_resource_.status();
    }
    return XdsServerConfigSelector::Create(static_resource_.value(),
                                           http_filters_);
  }

  void CancelWatch() override { watcher_.reset(); }

 private:
  absl::StatusOr<XdsRouteConfigResource> static_resource_;
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      http_filters_;
  std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
      watcher_;
};

// An XdsServerConfigSelectorProvider implementation for when the
// RouteConfiguration is to be fetched separately via RDS.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider
    : public ServerConfigSelectorProvider {
 public:
  DynamicXdsServerConfigSelectorProvider(
      RefCountedPtr<XdsClient> xds_client, std::string resource_name,
      absl::StatusOr<XdsRouteConfigResource> initial_resource,
      std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
          http_filters);

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> Watch(
      std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
          watcher) override;
  void CancelWatch() override;

 private:
  class RouteConfigWatcher;

  void OnRouteConfigChanged(XdsRouteConfigResource rds_update);
  void OnError(grpc_error_handle error);
  void OnResourceDoesNotExist();

  RefCountedPtr<XdsClient> xds_client_;
  std::string resource_name_;
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      http_filters_;
  RouteConfigWatcher* route_config_watcher_ = nullptr;
  Mutex mu_;
  std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
      watcher_ ABSL_GUARDED_BY(mu_);
  absl::StatusOr<XdsRouteConfigResource> resource_ ABSL_GUARDED_BY(mu_);
};

// A watcher implementation for updating the RDS resource used by
// DynamicXdsServerConfigSelectorProvider
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::RouteConfigWatcher
    : public XdsClient::RouteConfigWatcherInterface {
 public:
  explicit RouteConfigWatcher(
      RefCountedPtr<DynamicXdsServerConfigSelectorProvider> parent)
      : parent_(std::move(parent)) {}

  void OnRouteConfigChanged(XdsRouteConfigResource route_config) override {
    parent_->OnRouteConfigChanged(std::move(route_config));
  }

  void OnError(grpc_error_handle error) override { parent_->OnError(error); }

  void OnResourceDoesNotExist() override { parent_->OnResourceDoesNotExist(); }

 private:
  RefCountedPtr<DynamicXdsServerConfigSelectorProvider> parent_;
};

//
// XdsServerConfigFetcher
//

XdsServerConfigFetcher::XdsServerConfigFetcher(
    RefCountedPtr<XdsClient> xds_client,
    grpc_server_xds_status_notifier notifier)
    : xds_client_(std::move(xds_client)), serving_status_notifier_(notifier) {
  GPR_ASSERT(xds_client_ != nullptr);
}

void XdsServerConfigFetcher::StartWatch(
    std::string listening_address,
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface> watcher) {
  grpc_server_config_fetcher::WatcherInterface* watcher_ptr = watcher.get();
  auto listener_watcher = MakeRefCounted<ListenerWatcher>(
      xds_client_, std::move(watcher), serving_status_notifier_,
      listening_address);
  auto* listener_watcher_ptr = listener_watcher.get();
  xds_client_->WatchListenerData(
      absl::StrReplaceAll(
          xds_client_->bootstrap().server_listener_resource_name_template(),
          {{"%s", listening_address}}),
      std::move(listener_watcher));
  MutexLock lock(&mu_);
  listener_watchers_.emplace(watcher_ptr, listener_watcher_ptr);
}

void XdsServerConfigFetcher::CancelWatch(
    grpc_server_config_fetcher::WatcherInterface* watcher) {
  MutexLock lock(&mu_);
  auto it = listener_watchers_.find(watcher);
  if (it != listener_watchers_.end()) {
    // Cancel the watch on the listener before erasing
    xds_client_->CancelListenerDataWatch(
        absl::StrReplaceAll(
            xds_client_->bootstrap().server_listener_resource_name_template(),
            {{"%s", it->second->listening_address()}}),
        it->second, false /* delay_unsubscription */);
    listener_watchers_.erase(it);
  }
}

//
// XdsServerConfigFetcher::ListenerWatcher
//

XdsServerConfigFetcher::ListenerWatcher::ListenerWatcher(
    RefCountedPtr<XdsClient> xds_client,
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher,
    grpc_server_xds_status_notifier serving_status_notifier,
    std::string listening_address)
    : xds_client_(std::move(xds_client)),
      server_config_watcher_(std::move(server_config_watcher)),
      serving_status_notifier_(serving_status_notifier),
      listening_address_(std::move(listening_address)) {}

void XdsServerConfigFetcher::ListenerWatcher::OnListenerChanged(
    XdsListenerResource listener) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_server_config_fetcher_trace)) {
    gpr_log(GPR_INFO,
            "[ListenerWatcher %p] Received LDS update from xds client %p: %s",
            this, xds_client_.get(), listener.ToString().c_str());
  }
  if (listener.address != listening_address_) {
    MutexLock lock(&mu_);
    OnFatalError(absl::FailedPreconditionError(
        "Address in LDS update does not match listening address"));
    return;
  }
  auto new_filter_chain_match_manager = MakeRefCounted<FilterChainMatchManager>(
      xds_client_, std::move(listener.filter_chain_map),
      std::move(listener.default_filter_chain));
  MutexLock lock(&mu_);
  if (filter_chain_match_manager_ == nullptr ||
      !(new_filter_chain_match_manager->filter_chain_map() ==
            filter_chain_match_manager_->filter_chain_map() &&
        new_filter_chain_match_manager->default_filter_chain() ==
            filter_chain_match_manager_->default_filter_chain())) {
    pending_filter_chain_match_manager_ =
        std::move(new_filter_chain_match_manager);
    if (XdsRbacEnabled()) {
      pending_filter_chain_match_manager_->StartRdsWatch(Ref());
    } else {
      PendingFilterChainMatchManagerReadyLocked(
          pending_filter_chain_match_manager_.get());
    }
  }
}

void XdsServerConfigFetcher::ListenerWatcher::OnError(grpc_error_handle error) {
  MutexLock lock(&mu_);
  if (filter_chain_match_manager_ != nullptr ||
      pending_filter_chain_match_manager_ != nullptr) {
    gpr_log(GPR_ERROR,
            "ListenerWatcher:%p XdsClient reports error: %s for %s; "
            "ignoring in favor of existing resource",
            this, grpc_error_std_string(error).c_str(),
            listening_address_.c_str());
  } else {
    if (serving_status_notifier_.on_serving_status_update != nullptr) {
      serving_status_notifier_.on_serving_status_update(
          serving_status_notifier_.user_data, listening_address_.c_str(),
          {GRPC_STATUS_UNAVAILABLE, grpc_error_std_string(error).c_str()});
    } else {
      gpr_log(GPR_ERROR,
              "ListenerWatcher:%p error obtaining xDS Listener resource: %s; "
              "not serving on %s",
              this, grpc_error_std_string(error).c_str(),
              listening_address_.c_str());
    }
  }
  GRPC_ERROR_UNREF(error);
}

void XdsServerConfigFetcher::ListenerWatcher::OnFatalError(
    absl::Status status) {
  pending_filter_chain_match_manager_.reset();
  if (filter_chain_match_manager_ != nullptr) {
    // The server has started listening already, so we need to gracefully
    // stop serving.
    server_config_watcher_->StopServing();
    filter_chain_match_manager_.reset();
  }
  if (serving_status_notifier_.on_serving_status_update != nullptr) {
    serving_status_notifier_.on_serving_status_update(
        serving_status_notifier_.user_data, listening_address_.c_str(),
        {static_cast<grpc_status_code>(status.raw_code()),
         std::string(status.message()).c_str()});
  } else {
    gpr_log(GPR_ERROR,
            "ListenerWatcher:%p Encountered fatal error %s; not serving on %s",
            this, status.ToString().c_str(), listening_address_.c_str());
  }
}

void XdsServerConfigFetcher::ListenerWatcher::OnResourceDoesNotExist() {
  MutexLock lock(&mu_);
  OnFatalError(absl::NotFoundError("Requested listener does not exist"));
}

void XdsServerConfigFetcher::ListenerWatcher::
    PendingFilterChainMatchManagerReadyLocked(
        XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager*
            filter_chain_match_manager) {
  if (pending_filter_chain_match_manager_ != filter_chain_match_manager) {
    // This FilterChainMatchManager is no longer the current pending resource.
    // It should get cleaned up eventually. Ignore this update.
    return;
  }
  // Let the logger know about the update if there was no previous good update.
  if (filter_chain_match_manager_ == nullptr) {
    if (serving_status_notifier_.on_serving_status_update != nullptr) {
      serving_status_notifier_.on_serving_status_update(
          serving_status_notifier_.user_data, listening_address_.c_str(),
          {GRPC_STATUS_OK, ""});
    } else {
      gpr_log(GPR_INFO,
              "xDS Listener resource obtained; will start serving on %s",
              listening_address_.c_str());
    }
  }
  // Promote the pending FilterChainMatchManager
  filter_chain_match_manager_ = std::move(pending_filter_chain_match_manager_);
  // TODO(yashykt): Right now, the server_config_watcher_ does not invoke
  // XdsServerConfigFetcher while holding a lock, but that might change in the
  // future in which case we would want to execute this update outside the
  // critical region through a WorkSerializer similar to XdsClient.
  server_config_watcher_->UpdateConnectionManager(filter_chain_match_manager_);
}

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager
//

XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    FilterChainMatchManager(
        RefCountedPtr<XdsClient> xds_client,
        XdsListenerResource::FilterChainMap filter_chain_map,
        absl::optional<XdsListenerResource::FilterChainData> default_filter_chain)
    : xds_client_(std::move(xds_client)),
      filter_chain_map_(std::move(filter_chain_map)),
      default_filter_chain_(std::move(default_filter_chain)) {}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    StartRdsWatch(RefCountedPtr<ListenerWatcher> listener_watcher) {
  // Get the set of RDS resources to watch on
  std::set<std::string> resource_names;
  for (const auto& destination_ip : filter_chain_map_.destination_ip_vector) {
    for (const auto& source_type : destination_ip.source_types_array) {
      for (const auto& source_ip : source_type) {
        for (const auto& source_port_pair : source_ip.ports_map) {
          if (!source_port_pair.second.data->http_connection_manager
                   .route_config_name.empty()) {
            resource_names.insert(
                source_port_pair.second.data->http_connection_manager
                    .route_config_name);
          }
        }
      }
    }
  }
  if (default_filter_chain_.has_value() &&
      !default_filter_chain_->http_connection_manager.route_config_name
           .empty()) {
    resource_names.insert(
        default_filter_chain_->http_connection_manager.route_config_name);
  }
  // Start watching on referenced RDS resources
  {
    MutexLock lock(&mu_);
    for (const auto& resource_name : resource_names) {
      ++rds_resources_yet_to_fetch_;
      auto route_config_watcher =
          MakeRefCounted<RouteConfigWatcher>(resource_name, WeakRef());
      rds_map_.emplace(resource_name, RdsUpdateState{route_config_watcher.get(),
                                                     absl::nullopt});
      xds_client_->WatchRouteConfigData(resource_name,
                                        std::move(route_config_watcher));
    }
    if (rds_resources_yet_to_fetch_ != 0) {
      listener_watcher_ = std::move(listener_watcher);
      listener_watcher = nullptr;
    }
  }
  // Promote this filter chain match manager if all referenced resources are
  // fetched.
  if (listener_watcher != nullptr) {
    listener_watcher->PendingFilterChainMatchManagerReadyLocked(this);
  }
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    Orphan() {
  MutexLock lock(&mu_);
  // Cancel the RDS watches to clear up the weak refs
  for (const auto& entry : rds_map_) {
    xds_client_->CancelRouteConfigDataWatch(entry.first, entry.second.watcher,
                                            false /* delay_unsubscription */);
  }
  // Also give up the ref on ListenerWatcher since it won't be needed anymore
  listener_watcher_.reset();
}

absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    CreateOrGetXdsCertificateProviderFromFilterChainData(
        const XdsListenerResource::FilterChainData* filter_chain) {
  MutexLock lock(&mu_);
  auto it = certificate_providers_map_.find(filter_chain);
  if (it != certificate_providers_map_.end()) {
    return it->second.xds;
  }
  CertificateProviders certificate_providers;
  // Configure root cert.
  absl::string_view root_provider_instance_name =
      filter_chain->downstream_tls_context.common_tls_context
          .certificate_validation_context.ca_certificate_provider_instance
          .instance_name;
  absl::string_view root_provider_cert_name =
      filter_chain->downstream_tls_context.common_tls_context
          .certificate_validation_context.ca_certificate_provider_instance
          .certificate_name;
  if (!root_provider_instance_name.empty()) {
    certificate_providers.root =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(root_provider_instance_name);
    if (certificate_providers.root == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Certificate provider instance name: \"",
                       root_provider_instance_name, "\" not recognized."));
    }
  }
  // Configure identity cert.
  absl::string_view identity_provider_instance_name =
      filter_chain->downstream_tls_context.common_tls_context
          .tls_certificate_provider_instance.instance_name;
  absl::string_view identity_provider_cert_name =
      filter_chain->downstream_tls_context.common_tls_context
          .tls_certificate_provider_instance.certificate_name;
  if (!identity_provider_instance_name.empty()) {
    certificate_providers.instance =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(identity_provider_instance_name);
    if (certificate_providers.instance == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Certificate provider instance name: \"",
                       identity_provider_instance_name, "\" not recognized."));
    }
  }
  certificate_providers.xds = MakeRefCounted<XdsCertificateProvider>();
  certificate_providers.xds->UpdateRootCertNameAndDistributor(
      "", root_provider_cert_name,
      certificate_providers.root == nullptr
          ? nullptr
          : certificate_providers.root->distributor());
  certificate_providers.xds->UpdateIdentityCertNameAndDistributor(
      "", identity_provider_cert_name,
      certificate_providers.instance == nullptr
          ? nullptr
          : certificate_providers.instance->distributor());
  certificate_providers.xds->UpdateRequireClientCertificate(
      "", filter_chain->downstream_tls_context.require_client_certificate);
  auto xds_certificate_provider = certificate_providers.xds;
  certificate_providers_map_.emplace(filter_chain,
                                     std::move(certificate_providers));
  return xds_certificate_provider;
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    OnRouteConfigChanged(const std::string& resource_name,
                         XdsRouteConfigResource route_config) {
  RefCountedPtr<ListenerWatcher> listener_watcher;
  {
    MutexLock lock(&mu_);
    auto& state = rds_map_[resource_name];
    if (!state.rds_update.has_value()) {
      if (--rds_resources_yet_to_fetch_ == 0) {
        listener_watcher = std::move(listener_watcher_);
      }
    }
    state.rds_update = std::move(route_config);
  }
  // Promote the filter chain match manager object if all the referenced
  // resources are fetched.
  if (listener_watcher != nullptr) {
    listener_watcher->PendingFilterChainMatchManagerReady(this);
  }
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::OnError(
    const std::string& resource_name, grpc_error_handle error) {
  RefCountedPtr<ListenerWatcher> listener_watcher;
  {
    MutexLock lock(&mu_);
    auto& state = rds_map_[resource_name];
    if (!state.rds_update.has_value()) {
      if (--rds_resources_yet_to_fetch_ == 0) {
        listener_watcher = std::move(listener_watcher_);
      }
      state.rds_update = grpc_error_to_absl_status(error);
    } else {
      // Prefer existing good version over current errored version
      if (!state.rds_update->ok()) {
        state.rds_update = grpc_error_to_absl_status(error);
      }
    }
  }
  // Promote the filter chain match manager object if all the referenced
  // resources are fetched.
  if (listener_watcher != nullptr) {
    listener_watcher->PendingFilterChainMatchManagerReady(this);
  }
  GRPC_ERROR_UNREF(error);
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    OnResourceDoesNotExist(const std::string& resource_name) {
  RefCountedPtr<ListenerWatcher> listener_watcher;
  {
    MutexLock lock(&mu_);
    auto& state = rds_map_[resource_name];
    if (!state.rds_update.has_value()) {
      if (--rds_resources_yet_to_fetch_ == 0) {
        listener_watcher = std::move(listener_watcher_);
      }
    }
    state.rds_update =
        absl::NotFoundError("Requested route config does not exist");
  }
  // Promote the filter chain match manager object if all the referenced
  // resources are fetched.
  if (listener_watcher != nullptr) {
    listener_watcher->PendingFilterChainMatchManagerReady(this);
  }
}

const XdsListenerResource::FilterChainData* FindFilterChainDataForSourcePort(
    const XdsListenerResource::FilterChainMap::SourcePortsMap& source_ports_map,
    absl::string_view port_str) {
  int port = 0;
  if (!absl::SimpleAtoi(port_str, &port)) return nullptr;
  auto it = source_ports_map.find(port);
  if (it != source_ports_map.end()) {
    return it->second.data.get();
  }
  // Search for the catch-all port 0 since we didn't get a direct match
  it = source_ports_map.find(0);
  if (it != source_ports_map.end()) {
    return it->second.data.get();
  }
  return nullptr;
}

const XdsListenerResource::FilterChainData* FindFilterChainDataForSourceIp(
    const XdsListenerResource::FilterChainMap::SourceIpVector& source_ip_vector,
    const grpc_resolved_address* source_ip, absl::string_view port) {
  const XdsListenerResource::FilterChainMap::SourceIp* best_match = nullptr;
  for (const auto& entry : source_ip_vector) {
    // Special case for catch-all
    if (!entry.prefix_range.has_value()) {
      if (best_match == nullptr) {
        best_match = &entry;
      }
      continue;
    }
    if (best_match != nullptr && best_match->prefix_range.has_value() &&
        best_match->prefix_range->prefix_len >=
            entry.prefix_range->prefix_len) {
      continue;
    }
    if (grpc_sockaddr_match_subnet(source_ip, &entry.prefix_range->address,
                                   entry.prefix_range->prefix_len)) {
      best_match = &entry;
    }
  }
  if (best_match == nullptr) return nullptr;
  return FindFilterChainDataForSourcePort(best_match->ports_map, port);
}

bool IsLoopbackIp(const grpc_resolved_address* address) {
  const grpc_sockaddr* sock_addr =
      reinterpret_cast<const grpc_sockaddr*>(&address->addr);
  if (sock_addr->sa_family == GRPC_AF_INET) {
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(sock_addr);
    if (addr4->sin_addr.s_addr == grpc_htonl(INADDR_LOOPBACK)) {
      return true;
    }
  } else if (sock_addr->sa_family == GRPC_AF_INET6) {
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(sock_addr);
    if (memcmp(&addr6->sin6_addr, &in6addr_loopback,
               sizeof(in6addr_loopback)) == 0) {
      return true;
    }
  }
  return false;
}

const XdsListenerResource::FilterChainData* FindFilterChainDataForSourceType(
    const XdsListenerResource::FilterChainMap::ConnectionSourceTypesArray&
        source_types_array,
    grpc_endpoint* tcp, absl::string_view destination_ip) {
  auto source_uri = URI::Parse(grpc_endpoint_get_peer(tcp));
  if (!source_uri.ok() ||
      (source_uri->scheme() != "ipv4" && source_uri->scheme() != "ipv6")) {
    return nullptr;
  }
  std::string host;
  std::string port;
  if (!SplitHostPort(source_uri->path(), &host, &port)) {
    return nullptr;
  }
  grpc_resolved_address source_addr;
  grpc_error_handle error = grpc_string_to_sockaddr(
      &source_addr, host.c_str(), 0 /* port doesn't matter here */);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_DEBUG, "Could not parse string to socket address: %s",
            host.c_str());
    GRPC_ERROR_UNREF(error);
    return nullptr;
  }
  // Use kAny only if kSameIporLoopback and kExternal are empty
  if (source_types_array[static_cast<int>(
                             XdsListenerResource::FilterChainMap::
                                 ConnectionSourceType::kSameIpOrLoopback)]
          .empty() &&
      source_types_array[static_cast<int>(XdsListenerResource::FilterChainMap::
                                              ConnectionSourceType::kExternal)]
          .empty()) {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsListenerResource::FilterChainMap::ConnectionSourceType::kAny)],
        &source_addr, port);
  }
  if (IsLoopbackIp(&source_addr) || host == destination_ip) {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsListenerResource::FilterChainMap::ConnectionSourceType::
                kSameIpOrLoopback)],
        &source_addr, port);
  } else {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsListenerResource::FilterChainMap::ConnectionSourceType::
                kExternal)],
        &source_addr, port);
  }
}

const XdsListenerResource::FilterChainData* FindFilterChainDataForDestinationIp(
    const XdsListenerResource::FilterChainMap::DestinationIpVector
        destination_ip_vector,
    grpc_endpoint* tcp) {
  auto destination_uri = URI::Parse(grpc_endpoint_get_local_address(tcp));
  if (!destination_uri.ok() || (destination_uri->scheme() != "ipv4" &&
                                destination_uri->scheme() != "ipv6")) {
    return nullptr;
  }
  std::string host;
  std::string port;
  if (!SplitHostPort(destination_uri->path(), &host, &port)) {
    return nullptr;
  }
  grpc_resolved_address destination_addr;
  grpc_error_handle error = grpc_string_to_sockaddr(
      &destination_addr, host.c_str(), 0 /* port doesn't matter here */);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_DEBUG, "Could not parse string to socket address: %s",
            host.c_str());
    GRPC_ERROR_UNREF(error);
    return nullptr;
  }
  const XdsListenerResource::FilterChainMap::DestinationIp* best_match = nullptr;
  for (const auto& entry : destination_ip_vector) {
    // Special case for catch-all
    if (!entry.prefix_range.has_value()) {
      if (best_match == nullptr) {
        best_match = &entry;
      }
      continue;
    }
    if (best_match != nullptr && best_match->prefix_range.has_value() &&
        best_match->prefix_range->prefix_len >=
            entry.prefix_range->prefix_len) {
      continue;
    }
    if (grpc_sockaddr_match_subnet(&destination_addr,
                                   &entry.prefix_range->address,
                                   entry.prefix_range->prefix_len)) {
      best_match = &entry;
    }
  }
  if (best_match == nullptr) return nullptr;
  return FindFilterChainDataForSourceType(best_match->source_types_array, tcp,
                                          host);
}

absl::StatusOr<grpc_channel_args*> XdsServerConfigFetcher::ListenerWatcher::
    FilterChainMatchManager::UpdateChannelArgsForConnection(
        grpc_channel_args* args, grpc_endpoint* tcp) {
  const auto* filter_chain = FindFilterChainDataForDestinationIp(
      filter_chain_map_.destination_ip_vector, tcp);
  if (filter_chain == nullptr && default_filter_chain_.has_value()) {
    filter_chain = &default_filter_chain_.value();
  }
  if (filter_chain == nullptr) {
    grpc_channel_args_destroy(args);
    return absl::UnavailableError("No matching filter chain found");
  }
  absl::InlinedVector<grpc_arg, 3> args_to_add;
  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider;
  RefCountedPtr<XdsChannelStackModifier> channel_stack_modifier;
  RefCountedPtr<XdsCertificateProvider> xds_certificate_provider;
  // Add config selector filter
  if (XdsRbacEnabled()) {
    std::vector<const grpc_channel_filter*> filters;
    // Iterate the list of HTTP filters in reverse since in Core, received data
    // flows *up* the stack.
    for (auto reverse_iterator =
             filter_chain->http_connection_manager.http_filters.rbegin();
         reverse_iterator !=
         filter_chain->http_connection_manager.http_filters.rend();
         ++reverse_iterator) {
      // Find filter.  This is guaranteed to succeed, because it's checked
      // at config validation time in the XdsApi code.
      const XdsHttpFilterImpl* filter_impl =
          XdsHttpFilterRegistry::GetFilterForType(
              reverse_iterator->config.config_proto_type_name);
      GPR_ASSERT(filter_impl != nullptr);
      // Some filters like the router filter are no-op filters and do not have
      // an implementation.
      if (filter_impl->channel_filter() != nullptr) {
        filters.push_back(filter_impl->channel_filter());
      }
    }
    filters.push_back(&kServerConfigSelectorFilter);
    channel_stack_modifier =
        MakeRefCounted<XdsChannelStackModifier>(std::move(filters));
    if (filter_chain->http_connection_manager.rds_update.has_value()) {
      server_config_selector_provider =
          MakeRefCounted<StaticXdsServerConfigSelectorProvider>(
              filter_chain->http_connection_manager.rds_update.value(),
              filter_chain->http_connection_manager.http_filters);
    } else {
      absl::StatusOr<XdsRouteConfigResource> initial_resource;
      {
        MutexLock lock(&mu_);
        initial_resource =
            rds_map_[filter_chain->http_connection_manager.route_config_name]
                .rds_update.value();
      }
      server_config_selector_provider =
          MakeRefCounted<DynamicXdsServerConfigSelectorProvider>(
              xds_client_,
              filter_chain->http_connection_manager.route_config_name,
              std::move(initial_resource),
              filter_chain->http_connection_manager.http_filters);
    }
    args_to_add.emplace_back(server_config_selector_provider->MakeChannelArg());
    args_to_add.emplace_back(channel_stack_modifier->MakeChannelArg());
  }
  // Add XdsCertificateProvider if credentials are xDS.
  grpc_server_credentials* server_creds =
      grpc_find_server_credentials_in_args(args);
  if (server_creds != nullptr && server_creds->type() == kCredentialsTypeXds) {
    absl::StatusOr<RefCountedPtr<XdsCertificateProvider>> result =
        CreateOrGetXdsCertificateProviderFromFilterChainData(filter_chain);
    if (!result.ok()) {
      grpc_channel_args_destroy(args);
      return result.status();
    }
    xds_certificate_provider = std::move(*result);
    GPR_ASSERT(xds_certificate_provider != nullptr);
    args_to_add.emplace_back(xds_certificate_provider->MakeChannelArg());
  }
  if (!args_to_add.empty()) {
    grpc_channel_args* updated_args = grpc_channel_args_copy_and_add(
        args, args_to_add.data(), args_to_add.size());
    grpc_channel_args_destroy(args);
    args = updated_args;
  }
  return args;
}

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::XdsServerConfigSelector
//

absl::StatusOr<
    RefCountedPtr<XdsServerConfigFetcher::ListenerWatcher::
                      FilterChainMatchManager::XdsServerConfigSelector>>
XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    XdsServerConfigSelector::Create(
        XdsRouteConfigResource rds_update,
        const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
            http_filters) {
  auto config_selector = MakeRefCounted<XdsServerConfigSelector>();
  for (auto& vhost : rds_update.virtual_hosts) {
    config_selector->virtual_hosts_.emplace_back();
    auto& virtual_host = config_selector->virtual_hosts_.back();
    virtual_host.domains = std::move(vhost.domains);
    for (auto& route : vhost.routes) {
      virtual_host.routes.emplace_back();
      auto& config_selector_route = virtual_host.routes.back();
      config_selector_route.matchers = std::move(route.matchers);
      config_selector_route.unsupported_action =
          absl::get_if<XdsRouteConfigResource::Route::NonForwardingAction>(&route.action) ==
          nullptr;
      XdsRouting::GeneratePerHttpFilterConfigsResult result =
          XdsRouting::GeneratePerHTTPFilterConfigs(http_filters, vhost, route,
                                                   nullptr, nullptr);
      if (result.error != GRPC_ERROR_NONE) {
        return grpc_error_to_absl_status(result.error);
      }
      std::vector<std::string> fields;
      fields.reserve(result.per_filter_configs.size());
      for (const auto& p : result.per_filter_configs) {
        fields.emplace_back(absl::StrCat("    \"", p.first, "\": [\n",
                                         absl::StrJoin(p.second, ",\n"),
                                         "\n    ]"));
      }
      if (!fields.empty()) {
        std::string json = absl::StrCat(
            "{\n"
            "  \"methodConfig\": [ {\n"
            "    \"name\": [\n"
            "      {}\n"
            "    ],\n"
            "    ",
            absl::StrJoin(fields, ",\n"),
            "\n  } ]\n"
            "}");
        grpc_error_handle error = GRPC_ERROR_NONE;
        config_selector_route.method_config =
            ServiceConfig::Create(result.args, json.c_str(), &error);
        GPR_ASSERT(error == GRPC_ERROR_NONE);
      }
      grpc_channel_args_destroy(result.args);
    }
  }
  return config_selector;
}

ServerConfigSelector::CallConfig XdsServerConfigFetcher::ListenerWatcher::
    FilterChainMatchManager::XdsServerConfigSelector::GetCallConfig(
        grpc_metadata_batch* metadata) {
  CallConfig call_config;
  if (metadata->legacy_index()->named.path == nullptr) {
    call_config.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("No path found");
    return call_config;
  }
  absl::string_view path = StringViewFromSlice(
      GRPC_MDVALUE(metadata->legacy_index()->named.path->md));
  if (metadata->legacy_index()->named.authority == nullptr) {
    call_config.error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No authority found");
    return call_config;
  }
  absl::string_view authority = StringViewFromSlice(
      GRPC_MDVALUE(metadata->legacy_index()->named.authority->md));
  auto vhost_index = XdsRouting::FindVirtualHostForDomain(
      VirtualHostListIterator(&virtual_hosts_), authority);
  if (!vhost_index.has_value()) {
    call_config.error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
                               "could not find VirtualHost for ", authority,
                               " in RouteConfiguration")),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    return call_config;
  }
  auto& virtual_host = virtual_hosts_[vhost_index.value()];
  auto route_index = XdsRouting::GetRouteForRequest(
      VirtualHost::RouteListIterator(&virtual_host.routes), path, metadata);
  if (route_index.has_value()) {
    auto& route = virtual_host.routes[route_index.value()];
    // Found the matching route
    if (route.unsupported_action) {
      call_config.error = grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "Matching route has unsupported action"),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
      return call_config;
    }
    if (route.method_config != nullptr) {
      call_config.method_configs =
          route.method_config->GetMethodParsedConfigVector(grpc_empty_slice());
      call_config.service_config = route.method_config;
    }
    return call_config;
  }
  call_config.error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("No route matched"),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
  return call_config;
}

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::DynamicXdsServerConfigSelectorProvider
//

XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::
        DynamicXdsServerConfigSelectorProvider(
            RefCountedPtr<XdsClient> xds_client, std::string resource_name,
            absl::StatusOr<XdsRouteConfigResource> initial_resource,
            std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
                http_filters)
    : xds_client_(std::move(xds_client)),
      resource_name_(std::move(resource_name)),
      http_filters_(std::move(http_filters)),
      resource_(std::move(initial_resource)) {
  GPR_ASSERT(!resource_name_.empty());
  auto route_config_watcher = MakeRefCounted<RouteConfigWatcher>(Ref());
  route_config_watcher_ = route_config_watcher.get();
  xds_client_->WatchRouteConfigData(resource_name_,
                                    std::move(route_config_watcher));
}

absl::StatusOr<RefCountedPtr<ServerConfigSelector>>
XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::Watch(
        std::unique_ptr<
            ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
            watcher) {
  absl::StatusOr<XdsRouteConfigResource> resource;
  {
    MutexLock lock(&mu_);
    GPR_ASSERT(watcher_ == nullptr);
    watcher_ = std::move(watcher);
    resource = resource_;
  }
  if (!resource.ok()) {
    return resource.status();
  }
  return XdsServerConfigSelector::Create(resource.value(), http_filters_);
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::CancelWatch() {
  xds_client_->CancelRouteConfigDataWatch(resource_name_, route_config_watcher_,
                                          false /* delay_unsubscription */);
  MutexLock lock(&mu_);
  watcher_.reset();
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::OnRouteConfigChanged(
        XdsRouteConfigResource rds_update) {
  MutexLock lock(&mu_);
  resource_ = std::move(rds_update);
  if (watcher_ == nullptr) {
    return;
  }
  watcher_->OnServerConfigSelectorUpdate(
      XdsServerConfigSelector::Create(*resource_, http_filters_));
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::OnError(grpc_error_handle error) {
  MutexLock lock(&mu_);
  // Prefer existing good update.
  if (resource_.ok()) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  resource_ = grpc_error_to_absl_status(error);
  GRPC_ERROR_UNREF(error);
  if (watcher_ == nullptr) {
    return;
  }
  watcher_->OnServerConfigSelectorUpdate(resource_.status());
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::OnResourceDoesNotExist() {
  MutexLock lock(&mu_);
  resource_ = absl::NotFoundError("Requested route config does not exist");
  if (watcher_ == nullptr) {
    return;
  }
  watcher_->OnServerConfigSelectorUpdate(resource_.status());
}

}  // namespace
}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create(
    grpc_server_xds_status_notifier notifier, const grpc_channel_args* args) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  args = grpc_channel_args_remove_grpc_internal(args);
  GRPC_API_TRACE("grpc_server_config_fetcher_xds_create()", 0, ());
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::RefCountedPtr<grpc_core::XdsClient> xds_client =
      grpc_core::XdsClient::GetOrCreate(args, &error);
  grpc_channel_args_destroy(args);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Failed to create xds client: %s",
            grpc_error_std_string(error).c_str());
    GRPC_ERROR_UNREF(error);
    return nullptr;
  }
  if (xds_client->bootstrap()
          .server_listener_resource_name_template()
          .empty()) {
    gpr_log(GPR_ERROR,
            "server_listener_resource_name_template not provided in bootstrap "
            "file.");
    return nullptr;
  }
  return new grpc_core::XdsServerConfigFetcher(std::move(xds_client), notifier);
}
