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

class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  class FilterChainMatchManager
      : public grpc_server_config_fetcher::ConnectionManager {
   public:
    FilterChainMatchManager(
        RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
        XdsApi::LdsUpdate::FilterChainMap filter_chain_map,
        absl::optional<XdsApi::LdsUpdate::FilterChainData> default_filter_chain,
        std::vector<std::string> resource_names)
        : server_config_fetcher_(std::move(server_config_fetcher)),
          filter_chain_map_(std::move(filter_chain_map)),
          default_filter_chain_(std::move(default_filter_chain)),
          resource_names_(std::move(resource_names)) {}

    ~FilterChainMatchManager() override;

    absl::StatusOr<grpc_channel_args*> UpdateChannelArgsForConnection(
        grpc_channel_args* args, grpc_endpoint* tcp) override;

    const XdsApi::LdsUpdate::FilterChainMap& filter_chain_map() const {
      return filter_chain_map_;
    }

    const absl::optional<XdsApi::LdsUpdate::FilterChainData>&
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

    absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
    CreateOrGetXdsCertificateProviderFromFilterChainData(
        const XdsApi::LdsUpdate::FilterChainData* filter_chain);

    RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher_;
    const XdsApi::LdsUpdate::FilterChainMap filter_chain_map_;
    const absl::optional<XdsApi::LdsUpdate::FilterChainData>
        default_filter_chain_;
    const std::vector<std::string> resource_names_;
    Mutex mu_;
    std::map<const XdsApi::LdsUpdate::FilterChainData*, CertificateProviders>
        certificate_providers_map_ ABSL_GUARDED_BY(mu_);
  };

  class RdsUpdateWatcherInterface {
   public:
    virtual ~RdsUpdateWatcherInterface() = default;
    // A return value of true indicates that the watcher wants to continue
    // watching on RDS updates, while a false value indicates that the fetcher
    // should remove this watcher.
    virtual ABSL_MUST_USE_RESULT bool OnRdsUpdate(
        absl::StatusOr<XdsApi::RdsUpdate> rds_update) = 0;
  };

  XdsServerConfigFetcher(RefCountedPtr<XdsClient> xds_client,
                         grpc_server_xds_status_notifier notifier);

  void StartWatch(std::string listening_address,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override;

  void CancelWatch(
      grpc_server_config_fetcher::WatcherInterface* watcher) override;

  absl::optional<absl::StatusOr<XdsApi::RdsUpdate>> StartRdsWatch(
      absl::string_view resource_name,
      std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface>
          watcher,
      bool inc_ref);

  void CancelRdsWatch(
      absl::string_view resource_name,
      XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher, bool dec_ref);

  // Return the interested parties from the xds client so that it can be polled.
  grpc_pollset_set* interested_parties() override {
    return xds_client_->interested_parties();
  }

 private:
  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    ListenerWatcher(
        std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
            server_config_watcher,
        RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
        grpc_server_xds_status_notifier serving_status_notifier,
        std::string listening_address);

    void OnListenerChanged(XdsApi::LdsUpdate listener) override;

    void OnError(grpc_error_handle error) override;

    void OnResourceDoesNotExist() override;

   private:
    class RdsUpdateWatcher : public RdsUpdateWatcherInterface {
     public:
      RdsUpdateWatcher(std::string resource_name, ListenerWatcher* parent);
      bool OnRdsUpdate(absl::StatusOr<XdsApi::RdsUpdate> rds_update) override
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(
              parent_->server_config_fetcher_->rds_mu_);

     private:
      std::string resource_name_;
      ListenerWatcher* parent_;
    };

    void OnFatalError(absl::Status status) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    void UpdateFilterChainMatchManagerLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    const std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher_;
    RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher_;
    const grpc_server_xds_status_notifier serving_status_notifier_;
    const std::string listening_address_;
    Mutex mu_;
    RefCountedPtr<FilterChainMatchManager> filter_chain_match_manager_
        ABSL_GUARDED_BY(mu_);
    RefCountedPtr<FilterChainMatchManager> pending_filter_chain_match_manager_
        ABSL_GUARDED_BY(mu_);
    std::vector<std::string> pending_rds_updates_ ABSL_GUARDED_BY(mu_);
  };

  struct WatcherState {
    std::string listening_address;
    ListenerWatcher* listener_watcher = nullptr;
  };

  struct RdsUpdateWatcherState;

  class RouteConfigWatcher : public XdsClient::RouteConfigWatcherInterface {
   public:
    explicit RouteConfigWatcher(
        absl::string_view resource_name,
        RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher)
        : resource_name_(resource_name),
          server_config_fetcher_(std::move(server_config_fetcher)) {}

    void OnRouteConfigChanged(XdsApi::RdsUpdate route_config) override;
    void OnError(grpc_error_handle error) override;
    void OnResourceDoesNotExist() override;

   private:
    void Update(RdsUpdateWatcherState* state,
                absl::StatusOr<XdsApi::RdsUpdate> rds_update);

    std::string resource_name_;
    RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher_;
  };

  struct RdsUpdateWatcherState {
    std::map<RdsUpdateWatcherInterface*,
             std::unique_ptr<RdsUpdateWatcherInterface>>
        rds_watchers;
    RouteConfigWatcher* route_config_watcher = nullptr;
    absl::optional<absl::StatusOr<XdsApi::RdsUpdate>> rds_update;
    int listener_refs = 0;
  };

  // A fire and forget class to start watching on route config data from
  // XdsClient via ExecCtx
  class RouteConfigWatchStarter {
   public:
    RouteConfigWatchStarter(
        RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
        absl::string_view resource_name);

   private:
    static void RunInExecCtx(void* arg, grpc_error_handle /* error */);

    grpc_closure closure_;
    RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher_;
    std::string resource_name_;
  };

  absl::optional<absl::StatusOr<XdsApi::RdsUpdate>> StartRdsWatchLocked(
      absl::string_view resource_name,
      std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface>
          watcher,
      bool inc_ref) ABSL_EXCLUSIVE_LOCKS_REQUIRED(rds_mu_);

  RefCountedPtr<XdsClient> xds_client_;
  grpc_server_xds_status_notifier serving_status_notifier_;
  Mutex mu_;
  std::map<grpc_server_config_fetcher::WatcherInterface*, WatcherState>
      listener_watchers_ ABSL_GUARDED_BY(mu_);
  Mutex rds_mu_;
  std::map<std::string, RdsUpdateWatcherState> rds_watchers_
      ABSL_GUARDED_BY(rds_mu_);
};

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

