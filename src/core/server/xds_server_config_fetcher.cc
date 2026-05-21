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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/tls/grpc_tls_certificate_distributor.h"
#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/credentials/transport/xds/xds_credentials.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/server/server.h"
#include "src/core/server/server_config_selector.h"
#include "src/core/server/server_config_selector_filter.h"
#include "src/core/server/xds_channel_stack_modifier.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/host_port.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/uri.h"
#include "src/core/xds/grpc/certificate_provider_store.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_certificate_provider.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_listener_parser.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "src/core/xds/grpc/xds_route_config_parser.h"
#include "src/core/xds/grpc/xds_routing.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ReadDelayHandle = XdsClient::ReadDelayHandle;

// A server config fetcher that fetches the information for configuring server
// listeners from the xDS control plane.
class XdsServerConfigFetcher final : public ServerConfigFetcher {
 public:
  XdsServerConfigFetcher(RefCountedPtr<GrpcXdsClient> xds_client,
                         grpc_server_xds_status_notifier notifier);

  ~XdsServerConfigFetcher() override {
LOG(INFO) << "~XdsServerConfigFetcher()";
    xds_client_.reset(DEBUG_LOCATION, "XdsServerConfigFetcher");
  }

  void StartWatch(
      std::string listening_address,
      std::unique_ptr<ServerConfigFetcher::WatcherInterface> watcher) override;

  void CancelWatch(ServerConfigFetcher::WatcherInterface* watcher) override;

  // Return the interested parties from the xds client so that it can be polled.
  grpc_pollset_set* interested_parties() override {
    return xds_client_->interested_parties();
  }

 private:
  class ListenerWatcher;

  RefCountedPtr<GrpcXdsClient> xds_client_;
  const grpc_server_xds_status_notifier serving_status_notifier_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::map<ServerConfigFetcher::WatcherInterface*, ListenerWatcher*>
      listener_watchers_ ABSL_GUARDED_BY(*work_serializer_);
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
  ListenerWatcher(RefCountedPtr<XdsServerConfigFetcher> config_fetcher,
                  std::unique_ptr<ServerConfigFetcher::WatcherInterface>
                      server_config_watcher,
                  grpc_server_xds_status_notifier serving_status_notifier,
                  std::string listening_address);

  ~ListenerWatcher() override {
LOG(INFO) << "~ListenerWatcher() " << this;
    config_fetcher_.reset(DEBUG_LOCATION, "ListenerWatcher");
  }

  void OnResourceChanged(
      absl::StatusOr<std::shared_ptr<const XdsListenerResource>> listener,
      RefCountedPtr<ReadDelayHandle> read_delay_handle) override;

  void OnAmbientError(
      absl::Status status,
      RefCountedPtr<ReadDelayHandle> read_delay_handle) override;

  const std::string& listening_address() const { return listening_address_; }

 private:
  class FilterChainMatchManager;

  void OnFatalError(absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*config_fetcher_->work_serializer_);

  // Invoked by FilterChainMatchManager that is done fetching all referenced RDS
  // resources. If the calling FilterChainMatchManager is the
  // pending_filter_chain_match_manager_, it is promoted to be the
  // filter_chain_match_manager_ in use.
  void PendingFilterChainMatchManagerReady(
      FilterChainMatchManager* filter_chain_match_manager)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*config_fetcher_->work_serializer_);

  RefCountedPtr<XdsServerConfigFetcher> config_fetcher_;
  const std::unique_ptr<ServerConfigFetcher::WatcherInterface>
      server_config_watcher_;
  const grpc_server_xds_status_notifier serving_status_notifier_;
  const std::string listening_address_;

  RefCountedPtr<FilterChainMatchManager> filter_chain_match_manager_
      ABSL_GUARDED_BY(*config_fetcher_->work_serializer_);
  RefCountedPtr<FilterChainMatchManager> pending_filter_chain_match_manager_
      ABSL_GUARDED_BY(*config_fetcher_->work_serializer_);
};

