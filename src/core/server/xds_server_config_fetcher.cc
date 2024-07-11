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

#include <string.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/core/server/server.h"
#include "src/core/server/server_config_selector.h"
#include "src/core/server/server_config_selector_filter.h"
#include "src/core/server/xds_channel_stack_modifier.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/xds/grpc/certificate_provider_store.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_certificate_provider.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_http_filters.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "src/core/xds/grpc/xds_routing.h"
#include "src/core/xds/xds_client/xds_client.h"

namespace grpc_core {
namespace {

using ReadDelayHandle = XdsClient::ReadDelayHandle;

// A server config fetcher that fetches the information for configuring server
// listeners from the xDS control plane.
class XdsServerConfigFetcher final : public grpc_server_config_fetcher {
 public:
  XdsServerConfigFetcher(RefCountedPtr<GrpcXdsClient> xds_client,
                         grpc_server_xds_status_notifier notifier);

  ~XdsServerConfigFetcher() override {
    xds_client_.reset(DEBUG_LOCATION, "XdsServerConfigFetcher");
  }

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

  RefCountedPtr<GrpcXdsClient> xds_client_;
  const grpc_server_xds_status_notifier serving_status_notifier_;
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
class XdsServerConfigFetcher::ListenerWatcher final
    : public XdsListenerResourceType::WatcherInterface {
 public:
  ListenerWatcher(RefCountedPtr<GrpcXdsClient> xds_client,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      server_config_watcher,
                  grpc_server_xds_status_notifier serving_status_notifier,
                  std::string listening_address);

  ~ListenerWatcher() override {
    xds_client_.reset(DEBUG_LOCATION, "ListenerWatcher");
  }

  void OnResourceChanged(
      std::shared_ptr<const XdsListenerResource> listener,
      RefCountedPtr<ReadDelayHandle> read_delay_handle) override;

  void OnError(absl::Status status,
               RefCountedPtr<ReadDelayHandle> read_delay_handle) override;

  void OnResourceDoesNotExist(
      RefCountedPtr<ReadDelayHandle> read_delay_handle) override;

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

  RefCountedPtr<GrpcXdsClient> xds_client_;
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
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager final
    : public grpc_server_config_fetcher::ConnectionManager {
 public:
  FilterChainMatchManager(RefCountedPtr<GrpcXdsClient> xds_client,
                          XdsListenerResource::FilterChainMap filter_chain_map,
                          absl::optional<XdsListenerResource::FilterChainData>
                              default_filter_chain);

  ~FilterChainMatchManager() override {
    xds_client_.reset(DEBUG_LOCATION, "FilterChainMatchManager");
  }

  absl::StatusOr<ChannelArgs> UpdateChannelArgsForConnection(
      const ChannelArgs& args, grpc_endpoint* tcp) override;

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
  class RouteConfigWatcher;
  struct RdsUpdateState {
    RouteConfigWatcher* watcher;
    absl::optional<
        absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>>
        rds_update;
  };

  class XdsServerConfigSelector;
  class StaticXdsServerConfigSelectorProvider;
  class DynamicXdsServerConfigSelectorProvider;

  absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
  CreateOrGetXdsCertificateProviderFromFilterChainData(
      const XdsListenerResource::FilterChainData* filter_chain);
  void Orphaned() override;

  // Helper functions invoked by RouteConfigWatcher when there are updates to
  // RDS resources.
  void OnRouteConfigChanged(
      const std::string& resource_name,
      std::shared_ptr<const XdsRouteConfigResource> route_config);
  void OnError(const std::string& resource_name, absl::Status status);
  void OnResourceDoesNotExist(const std::string& resource_name);

  RefCountedPtr<GrpcXdsClient> xds_client_;
  // This ref is only kept around till the FilterChainMatchManager becomes
  // ready.
  RefCountedPtr<ListenerWatcher> listener_watcher_;
  // TODO(roth): Consider holding a ref to the LDS resource and storing
  // a pointer to the filter chain data within that LDS resource, rather
  // than copying the filter chain data here.
  XdsListenerResource::FilterChainMap filter_chain_map_;
  absl::optional<XdsListenerResource::FilterChainData> default_filter_chain_;
  Mutex mu_;
  size_t rds_resources_yet_to_fetch_ ABSL_GUARDED_BY(mu_) = 0;
  std::map<std::string /* resource_name */, RdsUpdateState> rds_map_
      ABSL_GUARDED_BY(mu_);
  std::map<const XdsListenerResource::FilterChainData*,
           RefCountedPtr<XdsCertificateProvider>>
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
    RouteConfigWatcher final
    : public XdsRouteConfigResourceType::WatcherInterface {
 public:
  RouteConfigWatcher(
      std::string resource_name,
      WeakRefCountedPtr<FilterChainMatchManager> filter_chain_match_manager)
      : resource_name_(std::move(resource_name)),
        filter_chain_match_manager_(std::move(filter_chain_match_manager)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsRouteConfigResource> route_config,
      RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) override {
    filter_chain_match_manager_->OnRouteConfigChanged(resource_name_,
                                                      std::move(route_config));
  }

  void OnError(
      absl::Status status,
      RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) override {
    filter_chain_match_manager_->OnError(resource_name_, status);
  }

  void OnResourceDoesNotExist(
      RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) override {
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
    XdsServerConfigSelector final : public ServerConfigSelector {
 public:
  static absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>> Create(
      const XdsHttpFilterRegistry& http_filter_registry,
      std::shared_ptr<const XdsRouteConfigResource> rds_update,
      const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
          http_filters);
  ~XdsServerConfigSelector() override = default;

  absl::StatusOr<CallConfig> GetCallConfig(
      grpc_metadata_batch* metadata) override;

 private:
  struct VirtualHost {
    struct Route {
      // true if an action other than kNonForwardingAction is configured.
      bool unsupported_action;
      // TODO(roth): Consider holding a ref to the RDS resource and storing
      // a pointer to the matchers within that RDS resource, rather than
      // copying the matchers here.
      XdsRouteConfigResource::Route::Matchers matchers;
      RefCountedPtr<ServiceConfig> method_config;
    };

    class RouteListIterator final : public XdsRouting::RouteListIterator {
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

  class VirtualHostListIterator final
      : public XdsRouting::VirtualHostListIterator {
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
    StaticXdsServerConfigSelectorProvider final
    : public ServerConfigSelectorProvider {
 public:
  StaticXdsServerConfigSelectorProvider(
      RefCountedPtr<GrpcXdsClient> xds_client,
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          static_resource,
      std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
          http_filters)
      : xds_client_(std::move(xds_client)),
        static_resource_(std::move(static_resource)),
        http_filters_(std::move(http_filters)) {}

  ~StaticXdsServerConfigSelectorProvider() override {
    xds_client_.reset(DEBUG_LOCATION, "StaticXdsServerConfigSelectorProvider");
  }

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> Watch(
      std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
          watcher) override {
    CHECK(watcher_ == nullptr);
    watcher_ = std::move(watcher);
    if (!static_resource_.ok()) {
      return static_resource_.status();
    }
    return XdsServerConfigSelector::Create(
        static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
            .http_filter_registry(),
        static_resource_.value(), http_filters_);
  }

  void CancelWatch() override { watcher_.reset(); }

 private:
  void Orphaned() override {}

  RefCountedPtr<GrpcXdsClient> xds_client_;
  absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
      static_resource_;
  // TODO(roth): Consider holding a ref to the LDS resource and storing
  // a pointer to the HTTP filters within that LDS resource, rather than
  // copying the HTTP filters here.
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      http_filters_;
  std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
      watcher_;
};

// An XdsServerConfigSelectorProvider implementation for when the
// RouteConfiguration is to be fetched separately via RDS.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider final
    : public ServerConfigSelectorProvider {
 public:
  DynamicXdsServerConfigSelectorProvider(
      RefCountedPtr<GrpcXdsClient> xds_client, std::string resource_name,
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          initial_resource,
      std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
          http_filters);

  ~DynamicXdsServerConfigSelectorProvider() override {
    xds_client_.reset(DEBUG_LOCATION, "DynamicXdsServerConfigSelectorProvider");
  }

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> Watch(
      std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
          watcher) override;
  void CancelWatch() override;

 private:
  class RouteConfigWatcher;

  void Orphaned() override;
  void OnRouteConfigChanged(
      std::shared_ptr<const XdsRouteConfigResource> rds_update);
  void OnError(absl::Status status);
  void OnResourceDoesNotExist();

  RefCountedPtr<GrpcXdsClient> xds_client_;
  std::string resource_name_;
  // TODO(roth): Consider holding a ref to the LDS resource and storing
  // a pointer to the HTTP filters within that LDS resource, rather than
  // copying the HTTP filters here.
  std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
      http_filters_;
  RouteConfigWatcher* route_config_watcher_ = nullptr;
  Mutex mu_;
  std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
      watcher_ ABSL_GUARDED_BY(mu_);
  absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>> resource_
      ABSL_GUARDED_BY(mu_);
};

// A watcher implementation for updating the RDS resource used by
// DynamicXdsServerConfigSelectorProvider
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::RouteConfigWatcher final
    : public XdsRouteConfigResourceType::WatcherInterface {
 public:
  explicit RouteConfigWatcher(
      WeakRefCountedPtr<DynamicXdsServerConfigSelectorProvider> parent)
      : parent_(std::move(parent)) {}

  void OnResourceChanged(
      std::shared_ptr<const XdsRouteConfigResource> route_config,
      RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) override {
    parent_->OnRouteConfigChanged(std::move(route_config));
  }

  void OnError(
      absl::Status status,
      RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) override {
    parent_->OnError(status);
  }

  void OnResourceDoesNotExist(
      RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) override {
    parent_->OnResourceDoesNotExist();
  }

 private:
  WeakRefCountedPtr<DynamicXdsServerConfigSelectorProvider> parent_;
};

//
// XdsServerConfigFetcher
//

XdsServerConfigFetcher::XdsServerConfigFetcher(
    RefCountedPtr<GrpcXdsClient> xds_client,
    grpc_server_xds_status_notifier notifier)
    : xds_client_(std::move(xds_client)), serving_status_notifier_(notifier) {
  CHECK(xds_client_ != nullptr);
}

std::string ListenerResourceName(absl::string_view resource_name_template,
                                 absl::string_view listening_address) {
  std::string tmp;
  if (absl::StartsWith(resource_name_template, "xdstp:")) {
    tmp = URI::PercentEncodePath(listening_address);
    listening_address = tmp;
  }
  return absl::StrReplaceAll(resource_name_template,
                             {{"%s", listening_address}});
}

void XdsServerConfigFetcher::StartWatch(
    std::string listening_address,
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface> watcher) {
  grpc_server_config_fetcher::WatcherInterface* watcher_ptr = watcher.get();
  auto listener_watcher = MakeRefCounted<ListenerWatcher>(
      xds_client_.Ref(DEBUG_LOCATION, "ListenerWatcher"), std::move(watcher),
      serving_status_notifier_, listening_address);
  auto* listener_watcher_ptr = listener_watcher.get();
  XdsListenerResourceType::StartWatch(
      xds_client_.get(),
      ListenerResourceName(
          static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
              .server_listener_resource_name_template(),
          listening_address),
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
    XdsListenerResourceType::CancelWatch(
        xds_client_.get(),
        ListenerResourceName(
            static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
                .server_listener_resource_name_template(),
            it->second->listening_address()),
        it->second, false /* delay_unsubscription */);
    listener_watchers_.erase(it);
  }
}

//
// XdsServerConfigFetcher::ListenerWatcher
//

XdsServerConfigFetcher::ListenerWatcher::ListenerWatcher(
    RefCountedPtr<GrpcXdsClient> xds_client,
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher,
    grpc_server_xds_status_notifier serving_status_notifier,
    std::string listening_address)
    : xds_client_(std::move(xds_client)),
      server_config_watcher_(std::move(server_config_watcher)),
      serving_status_notifier_(serving_status_notifier),
      listening_address_(std::move(listening_address)) {}

void XdsServerConfigFetcher::ListenerWatcher::OnResourceChanged(
    std::shared_ptr<const XdsListenerResource> listener,
    RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_server_config_fetcher)) {
    gpr_log(GPR_INFO,
            "[ListenerWatcher %p] Received LDS update from xds client %p: %s",
            this, xds_client_.get(), listener->ToString().c_str());
  }
  auto* tcp_listener =
      absl::get_if<XdsListenerResource::TcpListener>(&listener->listener);
  if (tcp_listener == nullptr) {
    MutexLock lock(&mu_);
    OnFatalError(
        absl::FailedPreconditionError("LDS resource is not a TCP listener"));
    return;
  }
  if (tcp_listener->address != listening_address_) {
    MutexLock lock(&mu_);
    OnFatalError(absl::FailedPreconditionError(
        "Address in LDS update does not match listening address"));
    return;
  }
  auto new_filter_chain_match_manager = MakeRefCounted<FilterChainMatchManager>(
      xds_client_.Ref(DEBUG_LOCATION, "FilterChainMatchManager"),
      tcp_listener->filter_chain_map, tcp_listener->default_filter_chain);
  MutexLock lock(&mu_);
  if (filter_chain_match_manager_ == nullptr ||
      !(new_filter_chain_match_manager->filter_chain_map() ==
            filter_chain_match_manager_->filter_chain_map() &&
        new_filter_chain_match_manager->default_filter_chain() ==
            filter_chain_match_manager_->default_filter_chain())) {
    pending_filter_chain_match_manager_ =
        std::move(new_filter_chain_match_manager);
    pending_filter_chain_match_manager_->StartRdsWatch(
        RefAsSubclass<ListenerWatcher>());
  }
}

void XdsServerConfigFetcher::ListenerWatcher::OnError(
    absl::Status status,
    RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) {
  MutexLock lock(&mu_);
  if (filter_chain_match_manager_ != nullptr ||
      pending_filter_chain_match_manager_ != nullptr) {
    gpr_log(GPR_ERROR,
            "ListenerWatcher:%p XdsClient reports error: %s for %s; "
            "ignoring in favor of existing resource",
            this, status.ToString().c_str(), listening_address_.c_str());
  } else {
    if (serving_status_notifier_.on_serving_status_update != nullptr) {
      serving_status_notifier_.on_serving_status_update(
          serving_status_notifier_.user_data, listening_address_.c_str(),
          {GRPC_STATUS_UNAVAILABLE, status.ToString().c_str()});
    } else {
      gpr_log(GPR_ERROR,
              "ListenerWatcher:%p error obtaining xDS Listener resource: %s; "
              "not serving on %s",
              this, status.ToString().c_str(), listening_address_.c_str());
    }
  }
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

void XdsServerConfigFetcher::ListenerWatcher::OnResourceDoesNotExist(
    RefCountedPtr<ReadDelayHandle> /* read_delay_handle */) {
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
        RefCountedPtr<GrpcXdsClient> xds_client,
        XdsListenerResource::FilterChainMap filter_chain_map,
        absl::optional<XdsListenerResource::FilterChainData>
            default_filter_chain)
    : xds_client_(std::move(xds_client)),
      filter_chain_map_(std::move(filter_chain_map)),
      default_filter_chain_(std::move(default_filter_chain)) {}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    StartRdsWatch(RefCountedPtr<ListenerWatcher> listener_watcher) {
  // Get the set of RDS resources to watch on. Also get the set of
  // FilterChainData so that we can reverse the list of HTTP filters since
  // received data moves *up* the stack in Core.
  std::set<std::string> resource_names;
  std::set<XdsListenerResource::FilterChainData*> filter_chain_data_set;
  for (const auto& destination_ip : filter_chain_map_.destination_ip_vector) {
    for (const auto& source_type : destination_ip.source_types_array) {
      for (const auto& source_ip : source_type) {
        for (const auto& source_port_pair : source_ip.ports_map) {
          auto* filter_chain_data = source_port_pair.second.data.get();
          const auto* rds_name = absl::get_if<std::string>(
              &filter_chain_data->http_connection_manager.route_config);
          if (rds_name != nullptr) resource_names.insert(*rds_name);
          filter_chain_data_set.insert(filter_chain_data);
        }
      }
    }
  }
  if (default_filter_chain_.has_value()) {
    auto& hcm = default_filter_chain_->http_connection_manager;
    const auto* rds_name = absl::get_if<std::string>(&hcm.route_config);
    if (rds_name != nullptr) resource_names.insert(*rds_name);
    std::reverse(hcm.http_filters.begin(), hcm.http_filters.end());
  }
  // Reverse the lists of HTTP filters in all the filter chains
  for (auto* filter_chain_data : filter_chain_data_set) {
    auto& hcm = filter_chain_data->http_connection_manager;
    std::reverse(hcm.http_filters.begin(), hcm.http_filters.end());
  }
  // Start watching on referenced RDS resources
  struct WatcherToStart {
    std::string resource_name;
    RefCountedPtr<RouteConfigWatcher> watcher;
  };
  std::vector<WatcherToStart> watchers_to_start;
  watchers_to_start.reserve(resource_names.size());
  {
    MutexLock lock(&mu_);
    for (const auto& resource_name : resource_names) {
      ++rds_resources_yet_to_fetch_;
      auto route_config_watcher = MakeRefCounted<RouteConfigWatcher>(
          resource_name, WeakRefAsSubclass<FilterChainMatchManager>());
      rds_map_.emplace(resource_name, RdsUpdateState{route_config_watcher.get(),
                                                     absl::nullopt});
      watchers_to_start.push_back(
          WatcherToStart{resource_name, std::move(route_config_watcher)});
    }
    if (rds_resources_yet_to_fetch_ != 0) {
      listener_watcher_ = std::move(listener_watcher);
      listener_watcher = nullptr;
    }
  }
  for (auto& watcher_to_start : watchers_to_start) {
    XdsRouteConfigResourceType::StartWatch(xds_client_.get(),
                                           watcher_to_start.resource_name,
                                           std::move(watcher_to_start.watcher));
  }
  // Promote this filter chain match manager if all referenced resources are
  // fetched.
  if (listener_watcher != nullptr) {
    listener_watcher->PendingFilterChainMatchManagerReadyLocked(this);
  }
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    Orphaned() {
  MutexLock lock(&mu_);
  // Cancel the RDS watches to clear up the weak refs
  for (const auto& entry : rds_map_) {
    XdsRouteConfigResourceType::CancelWatch(xds_client_.get(), entry.first,
                                            entry.second.watcher,
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
  if (it != certificate_providers_map_.end()) return it->second;
  // Configure root cert.
  absl::string_view root_provider_instance_name =
      filter_chain->downstream_tls_context.common_tls_context
          .certificate_validation_context.ca_certificate_provider_instance
          .instance_name;
  absl::string_view root_provider_cert_name =
      filter_chain->downstream_tls_context.common_tls_context
          .certificate_validation_context.ca_certificate_provider_instance
          .certificate_name;
  RefCountedPtr<grpc_tls_certificate_provider> root_cert_provider;
  if (!root_provider_instance_name.empty()) {
    root_cert_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(root_provider_instance_name);
    if (root_cert_provider == nullptr) {
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
  RefCountedPtr<grpc_tls_certificate_provider> identity_cert_provider;
  if (!identity_provider_instance_name.empty()) {
    identity_cert_provider =
        xds_client_->certificate_provider_store()
            .CreateOrGetCertificateProvider(identity_provider_instance_name);
    if (identity_cert_provider == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Certificate provider instance name: \"",
                       identity_provider_instance_name, "\" not recognized."));
    }
  }
  auto xds_cert_provider = MakeRefCounted<XdsCertificateProvider>(
      std::move(root_cert_provider), root_provider_cert_name,
      std::move(identity_cert_provider), identity_provider_cert_name,
      filter_chain->downstream_tls_context.require_client_certificate);
  certificate_providers_map_.emplace(filter_chain, xds_cert_provider);
  return xds_cert_provider;
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    OnRouteConfigChanged(
        const std::string& resource_name,
        std::shared_ptr<const XdsRouteConfigResource> route_config) {
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
    const std::string& resource_name, absl::Status status) {
  RefCountedPtr<ListenerWatcher> listener_watcher;
  {
    MutexLock lock(&mu_);
    auto& state = rds_map_[resource_name];
    if (!state.rds_update.has_value()) {
      if (--rds_resources_yet_to_fetch_ == 0) {
        listener_watcher = std::move(listener_watcher_);
      }
      state.rds_update = status;
    } else {
      // Prefer existing good version over current errored version
      if (!state.rds_update->ok()) {
        state.rds_update = status;
      }
    }
  }
  // Promote the filter chain match manager object if all the referenced
  // resources are fetched.
  if (listener_watcher != nullptr) {
    listener_watcher->PendingFilterChainMatchManagerReady(this);
  }
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
  auto source_addr = StringToSockaddr(host, 0);  // Port doesn't matter here.
  if (!source_addr.ok()) {
    gpr_log(GPR_DEBUG, "Could not parse \"%s\" as socket address: %s",
            host.c_str(), source_addr.status().ToString().c_str());
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
        &*source_addr, port);
  }
  if (IsLoopbackIp(&*source_addr) || host == destination_ip) {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsListenerResource::FilterChainMap::ConnectionSourceType::
                kSameIpOrLoopback)],
        &*source_addr, port);
  } else {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsListenerResource::FilterChainMap::ConnectionSourceType::
                kExternal)],
        &*source_addr, port);
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
  auto destination_addr =
      StringToSockaddr(host, 0);  // Port doesn't matter here.
  if (!destination_addr.ok()) {
    gpr_log(GPR_DEBUG, "Could not parse \"%s\" as socket address: %s",
            host.c_str(), destination_addr.status().ToString().c_str());
    return nullptr;
  }
  const XdsListenerResource::FilterChainMap::DestinationIp* best_match =
      nullptr;
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
    if (grpc_sockaddr_match_subnet(&*destination_addr,
                                   &entry.prefix_range->address,
                                   entry.prefix_range->prefix_len)) {
      best_match = &entry;
    }
  }
  if (best_match == nullptr) return nullptr;
  return FindFilterChainDataForSourceType(best_match->source_types_array, tcp,
                                          host);
}

absl::StatusOr<ChannelArgs> XdsServerConfigFetcher::ListenerWatcher::
    FilterChainMatchManager::UpdateChannelArgsForConnection(
        const ChannelArgs& input_args, grpc_endpoint* tcp) {
  ChannelArgs args = input_args;
  const auto* filter_chain = FindFilterChainDataForDestinationIp(
      filter_chain_map_.destination_ip_vector, tcp);
  if (filter_chain == nullptr && default_filter_chain_.has_value()) {
    filter_chain = &default_filter_chain_.value();
  }
  if (filter_chain == nullptr) {
    return absl::UnavailableError("No matching filter chain found");
  }
  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider;
  RefCountedPtr<XdsChannelStackModifier> channel_stack_modifier;
  RefCountedPtr<XdsCertificateProvider> xds_certificate_provider;
  // Iterate the list of HTTP filters in reverse since in Core, received data
  // flows *up* the stack.
  std::vector<const grpc_channel_filter*> filters;
  const auto& http_filter_registry =
      static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
          .http_filter_registry();
  for (const auto& http_filter :
       filter_chain->http_connection_manager.http_filters) {
    // Find filter.  This is guaranteed to succeed, because it's checked
    // at config validation time in the XdsApi code.
    const XdsHttpFilterImpl* filter_impl =
        http_filter_registry.GetFilterForType(
            http_filter.config.config_proto_type_name);
    CHECK_NE(filter_impl, nullptr);
    // Some filters like the router filter are no-op filters and do not have
    // an implementation.
    if (filter_impl->channel_filter() != nullptr) {
      filters.push_back(filter_impl->channel_filter());
    }
  }
  // Add config selector filter.
  filters.push_back(&kServerConfigSelectorFilter);
  channel_stack_modifier =
      MakeRefCounted<XdsChannelStackModifier>(std::move(filters));
  Match(
      filter_chain->http_connection_manager.route_config,
      // RDS resource name
      [&](const std::string& rds_name) {
        absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
            initial_resource;
        {
          MutexLock lock(&mu_);
          initial_resource = rds_map_[rds_name].rds_update.value();
        }
        server_config_selector_provider =
            MakeRefCounted<DynamicXdsServerConfigSelectorProvider>(
                xds_client_.Ref(DEBUG_LOCATION,
                                "DynamicXdsServerConfigSelectorProvider"),
                rds_name, std::move(initial_resource),
                filter_chain->http_connection_manager.http_filters);
      },
      // inline RouteConfig
      [&](const std::shared_ptr<const XdsRouteConfigResource>& route_config) {
        server_config_selector_provider =
            MakeRefCounted<StaticXdsServerConfigSelectorProvider>(
                xds_client_.Ref(DEBUG_LOCATION,
                                "StaticXdsServerConfigSelectorProvider"),
                route_config,
                filter_chain->http_connection_manager.http_filters);
      });
  args = args.SetObject(server_config_selector_provider)
             .SetObject(channel_stack_modifier);
  // Add XdsCertificateProvider if credentials are xDS.
  auto* server_creds = args.GetObject<grpc_server_credentials>();
  if (server_creds != nullptr &&
      server_creds->type() == XdsServerCredentials::Type()) {
    absl::StatusOr<RefCountedPtr<XdsCertificateProvider>> result =
        CreateOrGetXdsCertificateProviderFromFilterChainData(filter_chain);
    if (!result.ok()) {
      return result.status();
    }
    xds_certificate_provider = std::move(*result);
    CHECK(xds_certificate_provider != nullptr);
    args = args.SetObject(xds_certificate_provider);
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
        const XdsHttpFilterRegistry& http_filter_registry,
        std::shared_ptr<const XdsRouteConfigResource> rds_update,
        const std::vector<
            XdsListenerResource::HttpConnectionManager::HttpFilter>&
            http_filters) {
  auto config_selector = MakeRefCounted<XdsServerConfigSelector>();
  for (auto& vhost : rds_update->virtual_hosts) {
    config_selector->virtual_hosts_.emplace_back();
    auto& virtual_host = config_selector->virtual_hosts_.back();
    virtual_host.domains = vhost.domains;
    for (auto& route : vhost.routes) {
      virtual_host.routes.emplace_back();
      auto& config_selector_route = virtual_host.routes.back();
      config_selector_route.matchers = route.matchers;
      config_selector_route.unsupported_action =
          absl::get_if<XdsRouteConfigResource::Route::NonForwardingAction>(
              &route.action) == nullptr;
      auto result = XdsRouting::GeneratePerHTTPFilterConfigs(
          http_filter_registry, http_filters, vhost, route, nullptr,
          ChannelArgs());
      if (!result.ok()) return result.status();
      std::vector<std::string> fields;
      fields.reserve(result->per_filter_configs.size());
      for (const auto& p : result->per_filter_configs) {
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
        config_selector_route.method_config =
            ServiceConfigImpl::Create(result->args, json.c_str()).value();
      }
    }
  }
  return config_selector;
}

absl::StatusOr<ServerConfigSelector::CallConfig>
XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    XdsServerConfigSelector::GetCallConfig(grpc_metadata_batch* metadata) {
  CallConfig call_config;
  if (metadata->get_pointer(HttpPathMetadata()) == nullptr) {
    return absl::InternalError("no path found");
  }
  absl::string_view path =
      metadata->get_pointer(HttpPathMetadata())->as_string_view();
  if (metadata->get_pointer(HttpAuthorityMetadata()) == nullptr) {
    return absl::InternalError("no authority found");
  }
  absl::string_view authority =
      metadata->get_pointer(HttpAuthorityMetadata())->as_string_view();
  auto vhost_index = XdsRouting::FindVirtualHostForDomain(
      VirtualHostListIterator(&virtual_hosts_), authority);
  if (!vhost_index.has_value()) {
    return absl::UnavailableError(
        absl::StrCat("could not find VirtualHost for ", authority,
                     " in RouteConfiguration"));
  }
  auto& virtual_host = virtual_hosts_[vhost_index.value()];
  auto route_index = XdsRouting::GetRouteForRequest(
      VirtualHost::RouteListIterator(&virtual_host.routes), path, metadata);
  if (route_index.has_value()) {
    auto& route = virtual_host.routes[route_index.value()];
    // Found the matching route
    if (route.unsupported_action) {
      return absl::UnavailableError("matching route has unsupported action");
    }
    if (route.method_config != nullptr) {
      call_config.method_configs =
          route.method_config->GetMethodParsedConfigVector(grpc_empty_slice());
      call_config.service_config = route.method_config;
    }
    return call_config;
  }
  return absl::UnavailableError("no route matched");
}

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::DynamicXdsServerConfigSelectorProvider
//

XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::
        DynamicXdsServerConfigSelectorProvider(
            RefCountedPtr<GrpcXdsClient> xds_client, std::string resource_name,
            absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
                initial_resource,
            std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>
                http_filters)
    : xds_client_(std::move(xds_client)),
      resource_name_(std::move(resource_name)),
      http_filters_(std::move(http_filters)),
      resource_(std::move(initial_resource)) {
  CHECK(!resource_name_.empty());
  // RouteConfigWatcher is being created here instead of in Watch() to avoid
  // deadlocks from invoking XdsRouteConfigResourceType::StartWatch whilst in a
  // critical region.
  auto route_config_watcher = MakeRefCounted<RouteConfigWatcher>(
      WeakRefAsSubclass<DynamicXdsServerConfigSelectorProvider>());
  route_config_watcher_ = route_config_watcher.get();
  XdsRouteConfigResourceType::StartWatch(xds_client_.get(), resource_name_,
                                         std::move(route_config_watcher));
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::Orphaned() {
  XdsRouteConfigResourceType::CancelWatch(xds_client_.get(), resource_name_,
                                          route_config_watcher_,
                                          false /* delay_unsubscription */);
}

absl::StatusOr<RefCountedPtr<ServerConfigSelector>>
XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::Watch(
        std::unique_ptr<
            ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
            watcher) {
  absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>> resource;
  {
    MutexLock lock(&mu_);
    CHECK(watcher_ == nullptr);
    watcher_ = std::move(watcher);
    resource = resource_;
  }
  if (!resource.ok()) {
    return resource.status();
  }
  return XdsServerConfigSelector::Create(
      static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
          .http_filter_registry(),
      resource.value(), http_filters_);
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::CancelWatch() {
  MutexLock lock(&mu_);
  watcher_.reset();
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::OnRouteConfigChanged(
        std::shared_ptr<const XdsRouteConfigResource> rds_update) {
  MutexLock lock(&mu_);
  resource_ = std::move(rds_update);
  if (watcher_ == nullptr) {
    return;
  }
  // Currently server_config_selector_filter does not call into
  // DynamicXdsServerConfigSelectorProvider while holding a lock, but if that
  // ever changes, we would want to invoke the update outside the critical
  // region with the use of a WorkSerializer.
  watcher_->OnServerConfigSelectorUpdate(XdsServerConfigSelector::Create(
      static_cast<const GrpcXdsBootstrap&>(xds_client_->bootstrap())
          .http_filter_registry(),
      *resource_, http_filters_));
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    DynamicXdsServerConfigSelectorProvider::OnError(absl::Status status) {
  MutexLock lock(&mu_);
  // Prefer existing good update.
  if (resource_.ok()) {
    return;
  }
  resource_ = status;
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
  grpc_core::ChannelArgs channel_args = grpc_core::CoreConfiguration::Get()
                                            .channel_args_preconditioning()
                                            .PreconditionChannelArgs(args);
  GRPC_API_TRACE(
      "grpc_server_config_fetcher_xds_create(notifier={on_serving_status_"
      "update=%p, user_data=%p}, args=%p)",
      3, (notifier.on_serving_status_update, notifier.user_data, args));
  auto xds_client = grpc_core::GrpcXdsClient::GetOrCreate(
      grpc_core::GrpcXdsClient::kServerKey, channel_args,
      "XdsServerConfigFetcher");
  if (!xds_client.ok()) {
    gpr_log(GPR_ERROR, "Failed to create xds client: %s",
            xds_client.status().ToString().c_str());
    return nullptr;
  }
  if (static_cast<const grpc_core::GrpcXdsBootstrap&>(
          (*xds_client)->bootstrap())
          .server_listener_resource_name_template()
          .empty()) {
    gpr_log(GPR_ERROR,
            "server_listener_resource_name_template not provided in bootstrap "
            "file.");
    return nullptr;
  }
  return new grpc_core::XdsServerConfigFetcher(std::move(*xds_client),
                                               notifier);
}