class XdsServerConfigSelector : public ServerConfigSelector {
 public:
  static absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>> Create(
      XdsApi::RdsUpdate rds_update,
      const std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>&
          http_filters);
  ~XdsServerConfigSelector() override = default;

  CallConfig GetCallConfig(grpc_metadata_batch* metadata) override;

 private:
  class VirtualHostListIterator;
  struct VirtualHost {
    class RouteListIterator;
    struct Route {
      bool inappropriate_action;  // true if an action other than
                                  // kNonForwardingAction is configured.
      XdsApi::Route::Matchers matchers;
      RefCountedPtr<ServiceConfig> method_config;
    };
    std::vector<std::string> domains;
    std::vector<Route> routes;
  };

  std::vector<VirtualHost> virtual_hosts_;
};

class XdsServerConfigSelector::VirtualHostListIterator
    : public XdsRouting::VirtualHostListIterator {
 public:
  VirtualHostListIterator(const std::vector<VirtualHost>& virtual_hosts)
      : virtual_hosts_(virtual_hosts) {}

  size_t Size() const override { return virtual_hosts_.size(); }

  const std::vector<std::string>& GetDomainsForVirtualHost(
      size_t index) const override {
    return virtual_hosts_[index].domains;
  }

 private:
  const std::vector<VirtualHost>& virtual_hosts_;
};

class XdsServerConfigSelector::VirtualHost::RouteListIterator
    : public XdsRouting::RouteListIterator {
 public:
  RouteListIterator(const std::vector<Route>& routes) : routes_(routes) {}

  size_t Size() const override { return routes_.size(); }

  const XdsApi::Route::Matchers& GetMatchersForRoute(
      size_t index) const override {
    return routes_[index].matchers;
  }

 private:
  const std::vector<Route>& routes_;
};