// A connection manager used by the server listener code to inject channel args
// to be used for each incoming connection. This implementation chooses the
// appropriate filter chain from the xDS Listener resource and injects channel
// args that configure the right mTLS certs and cause the right set of HTTP
// filters to be injected.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager final
    : public ServerConfigFetcher::ConnectionManager {
 public:
  FilterChainMatchManager(
      RefCountedPtr<XdsServerConfigFetcher> config_fetcher,
      RefCountedPtr<ListenerWatcher> listener_watcher,
      XdsListenerResource::FilterChainMap filter_chain_map,
      std::optional<XdsListenerResource::FilterChainData> default_filter_chain);

  ~FilterChainMatchManager() override {
    config_fetcher_.reset(DEBUG_LOCATION, "FilterChainMatchManager");
  }

  void Start();

  absl::StatusOr<ChannelArgs> UpdateChannelArgsForConnection(
      const ChannelArgs& args, grpc_endpoint* tcp) override;

  const XdsListenerResource::FilterChainMap& filter_chain_map() const {
    return filter_chain_map_;
  }

  const std::optional<XdsListenerResource::FilterChainData>&
  default_filter_chain() const {
    return default_filter_chain_;
  }

 private:
  class L4FilterChain;

  void Orphaned() override;

  // Checks if all L4FilterChains have their RouteConfig.  If so,
  // promotes this FilterChainMatchManager to be the current one.
  void MaybePromote()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*config_fetcher_->work_serializer_);

  // Executes cb once for each unique filter chain object in the LDS resource.
  void ForEachFilterChain(
      absl::FunctionRef<void(const XdsListenerResource::FilterChainData&)> cb)
      const;

  RefCountedPtr<XdsServerConfigFetcher> config_fetcher_;
  // This ref is only kept around until the FilterChainMatchManager becomes
  // ready.
  // TODO(roth): Consider adding a separate object to represent the
  // listener's lifetime, separate from the XdsClient listener watcher,
  // so that we don't need to give up this ref early (in which case we
  // wouldn't need to hold a ref to the config fetcher at all).
  RefCountedPtr<ListenerWatcher> listener_watcher_;

  // TODO(roth): Consider holding a ref to the LDS resource and storing
  // a pointer to the filter chain data within that LDS resource, rather
  // than copying the filter chain data here.
  const XdsListenerResource::FilterChainMap filter_chain_map_;
  const std::optional<XdsListenerResource::FilterChainData>
      default_filter_chain_;

  // TODO(roth): Consider refactoring the FilterChainMap data structure
  // such that we construct it here rather than at config parsing time,
  // so that we don't need to maintain two different data structures
  // here (first we find the FilterChainData object in FilterChainMap,
  // and then we look up that FilterChainData object in this map).
  std::map<const XdsListenerResource::FilterChainData*,
           OrphanablePtr<L4FilterChain>>
      l4_filter_chains_;
};

class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain final : public InternallyRefCounted<L4FilterChain> {
 public:
  L4FilterChain(
      WeakRefCountedPtr<FilterChainMatchManager> filter_chain_match_manager,
      const XdsListenerResource::FilterChainData& filter_chain_data);

  void Orphan() override ABSL_EXCLUSIVE_LOCKS_REQUIRED(
      *filter_chain_match_manager_->config_fetcher_->work_serializer_);

  bool HasRouteConfig() const { return config_selector_provider_ != nullptr; }

  absl::StatusOr<ChannelArgs> UpdateChannelArgsForConnection(
      const ChannelArgs& args) const;

 private:
  class XdsServerConfigSelector;
  class XdsServerConfigSelectorProvider;
  class RouteConfigWatcher;

  void OnRouteConfigChanged(
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          route_config)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(
          *filter_chain_match_manager_->config_fetcher_->work_serializer_);
  void OnAmbientError(absl::Status status) ABSL_EXCLUSIVE_LOCKS_REQUIRED(
      *filter_chain_match_manager_->config_fetcher_->work_serializer_);

  void UpdateServerConfigSelector(
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          route_config)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(
          *filter_chain_match_manager_->config_fetcher_->work_serializer_);

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>>
  CreateServerConfigSelector(
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          route_config)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(
          *filter_chain_match_manager_->config_fetcher_->work_serializer_);

  WeakRefCountedPtr<FilterChainMatchManager> filter_chain_match_manager_;

  // Reference into data owned by FilterChainMatchManager.
  const XdsListenerResource::FilterChainData& filter_chain_data_;

  absl::StatusOr<RefCountedPtr<XdsCertificateProvider>> certificate_provider_;

  RouteConfigWatcher* watcher_ = nullptr;

  // FIXME: add
  //RefCountedPtr<Blackboard> blackboard_;

  // Will be null until we get the initial RouteConfiguration.
  // No need for synchronization, because this will always be set before
  // we start being used for incoming connections.
  RefCountedPtr<XdsServerConfigSelectorProvider> config_selector_provider_;
};

// An implementation of ServerConfigSelector that stores parsed xDS filter
// configs for each route, constructs filter chains for each, and
// selects the appropriate filter chain for each RPC.
class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::XdsServerConfigSelector final : public ServerConfigSelector {
 public:
  static absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>> Create(
      const XdsHttpFilterRegistry& http_filter_registry,
      std::shared_ptr<const XdsRouteConfigResource> rds_update,
      const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
          http_filters);

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

//
// XdsServerConfigFetcher
//

XdsServerConfigFetcher::XdsServerConfigFetcher(
    RefCountedPtr<GrpcXdsClient> xds_client,
    grpc_server_xds_status_notifier notifier)
    : xds_client_(std::move(xds_client)),
      serving_status_notifier_(notifier),
      work_serializer_(std::make_shared<WorkSerializer>(
          grpc_event_engine::experimental::GetDefaultEventEngine())) {
  GRPC_CHECK(xds_client_ != nullptr);
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
    std::unique_ptr<ServerConfigFetcher::WatcherInterface> watcher) {
  work_serializer_->Run([self = RefAsSubclass<XdsServerConfigFetcher>(),
                         listening_address = std::move(listening_address),
                         watcher = std::move(watcher)]() mutable {
    std::string resource_name = ListenerResourceName(
        DownCast<const GrpcXdsBootstrap&>(self->xds_client_->bootstrap())
            .server_listener_resource_name_template(),
        listening_address);
    GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
        << "[XdsServerConfigFetcher " << self.get()
        << "]: starting watch for LDS resource " << resource_name;
    ServerConfigFetcher::WatcherInterface* watcher_ptr = watcher.get();
    auto listener_watcher = MakeRefCounted<ListenerWatcher>(
        self->RefAsSubclass<XdsServerConfigFetcher>(DEBUG_LOCATION,
                                                    "ListenerWatcher"),
        std::move(watcher), self->serving_status_notifier_, listening_address);
    auto* listener_watcher_ptr = listener_watcher.get();
    XdsListenerResourceType::StartWatch(self->xds_client_.get(), resource_name,
                                        std::move(listener_watcher));
    self->listener_watchers_.emplace(watcher_ptr, listener_watcher_ptr);
  });
}

void XdsServerConfigFetcher::CancelWatch(
    ServerConfigFetcher::WatcherInterface* watcher) {
  work_serializer_->Run(
      [self = RefAsSubclass<XdsServerConfigFetcher>(), watcher]() {
        auto it = self->listener_watchers_.find(watcher);
        if (it == self->listener_watchers_.end()) return;
        std::string resource_name = ListenerResourceName(
            DownCast<const GrpcXdsBootstrap&>(self->xds_client_->bootstrap())
                .server_listener_resource_name_template(),
            it->second->listening_address());
        GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
            << "[XdsServerConfigFetcher " << self.get()
            << "]: cancelling watch for LDS resource " << resource_name;
        XdsListenerResourceType::CancelWatch(self->xds_client_.get(),
                                             resource_name, it->second,
                                             /*delay_unsubscription=*/false);
        self->listener_watchers_.erase(it);
      });
}

//
// XdsServerConfigFetcher::ListenerWatcher
//

XdsServerConfigFetcher::ListenerWatcher::ListenerWatcher(
    RefCountedPtr<XdsServerConfigFetcher> config_fetcher,
    std::unique_ptr<ServerConfigFetcher::WatcherInterface>
        server_config_watcher,
    grpc_server_xds_status_notifier serving_status_notifier,
    std::string listening_address)
    : config_fetcher_(std::move(config_fetcher)),
      server_config_watcher_(std::move(server_config_watcher)),
      serving_status_notifier_(serving_status_notifier),
      listening_address_(std::move(listening_address)) {
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[ListenerWatcher " << this << "]: created for address "
      << listening_address_;
}

void XdsServerConfigFetcher::ListenerWatcher::OnResourceChanged(
    absl::StatusOr<std::shared_ptr<const XdsListenerResource>> listener,
    RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  config_fetcher_->work_serializer_->Run(
      [self = RefAsSubclass<ListenerWatcher>(), listener = std::move(listener),
       read_delay_handle = std::move(read_delay_handle)]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(
              *config_fetcher_->work_serializer_) mutable {
            if (!listener.ok()) {
              self->OnFatalError(absl::Status(
                  listener.status().code(),
                  absl::StrCat("LDS resource: ", listener.status().message())));
              return;
            }
            GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
                << "[ListenerWatcher " << self.get()
                << "] Received LDS update from xds client "
                << self->config_fetcher_->xds_client_.get() << ": "
                << (*listener)->ToString();
            auto* tcp_listener = std::get_if<XdsListenerResource::TcpListener>(
                &(*listener)->listener);
            if (tcp_listener == nullptr) {
              self->OnFatalError(absl::FailedPreconditionError(
                  "LDS resource is not a TCP listener"));
              return;
            }
            if (tcp_listener->address != self->listening_address_) {
              self->OnFatalError(absl::FailedPreconditionError(
                  "Address in LDS update does not match listening address"));
              return;
            }
            if (self->filter_chain_match_manager_ != nullptr &&
                tcp_listener->filter_chain_map ==
                    self->filter_chain_match_manager_->filter_chain_map() &&
                tcp_listener->default_filter_chain ==
                    self->filter_chain_match_manager_->default_filter_chain()) {
              GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
                  << "[ListenerWatcher " << self.get()
                  << "]: ignoring unchanged LDS resource for "
                  << self->listening_address_;
              return;
            }
            self->pending_filter_chain_match_manager_ =
                MakeRefCounted<FilterChainMatchManager>(
                    self->config_fetcher_.Ref(DEBUG_LOCATION,
                                              "FilterChainMatchManager"),
                    self, tcp_listener->filter_chain_map,
                    tcp_listener->default_filter_chain);
            self->pending_filter_chain_match_manager_->Start();
          });
}

void XdsServerConfigFetcher::ListenerWatcher::OnAmbientError(
    absl::Status status, RefCountedPtr<ReadDelayHandle> /*read_delay_handle*/) {
  LOG(ERROR)
      << "[ListenerWatcher:" << this
      << "] XdsClient reports ambient error for LDS resource for address "
      << listening_address_ << ": " << status;
}

void XdsServerConfigFetcher::ListenerWatcher::OnFatalError(absl::Status status)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(*config_fetcher_->work_serializer_) {
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
    LOG(ERROR) << "[ListenerWatcher:" << this << "] Encountered fatal error "
               << status << "; not serving on " << listening_address_;
  }
}