absl::StatusOr<RefCountedPtr<XdsServerConfigSelector>>
XdsServerConfigSelector::Create(
    XdsApi::RdsUpdate rds_update,
    const std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>&
        http_filters) {
  auto config_selector = MakeRefCounted<XdsServerConfigSelector>();
  for (auto& vhost : rds_update.virtual_hosts) {
    config_selector->virtual_hosts_.push_back(VirtualHost());
    auto& virtual_host = config_selector->virtual_hosts_.back();
    virtual_host.domains = std::move(vhost.domains);
    for (auto& route : vhost.routes) {
      virtual_host.routes.push_back(VirtualHost::Route());
      auto& config_selector_route = virtual_host.routes.back();
      config_selector_route.matchers = std::move(route.matchers);
      config_selector_route.inappropriate_action =
          absl::get_if<XdsApi::Route::NonForwardingAction>(&route.action) ==
          nullptr;
      grpc_channel_args* args = nullptr;
      std::map<std::string, std::vector<std::string>> per_filter_configs;
      for (const auto& http_filter : http_filters) {
        // Find filter.  This is guaranteed to succeed, because it's checked
        // at config validation time in the XdsApi code.
        const XdsHttpFilterImpl* filter_impl =
            XdsHttpFilterRegistry::GetFilterForType(
                http_filter.config.config_proto_type_name);
        GPR_ASSERT(filter_impl != nullptr);
        // If there is not actually any C-core filter associated with this
        // xDS filter, then it won't need any config, so skip it.
        if (filter_impl->channel_filter() == nullptr) continue;
        // Allow filter to add channel args that may affect service config
        // parsing.
        args = filter_impl->ModifyChannelArgs(args);
        // Find config override, if any.
        const XdsHttpFilterImpl::FilterConfig* config_override = nullptr;
        auto it = route.typed_per_filter_config.find(http_filter.name);
        if (it == route.typed_per_filter_config.end()) {
          it = vhost.typed_per_filter_config.find(http_filter.name);
          if (it != vhost.typed_per_filter_config.end()) {
            config_override = &it->second;
          }
        } else {
          config_override = &it->second;
        }
        // Generate service config for filter.
        auto method_config_field = filter_impl->GenerateServiceConfig(
            http_filter.config, config_override);
        if (!method_config_field.ok()) {
          return method_config_field.status();
        }
        per_filter_configs[method_config_field->service_config_field_name]
            .push_back(method_config_field->element);
      }
      std::vector<std::string> fields;
      fields.reserve(per_filter_configs.size());
      for (const auto& p : per_filter_configs) {
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
            ServiceConfig::Create(args, json.c_str(), &error);
        GPR_ASSERT(error == GRPC_ERROR_NONE);
      }
      grpc_channel_args_destroy(args);
    }
  }
  return config_selector;
}

ServerConfigSelector::CallConfig XdsServerConfigSelector::GetCallConfig(
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
      VirtualHostListIterator(virtual_hosts_), authority);
  if (!vhost_index.has_value()) {
    call_config.error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("could not find VirtualHost for ", authority,
                     " in RouteConfiguration"));
    return call_config;
  }
  auto& virtual_host = virtual_hosts_[vhost_index.value()];
  auto route_index = XdsRouting::GetRouteForRequest(
      VirtualHost::RouteListIterator(virtual_host.routes), path, metadata);
  if (route_index.has_value()) {
    auto& route = virtual_host.routes[route_index.value()];
    // Found the matching route
    if (route.inappropriate_action) {
      call_config.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Matching route has inappropriate_action");
      return call_config;
    }
    if (route.method_config != nullptr) {
      call_config.method_configs =
          route.method_config->GetMethodParsedConfigVector(grpc_empty_slice());
      call_config.service_config = route.method_config;
    }
    return call_config;
  }
  call_config.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("No route matched");
  return call_config;
}

// A fire and forget class to cancel watching on Rds from XdsServerConfigFetcher
// via ExecCtx
class RdsWatchCanceller {
 public:
  RdsWatchCanceller(RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
                    std::vector<std::string> resource_names,
                    XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher,
                    bool dec_ref)
      : server_config_fetcher_(std::move(server_config_fetcher)),
        resource_names_(std::move(resource_names)),
        watcher_(watcher),
        dec_ref_(dec_ref) {
    GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
    ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
  }

 private:
  static void RunInExecCtx(void* arg, grpc_error_handle /* error */) {
    auto* self = static_cast<RdsWatchCanceller*>(arg);
    for (const auto& resource_name : self->resource_names_) {
      self->server_config_fetcher_->CancelRdsWatch(
          resource_name, self->watcher_, self->dec_ref_);
    }
    delete self;
  }

  grpc_closure closure_;
  RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher_;
  std::vector<std::string> resource_names_;
  XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher_;
  bool dec_ref_;
};

// XdsServerConfigSelectorProvider acts as a base class for
// StaticXdsServerConfigSelectorProvider and
// DynamicXdsServerConfigSelectorProvider.
class XdsServerConfigSelectorProvider : public ServerConfigSelectorProvider {
 public:
  absl::StatusOr<RefCountedPtr<ServerConfigSelector>> Watch(
      std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
          watcher) override {
    {
      MutexLock lock(&mu_);
      GPR_ASSERT(watcher_ == nullptr);
      watcher_ = std::move(watcher);
    }
    if (!resource_.ok()) {
      return resource_.status();
    }
    return XdsServerConfigSelector::Create(resource_.value(), http_filters_);
  }