void XdsServerConfigFetcher::ListenerWatcher::
    PendingFilterChainMatchManagerReady(
        XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager*
            filter_chain_match_manager)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(*config_fetcher_->work_serializer_) {
  if (pending_filter_chain_match_manager_ != filter_chain_match_manager) {
    // This FilterChainMatchManager is no longer the current pending resource.
    // It should get cleaned up eventually. Ignore this update.
    return;
  }
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[ListenerWatcher " << this << "]: promoting FilterChainMatchManager "
      << filter_chain_match_manager;
  bool first_good_update = filter_chain_match_manager_ == nullptr;
  // Promote the pending FilterChainMatchManager
  filter_chain_match_manager_ = std::move(pending_filter_chain_match_manager_);
  server_config_watcher_->UpdateConnectionManager(filter_chain_match_manager_);
  // Let the logger know about the update if there was no previous good update.
  if (first_good_update) {
    if (serving_status_notifier_.on_serving_status_update != nullptr) {
      serving_status_notifier_.on_serving_status_update(
          serving_status_notifier_.user_data, listening_address_.c_str(),
          {GRPC_STATUS_OK, ""});
    } else {
      LOG(INFO) << "xDS Listener resource obtained; will start serving on "
                << listening_address_;
    }
  }
}

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager
//

XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    FilterChainMatchManager(
        RefCountedPtr<XdsServerConfigFetcher> config_fetcher,
        RefCountedPtr<ListenerWatcher> listener_watcher,
        XdsListenerResource::FilterChainMap filter_chain_map,
        std::optional<XdsListenerResource::FilterChainData>
            default_filter_chain)
    : config_fetcher_(std::move(config_fetcher)),
      listener_watcher_(std::move(listener_watcher)),
      filter_chain_map_(std::move(filter_chain_map)),
      default_filter_chain_(std::move(default_filter_chain)) {
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[FilterChainMatchManager " << this << "]: created";
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::Start() {
  // Create L4FilterChain object for each L4 filter chain.
  bool ready = true;
  ForEachFilterChain(
      [&](const XdsListenerResource::FilterChainData& filter_chain_data) {
        auto l4_filter_chain = MakeOrphanable<L4FilterChain>(
            WeakRefAsSubclass<FilterChainMatchManager>(), filter_chain_data);
        if (!l4_filter_chain->HasRouteConfig()) ready = false;
        l4_filter_chains_.emplace(&filter_chain_data,
                                  std::move(l4_filter_chain));
      });
  // If all L4 filter chains had inline RouteConfigs, then promote this
  // FilterChainMatchManager immediately.
  if (ready) {
    GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
        << "[FilterChainMatchManager " << this << "]: have all RouteConfigs";
    listener_watcher_->PendingFilterChainMatchManagerReady(this);
    listener_watcher_.reset();
  }
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    ForEachFilterChain(
        absl::FunctionRef<void(const XdsListenerResource::FilterChainData&)> cb)
        const {
  // A given FilterChainData object may appear more than once in the map,
  // but we want to invoke the callback exactly once for each
  // FilterChainData object.  Therefore, we start by constructing a set
  // of all FilterChainData objects by address, to weed out duplicates.
  std::set<const XdsListenerResource::FilterChainData*> filter_chain_data_set;
  for (const auto& destination_ip : filter_chain_map_.destination_ip_vector) {
    for (const auto& source_type : destination_ip.source_types_array) {
      for (const auto& source_ip : source_type) {
        for (const auto& source_port_pair : source_ip.ports_map) {
          auto* filter_chain_data = source_port_pair.second.data.get();
          filter_chain_data_set.insert(filter_chain_data);
        }
      }
    }
  }
  // Invoke the callback once for each FilterChainData object.
  for (auto* filter_chain_data : filter_chain_data_set) {
    cb(*filter_chain_data);
  }
  // Also invoke the callback for the default filter chain, if present.
  if (default_filter_chain_.has_value()) {
    cb(*default_filter_chain_);
  }
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    Orphaned() {
  config_fetcher_->work_serializer_->Run(
      [self = WeakRefAsSubclass<FilterChainMatchManager>()]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(*config_fetcher_->work_serializer_) {
            GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
                << "[FilterChainMatchManager " << self.get() << "]: orphaned";
            self->listener_watcher_.reset();
            self->l4_filter_chains_.clear();
          });
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    MaybePromote()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(*config_fetcher_->work_serializer_) {
  if (listener_watcher_ == nullptr) return;  // Already promoted.
  RefCountedPtr<ListenerWatcher> listener_watcher;
  // Check if all L4 filter chains have their route configs.
  for (const auto& [_, l4_filter_chain] : l4_filter_chains_) {
    if (!l4_filter_chain->HasRouteConfig()) return;
  }
  // We have all route configs, so promote the FilterChainMatchManager.
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[FilterChainMatchManager " << this
      << "]: obtained all necessary RDS resources";
  listener_watcher_->PendingFilterChainMatchManagerReady(this);
  listener_watcher_.reset();
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
    VLOG(2) << "Could not parse \"" << host
            << "\" as socket address: " << source_addr.status();
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
    VLOG(2) << "Could not parse \"" << host
            << "\" as socket address: " << destination_addr.status();
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
        const ChannelArgs& args, grpc_endpoint* tcp) {
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[FilterChainMatchManager " << this
      << "]: choosing L4 filter chain for connection";
  const auto* filter_chain = FindFilterChainDataForDestinationIp(
      filter_chain_map_.destination_ip_vector, tcp);
  if (filter_chain == nullptr && default_filter_chain_.has_value()) {
    filter_chain = &*default_filter_chain_;
  }
  if (filter_chain == nullptr) {
    return absl::UnavailableError("No matching filter chain found");
  }
  // Find the corresponding L4FilterChain.
  auto it = l4_filter_chains_.find(filter_chain);
  if (it == l4_filter_chains_.end()) {
    // Should never happen.
    return absl::InternalError("could not find L4FilterChain");
  }
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[FilterChainMatchManager " << this << "]: selected L4 filter chain "
      << it->second.get();
  return it->second->UpdateChannelArgsForConnection(args);
}

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::L4FilterChain::XdsServerConfigSelectorProvider
//

class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::XdsServerConfigSelectorProvider final
    : public ServerConfigSelectorProvider {
 public:
  XdsServerConfigSelectorProvider(
      std::shared_ptr<WorkSerializer> work_serializer,
      absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector)
      : work_serializer_(std::move(work_serializer)),
        config_selector_(std::move(config_selector)) {}

  void SetServerConfigSelector(
      absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector) {
    if (GRPC_TRACE_FLAG_ENABLED(xds_server_config_fetcher)) {
      if (config_selector.ok()) {
        LOG(INFO) << "[XdsServerConfigSelectorProvider " << this
                  << "]: sending new XdsServerConfigSelector "
                  << config_selector->get();
      } else {
        LOG(INFO) << "[XdsServerConfigSelectorProvider " << this
                  << "]: sending new XdsServerConfigSelector "
                  << config_selector.status();
      }
    }
    config_selector_ = std::move(config_selector);
    for (const auto& watcher : watchers_) {
      watcher->OnServerConfigSelectorUpdate(config_selector_);
    }
  }

  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> Watch(
      std::shared_ptr<ServerConfigSelectorWatcher> watcher) override {
    work_serializer_->Run(
        [self = WeakRefAsSubclass<XdsServerConfigSelectorProvider>(),
         watcher = std::move(watcher)]() mutable {
          GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
              << "[XdsServerConfigSelectorProvider " << self.get()
              << "]: starting watch, watcher " << watcher.get();
          watcher->OnServerConfigSelectorUpdate(self->config_selector_);
          self->watchers_.insert(std::move(watcher));
        });
    return nullptr;
  }

  void CancelWatch(
      std::shared_ptr<ServerConfigSelectorWatcher> watcher) override {
    work_serializer_->Run(
        [self = WeakRefAsSubclass<XdsServerConfigSelectorProvider>(),
         watcher = std::move(watcher)]() mutable {
          GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
              << "[XdsServerConfigSelectorProvider " << self.get()
              << "]: cancelling watch, watcher " << watcher.get();
          self->watchers_.erase(watcher);
        });
  }

 private:
  void Orphaned() override {
    work_serializer_->Run(
        [self = WeakRefAsSubclass<XdsServerConfigSelectorProvider>()]() {
          GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
              << "[XdsServerConfigSelectorProvider " << self.get()
              << "]: orphaned";
          self->watchers_.clear();  // Just in case.
          self->config_selector_ = absl::CancelledError("shutting down");
        });
    work_serializer_.reset();
  }

  std::shared_ptr<WorkSerializer> work_serializer_;

  // FIXME: lock annotations?
  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector_;
  absl::flat_hash_set<std::shared_ptr<ServerConfigSelectorWatcher>> watchers_;
};

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::L4FilterChain::RouteConfigWatcher
//

class XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::RouteConfigWatcher final
    : public XdsRouteConfigResourceType::WatcherInterface {
 public:
  explicit RouteConfigWatcher(RefCountedPtr<L4FilterChain> l4_filter_chain)
      : l4_filter_chain_(std::move(l4_filter_chain)) {}

  void OnResourceChanged(
      absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
          route_config,
      RefCountedPtr<ReadDelayHandle> read_delay_handle) override {
    l4_filter_chain_->filter_chain_match_manager_->config_fetcher_
        ->work_serializer_->Run(
            [l4_filter_chain = l4_filter_chain_,
             route_config = std::move(route_config),
             read_delay_handle = std::move(read_delay_handle)]()
                ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                    *l4_filter_chain_->filter_chain_match_manager_
                         ->config_fetcher_->work_serializer_) mutable {
                  l4_filter_chain->OnRouteConfigChanged(
                      std::move(route_config));
                });
  }

  void OnAmbientError(
      absl::Status status,
      RefCountedPtr<ReadDelayHandle> read_delay_handle) override {
    l4_filter_chain_->filter_chain_match_manager_->config_fetcher_
        ->work_serializer_->Run(
            [l4_filter_chain = l4_filter_chain_, status = std::move(status),
             read_delay_handle = std::move(read_delay_handle)]()
                ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                    *l4_filter_chain_->filter_chain_match_manager_
                         ->config_fetcher_->work_serializer_) mutable {
                  l4_filter_chain->OnAmbientError(std::move(status));
                });
  }

 private:
  RefCountedPtr<L4FilterChain> l4_filter_chain_;
};

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::L4FilterChain
//

absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
CreateCertificateProviderForFilterChain(
    const XdsListenerResource::FilterChainData& filter_chain,
    CertificateProviderStore& certificate_provider_store) {
  // Configure root cert.
  auto* ca_cert_provider =
      std::get_if<CommonTlsContext::CertificateProviderPluginInstance>(
          &filter_chain.downstream_tls_context.common_tls_context
               .certificate_validation_context.ca_certs);
  absl::string_view root_provider_cert_name;
  RefCountedPtr<grpc_tls_certificate_provider> root_cert_provider;
  if (ca_cert_provider != nullptr) {
    root_provider_cert_name = ca_cert_provider->certificate_name;
    root_cert_provider =
        certificate_provider_store.CreateOrGetCertificateProvider(
            ca_cert_provider->instance_name);
    if (root_cert_provider == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Certificate provider instance name: \"",
                       ca_cert_provider->instance_name, "\" not recognized."));
    }
  }
  // Configure identity cert.
  absl::string_view identity_provider_instance_name =
      filter_chain.downstream_tls_context.common_tls_context
          .tls_certificate_provider_instance.instance_name;
  absl::string_view identity_provider_cert_name =
      filter_chain.downstream_tls_context.common_tls_context
          .tls_certificate_provider_instance.certificate_name;
  RefCountedPtr<grpc_tls_certificate_provider> identity_cert_provider;
  if (!identity_provider_instance_name.empty()) {
    identity_cert_provider =
        certificate_provider_store.CreateOrGetCertificateProvider(
            identity_provider_instance_name);
    if (identity_cert_provider == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Certificate provider instance name: \"",
                       identity_provider_instance_name, "\" not recognized."));
    }
  }
  return MakeRefCounted<XdsCertificateProvider>(
      std::move(root_cert_provider), root_provider_cert_name,
      std::move(identity_cert_provider), identity_provider_cert_name,
      filter_chain.downstream_tls_context.require_client_certificate);
}

XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::L4FilterChain(
        WeakRefCountedPtr<FilterChainMatchManager> filter_chain_match_manager,
        const XdsListenerResource::FilterChainData& filter_chain_data)
    : filter_chain_match_manager_(std::move(filter_chain_match_manager)),
      filter_chain_data_(filter_chain_data),
      certificate_provider_(CreateCertificateProviderForFilterChain(
          filter_chain_data, filter_chain_match_manager_->config_fetcher_
                                 ->xds_client_->certificate_provider_store())) {
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[L4FilterChain " << this << "]: created";
  auto& hcm = filter_chain_data_.http_connection_manager;
  Match(
      hcm.route_config,
      [&](const std::string& rds_resource_name) {
        // Start RDS watch.
        GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
            << "[L4FilterChain " << this << "]: starting RDS watch for "
            << rds_resource_name;
        auto watcher = MakeRefCounted<RouteConfigWatcher>(Ref());
        watcher_ = watcher.get();
        XdsRouteConfigResourceType::StartWatch(
            filter_chain_match_manager_->config_fetcher_->xds_client_.get(),
            rds_resource_name, std::move(watcher));
      },
      [&](const std::shared_ptr<const XdsRouteConfigResource>& route_config) {
        // RouteConfig was inlined in LDS.
        GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
            << "[L4FilterChain " << this << "]: got RouteConfig from LDS";
        UpdateServerConfigSelector(route_config);
      });
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::Orphan() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
        *filter_chain_match_manager_->config_fetcher_->work_serializer_) {
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[L4FilterChain " << this << "]: orphaned";
  if (watcher_ != nullptr) {
    auto& hcm = filter_chain_data_.http_connection_manager;
    auto& rds_resource_name = std::get<std::string>(hcm.route_config);
    XdsRouteConfigResourceType::CancelWatch(
        filter_chain_match_manager_->config_fetcher_->xds_client_.get(),
        rds_resource_name, watcher_);
    watcher_ = nullptr;
  }
  filter_chain_match_manager_.reset();
  Unref();
}