  void CancelWatch() override {
    MutexLock lock(&mu_);
    watcher_.reset();
  }

 protected:
  class RdsUpdateWatcher
      : public XdsServerConfigFetcher::RdsUpdateWatcherInterface {
   public:
    explicit RdsUpdateWatcher(XdsServerConfigSelectorProvider* parent)
        : parent_(parent) {}
    bool OnRdsUpdate(absl::StatusOr<XdsApi::RdsUpdate> rds_update) override {
      MutexLock lock(&parent_->mu_);
      if (parent_->watcher_ != nullptr) {
        if (!rds_update.ok()) {
          parent_->watcher_->OnServerConfigSelectorUpdate(rds_update.status());
        } else {
          parent_->watcher_->OnServerConfigSelectorUpdate(
              XdsServerConfigSelector::Create(rds_update.value(),
                                              parent_->http_filters_));
        }
      }
      return true;
    }

   private:
    XdsServerConfigSelectorProvider* parent_;
  };

  XdsServerConfigSelectorProvider(
      absl::StatusOr<XdsApi::RdsUpdate> static_resource,
      std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>
          http_filters)
      : resource_(std::move(static_resource)),
        http_filters_(std::move(http_filters)) {}

  explicit XdsServerConfigSelectorProvider(
      std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>
          http_filters)
      : http_filters_(std::move(http_filters)) {}

  void set_resource(absl::StatusOr<XdsApi::RdsUpdate> resource) {
    resource_ = std::move(resource);
  }

 private:
  absl::StatusOr<XdsApi::RdsUpdate> resource_;
  std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>
      http_filters_;
  Mutex mu_;
  std::unique_ptr<ServerConfigSelectorProvider::ServerConfigSelectorWatcher>
      watcher_ ABSL_GUARDED_BY(mu_);
};

// An XdsServerConfigSelectorProvider implementation for when the
// RouteConfiguration is available inline.
class StaticXdsServerConfigSelectorProvider
    : public XdsServerConfigSelectorProvider {
 public:
  StaticXdsServerConfigSelectorProvider(
      absl::StatusOr<XdsApi::RdsUpdate> static_resource,
      std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>
          http_filters)
      : XdsServerConfigSelectorProvider(std::move(static_resource),
                                        std::move(http_filters)) {}
};

// An XdsServerConfigSelectorProvider implementation for when the
// RouteConfiguration is to be fetched separately via RDS.
class DynamicXdsServerConfigSelectorProvider
    : public XdsServerConfigSelectorProvider {
 public:
  DynamicXdsServerConfigSelectorProvider(
      RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
      std::string resource_name,
      std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter>
          http_filters)
      : XdsServerConfigSelectorProvider(std::move(http_filters)),
        server_config_fetcher_(std::move(server_config_fetcher)),
        resource_name_(std::move(resource_name)) {
    GPR_ASSERT(!resource_name_.empty());
    auto rds_watcher = absl::make_unique<RdsUpdateWatcher>(this);
    rds_watcher_ = rds_watcher.get();
    auto rds_update = server_config_fetcher_->StartRdsWatch(
        resource_name_, std::move(rds_watcher), false /* inc_ref */);
    if (rds_update.has_value()) {
      set_resource(std::move(rds_update.value()));
    } else {
      // This should never happen
      set_resource(absl::UnavailableError("RDS resource has not been fetched"));
    }
  }

  ~DynamicXdsServerConfigSelectorProvider() override {
    if (!resource_name_.empty()) {
      new RdsWatchCanceller(server_config_fetcher_, {resource_name_},
                            rds_watcher_, /*dec_ref=*/false);
      rds_watcher_ = nullptr;
    }
  }

 private:
  RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher_;
  std::string resource_name_;
  RdsUpdateWatcher* rds_watcher_ = nullptr;
};

const XdsApi::LdsUpdate::FilterChainData* FindFilterChainDataForSourcePort(
    const XdsApi::LdsUpdate::FilterChainMap::SourcePortsMap& source_ports_map,
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

const XdsApi::LdsUpdate::FilterChainData* FindFilterChainDataForSourceIp(
    const XdsApi::LdsUpdate::FilterChainMap::SourceIpVector& source_ip_vector,
    const grpc_resolved_address* source_ip, absl::string_view port) {
  const XdsApi::LdsUpdate::FilterChainMap::SourceIp* best_match = nullptr;
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

const XdsApi::LdsUpdate::FilterChainData* FindFilterChainDataForSourceType(
    const XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceTypesArray&
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
                             XdsApi::LdsUpdate::FilterChainMap::
                                 ConnectionSourceType::kSameIpOrLoopback)]
          .empty() &&
      source_types_array[static_cast<int>(XdsApi::LdsUpdate::FilterChainMap::
                                              ConnectionSourceType::kExternal)]
          .empty()) {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType::kAny)],
        &source_addr, port);
  }
  if (IsLoopbackIp(&source_addr) || host == destination_ip) {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType::
                kSameIpOrLoopback)],
        &source_addr, port);
  } else {
    return FindFilterChainDataForSourceIp(
        source_types_array[static_cast<int>(
            XdsApi::LdsUpdate::FilterChainMap::ConnectionSourceType::
                kExternal)],
        &source_addr, port);
  }
}

const XdsApi::LdsUpdate::FilterChainData* FindFilterChainDataForDestinationIp(
    const XdsApi::LdsUpdate::FilterChainMap::DestinationIpVector
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
  const XdsApi::LdsUpdate::FilterChainMap::DestinationIp* best_match = nullptr;
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

//
// XdsServerConfigFetcher::FilterChainMatchManager
//

XdsServerConfigFetcher::FilterChainMatchManager::~FilterChainMatchManager() {
  new RdsWatchCanceller(server_config_fetcher_, resource_names_, nullptr,
                        /* dec_ref = */ true);
}

absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
XdsServerConfigFetcher::FilterChainMatchManager::
    CreateOrGetXdsCertificateProviderFromFilterChainData(
        const XdsApi::LdsUpdate::FilterChainData* filter_chain) {
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
        server_config_fetcher_->xds_client_->certificate_provider_store()
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
        server_config_fetcher_->xds_client_->certificate_provider_store()
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

absl::StatusOr<grpc_channel_args*>
XdsServerConfigFetcher::FilterChainMatchManager::UpdateChannelArgsForConnection(
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
    // Some filters like the router filter are no-op filters and do not have an
    // implementation.
    if (filter_impl->channel_filter() != nullptr) {
      filters.push_back(filter_impl->channel_filter());
    }
  }
  // Add config selector filter
  filters.push_back(&kServerConfigSelectorFilter);
  auto channel_stack_modifier =
      MakeRefCounted<XdsChannelStackModifier>(std::move(filters));
  grpc_channel_args* old_args = args;
  RefCountedPtr<ServerConfigSelectorProvider> server_config_selector_provider;
  if (filter_chain->http_connection_manager.rds_update.has_value()) {
    server_config_selector_provider =
        MakeRefCounted<StaticXdsServerConfigSelectorProvider>(
            filter_chain->http_connection_manager.rds_update.value(),
            filter_chain->http_connection_manager.http_filters);
  } else {
    server_config_selector_provider =
        MakeRefCounted<DynamicXdsServerConfigSelectorProvider>(
            server_config_fetcher_,
            filter_chain->http_connection_manager.route_config_name,
            filter_chain->http_connection_manager.http_filters);
  }
  grpc_arg args_to_add[] = {server_config_selector_provider->MakeChannelArg(),
                            channel_stack_modifier->MakeChannelArg()};
  args = grpc_channel_args_copy_and_add(old_args, args_to_add, 2);
  grpc_channel_args_destroy(old_args);
  // Nothing to update if credentials are not xDS.
  grpc_server_credentials* server_creds =
      grpc_find_server_credentials_in_args(args);
  if (server_creds == nullptr || server_creds->type() != kCredentialsTypeXds) {
    return args;
  }
  absl::StatusOr<RefCountedPtr<XdsCertificateProvider>> result =
      CreateOrGetXdsCertificateProviderFromFilterChainData(filter_chain);
  if (!result.ok()) {
    grpc_channel_args_destroy(args);
    return result.status();
  }
  RefCountedPtr<XdsCertificateProvider> xds_certificate_provider =
      std::move(*result);
  GPR_ASSERT(xds_certificate_provider != nullptr);
  grpc_arg arg_to_add = xds_certificate_provider->MakeChannelArg();
  grpc_channel_args* updated_args =
      grpc_channel_args_copy_and_add(args, &arg_to_add, 1);
  grpc_channel_args_destroy(args);
  return updated_args;
}

//
// XdsServerConfigFetcher::ListenerWatcher::RdsUpdateWatcher
//

XdsServerConfigFetcher::ListenerWatcher::RdsUpdateWatcher::RdsUpdateWatcher(
    std::string resource_name, ListenerWatcher* parent)
    : resource_name_(resource_name), parent_(parent) {}

bool XdsServerConfigFetcher::ListenerWatcher::RdsUpdateWatcher::OnRdsUpdate(
    absl::StatusOr<XdsApi::RdsUpdate> /* rds_update */) {
  {
    MutexLock lock(&parent_->mu_);
    for (auto it = parent_->pending_rds_updates_.begin();
         it != parent_->pending_rds_updates_.end(); ++it) {
      if (*it == resource_name_) {
        parent_->pending_rds_updates_.erase(it);
        break;
      }
    }
    if (parent_->pending_rds_updates_.empty() &&
        parent_->pending_filter_chain_match_manager_ != nullptr) {
      parent_->UpdateFilterChainMatchManagerLocked();
    }
  }
  return false;  // Stop watching on updates
}

//
// XdsServerConfigFetcher::ListenerWatcher
//

XdsServerConfigFetcher::ListenerWatcher::ListenerWatcher(
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher,
    // grpc_channel_args* args,
    RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
    grpc_server_xds_status_notifier serving_status_notifier,
    std::string listening_address)
    : server_config_watcher_(std::move(server_config_watcher)),
      server_config_fetcher_(std::move(server_config_fetcher)),
      serving_status_notifier_(serving_status_notifier),
      listening_address_(std::move(listening_address)) {}

void XdsServerConfigFetcher::ListenerWatcher::OnListenerChanged(
    XdsApi::LdsUpdate listener) {
  class RdsWatcherToCancel {
   public:
    RdsWatcherToCancel(
        RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
        absl::string_view route_config_name, RdsUpdateWatcher* watcher)
        : server_config_fetcher_(std::move(server_config_fetcher)),
          route_config_name_(route_config_name),
          watcher_(watcher) {}
    ~RdsWatcherToCancel() {
      new RdsWatchCanceller(server_config_fetcher_, {route_config_name_},
                            watcher_, /*dec_ref=*/false);
    }
    RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher_;
    std::string route_config_name_;
    RdsUpdateWatcher* watcher_;
  };
  std::vector<RdsWatcherToCancel> rds_watchers_to_cancel;
  {
    MutexLock rds_lock(&server_config_fetcher_->rds_mu_);
    MutexLock lock(&mu_);
    pending_filter_chain_match_manager_.reset();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_server_config_fetcher_trace)) {
      gpr_log(GPR_INFO,
              "[ListenerWatcher %p] Received LDS update from xds client %p: %s",
              this, server_config_fetcher_->xds_client_.get(),
              listener.ToString().c_str());
    }
    if (listener.address != listening_address_) {
      OnFatalError(absl::FailedPreconditionError(
          "Address in LDS update does not match listening address"));
      return;
    }
    if (filter_chain_match_manager_ == nullptr ||
        !(listener.filter_chain_map ==
              filter_chain_match_manager_->filter_chain_map() &&
          listener.default_filter_chain ==
              filter_chain_match_manager_->default_filter_chain())) {
      // TODO(): Maybe change LdsUpdate to provide a vector of
      // FilterChainDataSharedPtr directly to avoid going through the multilevel
      // map.
      std::set<std::shared_ptr<XdsApi::LdsUpdate::FilterChainData>>
          filter_chains;
      for (const auto& destination_ip :
           listener.filter_chain_map.destination_ip_vector) {
        for (int source_type = 0; source_type < 3; ++source_type) {
          for (const auto& source_ip :
               destination_ip.source_types_array[source_type]) {
            for (const auto& source_port_pair : source_ip.ports_map) {
              filter_chains.insert(source_port_pair.second.data);
            }
          }
        }
      }
      std::vector<std::string> resource_names;
      for (const auto& filter_chain : filter_chains) {
        if (!filter_chain->http_connection_manager.route_config_name.empty()) {
          resource_names.push_back(
              filter_chain->http_connection_manager.route_config_name);
        }
      }
      if (listener.default_filter_chain.has_value() &&
          !listener.default_filter_chain->http_connection_manager
               .route_config_name.empty()) {
        resource_names.push_back(
            listener.default_filter_chain->http_connection_manager
                .route_config_name);
      }
      for (const auto& resource_name : resource_names) {
        auto rds_update_watcher =
            absl::make_unique<RdsUpdateWatcher>(resource_name, this);
        auto rds_update_watcher_ptr = rds_update_watcher.get();
        auto rds_update = server_config_fetcher_->StartRdsWatchLocked(
            resource_name, std::move(rds_update_watcher), /* inc_ref= */ true);
        if (rds_update.has_value()) {
          rds_watchers_to_cancel.emplace_back(
              server_config_fetcher_, resource_name, rds_update_watcher_ptr);
        } else {
          pending_rds_updates_.push_back(resource_name);
        }
      }
      pending_filter_chain_match_manager_ =
          MakeRefCounted<FilterChainMatchManager>(
              server_config_fetcher_, std::move(listener.filter_chain_map),
              std::move(listener.default_filter_chain),
              std::move(resource_names));
      if (pending_rds_updates_.empty()) {
        UpdateFilterChainMatchManagerLocked();
      }
    }
  }
}