absl::StatusOr<ChannelArgs> XdsServerConfigFetcher::ListenerWatcher::
    FilterChainMatchManager::L4FilterChain::UpdateChannelArgsForConnection(
        const ChannelArgs& args) const {
  if (!certificate_provider_.ok()) return certificate_provider_.status();
  ChannelArgs new_args = args.SetObject(*certificate_provider_);
  // Add channel stack modifier to insert HTTP filters.
  // FIXME: remove
  std::vector<const grpc_channel_filter*> filters;
  const auto& http_filter_registry =
      DownCast<const GrpcXdsBootstrap&>(
          filter_chain_match_manager_->config_fetcher_->xds_client_
          ->bootstrap())
          .http_filter_registry();
  for (const auto& http_filter :
       filter_chain_data_.http_connection_manager.http_filters) {
    // Find filter.  This is guaranteed to succeed, because it's checked
    // at config validation time in the XdsApi code.
    const XdsHttpFilterImpl* filter_impl =
        http_filter_registry.GetFilterForTopLevelType(
            http_filter.config_proto_type);
    GRPC_CHECK_NE(filter_impl, nullptr);
    // Some filters like the router filter are no-op filters and do not have
    // an implementation.
    if (filter_impl->channel_filter() != nullptr) {
      filters.push_back(filter_impl->channel_filter());
    }
  }
  // Reverse the order, since filters flow *up* the stack on the server side.
  std::reverse(filters.begin(), filters.end());
  // Add config selector filter.
  filters.push_back(&kServerConfigSelectorFilter);
  new_args = new_args.SetObject(
      MakeRefCounted<XdsChannelStackModifier>(std::move(filters)));
  // FIXME: don't add ConfigSelectorProvider if there are no filters
  new_args = new_args.SetObject(config_selector_provider_);
  return new_args;
}