void XdsServerConfigFetcher::ListenerWatcher::OnError(grpc_error_handle error) {
  MutexLock lock(&mu_);
  pending_filter_chain_match_manager_.reset();
  if (filter_chain_match_manager_ != nullptr) {
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
  gpr_log(GPR_ERROR,
          "ListenerWatcher:%p Encountered fatal error %s; not serving on %s",
          this, status.ToString().c_str(), listening_address_.c_str());
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
  }
}

void XdsServerConfigFetcher::ListenerWatcher::OnResourceDoesNotExist() {
  MutexLock lock(&mu_);
  OnFatalError(absl::NotFoundError("Requested listener does not exist"));
}

void XdsServerConfigFetcher::ListenerWatcher::
    UpdateFilterChainMatchManagerLocked() {
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
  filter_chain_match_manager_ = std::move(pending_filter_chain_match_manager_);
  server_config_watcher_->UpdateConnectionManager(filter_chain_match_manager_);
}

//
// XdsServerConfigFetcher::RouteConfigWatcher
//

void XdsServerConfigFetcher::RouteConfigWatcher::OnRouteConfigChanged(
    XdsApi::RdsUpdate route_config) {
  ReleasableMutexLock lock(&server_config_fetcher_->rds_mu_);
  auto iterator = server_config_fetcher_->rds_watchers_.find(resource_name_);
  if (iterator == server_config_fetcher_->rds_watchers_.end()) {
    // All listener refs have already been dropped. Cancel the watch.
    lock.Release();
    server_config_fetcher_->xds_client_->CancelRouteConfigDataWatch(
        resource_name_, this, false);
    return;
  }
  Update(&iterator->second, std::move(route_config));
}

void XdsServerConfigFetcher::RouteConfigWatcher::OnError(
    grpc_error_handle error) {
  ReleasableMutexLock lock(&server_config_fetcher_->rds_mu_);
  auto iterator = server_config_fetcher_->rds_watchers_.find(resource_name_);
  if (iterator == server_config_fetcher_->rds_watchers_.end()) {
    // All listener refs have already been dropped. Cancel the watch.
    lock.Release();
    server_config_fetcher_->xds_client_->CancelRouteConfigDataWatch(
        resource_name_, this, false);
    return;
  }
  if (iterator->second.rds_update.has_value() &&
      iterator->second.rds_update->ok()) {
    gpr_log(GPR_ERROR,
            "RouteConfigWatcher:%p XdsClient reports error: %s for %s; "
            "ignoring in favor of existing resource",
            this, grpc_error_std_string(error).c_str(), resource_name_.c_str());
    return;
  }
  Update(&iterator->second, grpc_error_to_absl_status(error));
  GRPC_ERROR_UNREF(error);
}

void XdsServerConfigFetcher::RouteConfigWatcher::OnResourceDoesNotExist() {
  ReleasableMutexLock lock(&server_config_fetcher_->rds_mu_);
  auto iterator = server_config_fetcher_->rds_watchers_.find(resource_name_);
  if (iterator == server_config_fetcher_->rds_watchers_.end()) {
    // All listener refs have already been dropped. Cancel the watch.
    lock.Release();
    server_config_fetcher_->xds_client_->CancelRouteConfigDataWatch(
        resource_name_, this, false);
    return;
  }
  Update(&iterator->second,
         absl::NotFoundError("Requested route config does not exist"));
}

void XdsServerConfigFetcher::RouteConfigWatcher::Update(
    RdsUpdateWatcherState* state,
    absl::StatusOr<XdsApi::RdsUpdate> rds_update) {
  for (auto it = state->rds_watchers.begin();
       it != state->rds_watchers.end();) {
    bool continue_watch = it->second->OnRdsUpdate(rds_update);
    if (!continue_watch) {
      it = state->rds_watchers.erase(it);
    } else {
      ++it;
    }
  }
  state->rds_update = std::move(rds_update);
}

//
// XdsServerConfigFetcher::RouteConfigWatchStarter
//

XdsServerConfigFetcher::RouteConfigWatchStarter::RouteConfigWatchStarter(
    RefCountedPtr<XdsServerConfigFetcher> server_config_fetcher,
    absl::string_view resource_name)
    : server_config_fetcher_(std::move(server_config_fetcher)),
      resource_name_(resource_name) {
  GRPC_CLOSURE_INIT(&closure_, &RunInExecCtx, this, nullptr);
  ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
}