absl::StatusOr<RefCountedPtr<ServerConfigSelector>>
XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::CreateServerConfigSelector(
        absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
            route_config)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(
            *filter_chain_match_manager_->config_fetcher_->work_serializer_) {
  if (!route_config.ok()) return route_config.status();
  return XdsServerConfigSelector::Create(
      DownCast<const GrpcXdsBootstrap&>(
          filter_chain_match_manager_->config_fetcher_->xds_client_
              ->bootstrap())
          .http_filter_registry(),
      *route_config, filter_chain_data_.http_connection_manager.http_filters);
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::UpdateServerConfigSelector(
        absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
            route_config)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(
            *filter_chain_match_manager_->config_fetcher_->work_serializer_) {
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[L4FilterChain " << this << "]: creating new XdsServerConfigSelector";
  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> config_selector =
      CreateServerConfigSelector(std::move(route_config));
  if (config_selector_provider_ == nullptr) {
    GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
        << "[L4FilterChain " << this
        << "]: initial update; creating new XdsServerConfigSelectorProvider";
    config_selector_provider_ = MakeRefCounted<XdsServerConfigSelectorProvider>(
        filter_chain_match_manager_->config_fetcher_->work_serializer_,
        std::move(config_selector));
  } else {
    // Provider already exists, so just update it.
    config_selector_provider_->SetServerConfigSelector(
        std::move(config_selector));
  }
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::OnRouteConfigChanged(
        absl::StatusOr<std::shared_ptr<const XdsRouteConfigResource>>
            route_config)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(
            *filter_chain_match_manager_->config_fetcher_->work_serializer_) {
  GRPC_TRACE_LOG(xds_server_config_fetcher, INFO)
      << "[L4FilterChain " << this << "]: received RDS update";
  UpdateServerConfigSelector(std::move(route_config));
  filter_chain_match_manager_->MaybePromote();
}

void XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::OnAmbientError(absl::Status status) {
  auto& hcm = filter_chain_data_.http_connection_manager;
  auto& rds_resource_name = std::get<std::string>(hcm.route_config);
  LOG(ERROR) << "XdsClient reports ambient error for RDS resource "
             << rds_resource_name << ": " << status;
}

//
// XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::L4FilterChain::XdsServerConfigSelector
//

absl::StatusOr<RefCountedPtr<
    XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
        L4FilterChain::XdsServerConfigSelector>>
XdsServerConfigFetcher::ListenerWatcher::FilterChainMatchManager::
    L4FilterChain::XdsServerConfigSelector::Create(
        const XdsHttpFilterRegistry& http_filter_registry,
        std::shared_ptr<const XdsRouteConfigResource> rds_update,
        const std::vector<
            XdsListenerResource::HttpConnectionManager::HttpFilter>&
            http_filters) {
  // FIXME: for each route, construct merged filter configs and
  // update blackboard.
  // and make sure to reverse the order of the filters here!
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
          std::get_if<XdsRouteConfigResource::Route::NonForwardingAction>(
              &route.action) == nullptr;
      auto result = XdsRouting::GeneratePerHTTPFilterConfigsForMethodConfig(
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
    L4FilterChain::XdsServerConfigSelector::GetCallConfig(
        grpc_metadata_batch* metadata) {
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
  if (!route_index.has_value()) {
    return absl::UnavailableError("no route matched");
  }
  auto& route = virtual_host.routes[*route_index];
  // Found the matching route
  if (route.unsupported_action) {
    return absl::UnavailableError("matching route has unsupported action");
  }
  if (route.method_config != nullptr) {
    call_config.method_configs =
        route.method_config->GetMethodParsedConfigVector(grpc_empty_slice());
    call_config.service_config = route.method_config;
  }
  // FIXME: Populate filter chain.
  return call_config;
}

}  // namespace
}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create_legacy(
    grpc_server_xds_status_notifier notifier, const grpc_channel_args* args);

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create(
    grpc_server_xds_status_notifier notifier, const grpc_channel_args* args) {
  if (!grpc_core::IsXdsServerFilterChainPerRouteEnabled()) {
    return grpc_server_config_fetcher_xds_create_legacy(notifier, args);
  }
  grpc_core::ExecCtx exec_ctx;
  grpc_core::ChannelArgs channel_args = grpc_core::CoreConfiguration::Get()
                                            .channel_args_preconditioning()
                                            .PreconditionChannelArgs(args);
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_config_fetcher_xds_create(notifier={on_serving_status_"
         "update="
      << notifier.on_serving_status_update
      << ", user_data=" << notifier.user_data << "}, args=" << args << ")";
  auto xds_client = grpc_core::GrpcXdsClient::GetOrCreate(
      grpc_core::GrpcXdsClient::kServerKey, channel_args,
      "XdsServerConfigFetcher");
  if (!xds_client.ok()) {
    LOG(ERROR) << "Failed to create xds client: " << xds_client.status();
    return nullptr;
  }
  if (grpc_core::DownCast<const grpc_core::GrpcXdsBootstrap&>(
          (*xds_client)->bootstrap())
          .server_listener_resource_name_template()
          .empty()) {
    LOG(ERROR) << "server_listener_resource_name_template not provided in "
                  "bootstrap file.";
    return nullptr;
  }
  return (new grpc_core::XdsServerConfigFetcher(std::move(*xds_client),
                                                notifier))
      ->c_ptr();
}