void XdsServerConfigFetcher::RouteConfigWatchStarter::RunInExecCtx(
    void* arg, grpc_error_handle /* error */) {
  auto* self = static_cast<RouteConfigWatchStarter*>(arg);
  std::unique_ptr<RouteConfigWatcher> route_config_watcher;
  {
    MutexLock lock(&self->server_config_fetcher_->rds_mu_);
    auto rds_watcher_state_it =
        self->server_config_fetcher_->rds_watchers_.find(self->resource_name_);
    if (rds_watcher_state_it ==
        self->server_config_fetcher_->rds_watchers_.end()) {
      // No more listeners reference this resource anymore, so no need to start
      // watch.
      return;
    }
    route_config_watcher = absl::make_unique<RouteConfigWatcher>(
        self->resource_name_, self->server_config_fetcher_);
    rds_watcher_state_it->second.route_config_watcher =
        route_config_watcher.get();
  }
  self->server_config_fetcher_->xds_client_->WatchRouteConfigData(
      self->resource_name_, std::move(route_config_watcher));
  delete self;
}

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
  auto listener_watcher = absl::make_unique<ListenerWatcher>(
      std::move(watcher), Ref(), serving_status_notifier_, listening_address);
  auto* listener_watcher_ptr = listener_watcher.get();
  listening_address = absl::StrReplaceAll(
      xds_client_->bootstrap().server_listener_resource_name_template(),
      {{"%s", listening_address}});
  xds_client_->WatchListenerData(listening_address,
                                 std::move(listener_watcher));
  MutexLock lock(&mu_);
  auto& watcher_state = listener_watchers_[watcher_ptr];
  watcher_state.listening_address = listening_address;
  watcher_state.listener_watcher = listener_watcher_ptr;
}

void XdsServerConfigFetcher::CancelWatch(
    grpc_server_config_fetcher::WatcherInterface* watcher) {
  MutexLock lock(&mu_);
  auto it = listener_watchers_.find(watcher);
  if (it != listener_watchers_.end()) {
    // Cancel the watch on the listener before erasing
    xds_client_->CancelListenerDataWatch(it->second.listening_address,
                                         it->second.listener_watcher,
                                         false /* delay_unsubscription */);
    listener_watchers_.erase(it);
  }
}

absl::optional<absl::StatusOr<XdsApi::RdsUpdate>>
XdsServerConfigFetcher::StartRdsWatch(
    absl::string_view resource_name,
    std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface> watcher,
    bool inc_ref) {
  MutexLock lock(&rds_mu_);
  return StartRdsWatchLocked(resource_name, std::move(watcher), inc_ref);
}

absl::optional<absl::StatusOr<XdsApi::RdsUpdate>>
XdsServerConfigFetcher::StartRdsWatchLocked(
    absl::string_view resource_name,
    std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface> watcher,
    bool inc_ref) {
  auto& watcher_state = rds_watchers_[std::string(resource_name)];
  auto watcher_ptr = watcher.get();
  watcher_state.rds_watchers.emplace(watcher_ptr, std::move(watcher));
  if (inc_ref) {
    // Start route config watch at the first listener reference
    if (++watcher_state.listener_refs == 1) {
      // Start watching route config data in an ExecCtx closure since we are
      // already running inside an XdsClient callback and starting a watch
      // inline would cause deadlocks.
      new RouteConfigWatchStarter(Ref(), resource_name);
    }
  }
  return watcher_state.rds_update;
}

void XdsServerConfigFetcher::CancelRdsWatch(
    absl::string_view resource_name,
    XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher, bool dec_ref) {
  RouteConfigWatcher* watcher_to_cancel = nullptr;
  {
    MutexLock lock(&rds_mu_);
    auto rds_watcher_state_it = rds_watchers_.find(std::string(resource_name));
    if (rds_watcher_state_it == rds_watchers_.end()) return;
    rds_watcher_state_it->second.rds_watchers.erase(watcher);
    if (dec_ref) {
      GPR_ASSERT(rds_watcher_state_it->second.listener_refs > 0);
      if (--rds_watcher_state_it->second.listener_refs == 0) {
        watcher_to_cancel = rds_watcher_state_it->second.route_config_watcher;
        rds_watchers_.erase(rds_watcher_state_it);
      }
    }
  }
  if (watcher_to_cancel != nullptr) {
    xds_client_->CancelRouteConfigDataWatch(resource_name, watcher_to_cancel,
                                            false);
  }
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
