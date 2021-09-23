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

#include "src/core/ext/xds/xds_server_config_fetcher.h"

#include "absl/strings/str_replace.h"

#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_server_config_selector.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

TraceFlag grpc_xds_server_config_fetcher_trace(false,
                                               "xds_server_config_fetcher");

namespace {

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

}  // namespace

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

// XdsServerConfigFetcher::FilterChainMatchManager

XdsServerConfigFetcher::FilterChainMatchManager::~FilterChainMatchManager() {
  for (const auto& resource_name : resource_names_) {
    server_config_fetcher_->CancelRdsWatchInternal(resource_name, nullptr,
                                                   /* dec_ref= */ true);
  }
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

absl::StatusOr<grpc_server_config_fetcher::ConnectionConfiguration>
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
  if (!filters.empty()) {
    filters.push_back(&kXdsServerConfigSelectorFilter);
    grpc_channel_args* old_args = args;
    auto server_config_selector_arg =
        MakeRefCounted<XdsServerConfigSelectorArg>();
    server_config_selector_arg->resource_name =
        filter_chain->http_connection_manager.route_config_name;
    server_config_selector_arg->rds_update =
        filter_chain->http_connection_manager.rds_update;
    server_config_selector_arg->server_config_fetcher = server_config_fetcher_;
    server_config_selector_arg->http_filters =
        filter_chain->http_connection_manager.http_filters;
    grpc_arg arg_to_add = server_config_selector_arg->MakeChannelArg();
    args = grpc_channel_args_copy_and_add(old_args, &arg_to_add, 1);
    grpc_channel_args_destroy(old_args);
  }
  // Nothing to update if credentials are not xDS.
  grpc_server_credentials* server_creds =
      grpc_find_server_credentials_in_args(args);
  if (server_creds == nullptr || server_creds->type() != kCredentialsTypeXds) {
    return grpc_server_config_fetcher::ConnectionConfiguration{
        args, std::move(filters)};
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
  return grpc_server_config_fetcher::ConnectionConfiguration{
      updated_args, std::move(filters)};
}

// XdsServerConfigFetcher::ListenerWatcher::RdsUpdateWatcher

XdsServerConfigFetcher::ListenerWatcher::RdsUpdateWatcher::RdsUpdateWatcher(
    std::string resource_name, ListenerWatcher* parent)
    : resource_name_(resource_name), parent_(parent) {}

void XdsServerConfigFetcher::ListenerWatcher::RdsUpdateWatcher::OnRdsUpdate(
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
  parent_->server_config_fetcher_->CancelRdsWatchInternalLocked(
      resource_name_, this, /* dec_ref= */ false);
}

// XdsServerConfigFetcher::ListenerWatcher

XdsServerConfigFetcher::ListenerWatcher::ListenerWatcher(
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher,
    // grpc_channel_args* args,
    XdsServerConfigFetcher* server_config_fetcher,
    grpc_server_xds_status_notifier serving_status_notifier,
    std::string listening_address)
    : server_config_watcher_(std::move(server_config_watcher)),
      server_config_fetcher_(server_config_fetcher),
      serving_status_notifier_(serving_status_notifier),
      listening_address_(std::move(listening_address)) {}

void XdsServerConfigFetcher::ListenerWatcher::OnListenerChanged(
    XdsApi::LdsUpdate listener) {
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
    std::set<std::shared_ptr<XdsApi::LdsUpdate::FilterChainData>> filter_chains;
    std::vector<std::string> resource_names;
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
    for (const auto& filter_chain : filter_chains) {
      if (!filter_chain->http_connection_manager.route_config_name.empty()) {
        resource_names.push_back(
            filter_chain->http_connection_manager.route_config_name);
        auto rds_update_watcher = absl::make_unique<RdsUpdateWatcher>(
            filter_chain->http_connection_manager.route_config_name, this);
        auto rds_update_watcher_ptr = rds_update_watcher.get();
        auto rds_update = server_config_fetcher_->StartRdsWatchInternal(
            filter_chain->http_connection_manager.route_config_name,
            std::move(rds_update_watcher), /* inc_ref= */ true);
        if (rds_update.has_value()) {
          server_config_fetcher_->CancelRdsWatchInternal(
              filter_chain->http_connection_manager.route_config_name,
              rds_update_watcher_ptr, /* dec_ref= */ false);
        } else {
          pending_rds_updates_.push_back(
              filter_chain->http_connection_manager.route_config_name);
        }
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

// XdsServerConfigFetcher::RouteConfigWatcher

void XdsServerConfigFetcher::RouteConfigWatcher::OnRouteConfigChanged(
    XdsApi::RdsUpdate route_config) {
  MutexLock lock(&server_config_fetcher_->rds_mu_);
  auto iterator = server_config_fetcher_->rds_watchers_.find(resource_name_);
  if (iterator == server_config_fetcher_->rds_watchers_.end()) {
    gpr_log(GPR_ERROR,
            "RouteConfigWatcher:%p Rds watcher state for %s not found", this,
            resource_name_.c_str());
    GPR_ASSERT(0);
  }
  iterator->second.rds_update = route_config;
  for (const auto& rds_watcher : iterator->second.rds_watchers) {
    // TODO(): should this be done on ExecCtx?
    rds_watcher.first->OnRdsUpdate(route_config);
  }
}

void XdsServerConfigFetcher::RouteConfigWatcher::OnError(
    grpc_error_handle error) {
  MutexLock lock(&server_config_fetcher_->rds_mu_);
  auto iterator = server_config_fetcher_->rds_watchers_.find(resource_name_);
  if (iterator == server_config_fetcher_->rds_watchers_.end()) {
    gpr_log(GPR_ERROR,
            "RouteConfigWatcher:%p Rds watcher state for %s not found", this,
            resource_name_.c_str());
    GPR_ASSERT(0);
  }
  if (iterator->second.rds_update.has_value() &&
      iterator->second.rds_update->ok()) {
    gpr_log(GPR_ERROR,
            "RouteConfigWatcher:%p XdsClient reports error: %s for %s; "
            "ignoring in favor of existing resource",
            this, grpc_error_std_string(error).c_str(), resource_name_.c_str());
    return;
  }
  iterator->second.rds_update = grpc_error_to_absl_status(error);
  for (const auto& rds_watcher : iterator->second.rds_watchers) {
    // TODO(): should this be done on ExecCtx?
    rds_watcher.first->OnRdsUpdate(iterator->second.rds_update.value());
  }
}

void XdsServerConfigFetcher::RouteConfigWatcher::OnResourceDoesNotExist() {
  MutexLock lock(&server_config_fetcher_->rds_mu_);
  auto iterator = server_config_fetcher_->rds_watchers_.find(resource_name_);
  if (iterator == server_config_fetcher_->rds_watchers_.end()) {
    gpr_log(GPR_ERROR,
            "RouteConfigWatcher:%p Rds watcher state for %s not found", this,
            resource_name_.c_str());
    GPR_ASSERT(0);
  }
  iterator->second.rds_update =
      absl::NotFoundError("Requested route config does not exist");
  for (const auto& rds_watcher : iterator->second.rds_watchers) {
    // TODO(): should this be done on ExecCtx?
    rds_watcher.first->OnRdsUpdate(iterator->second.rds_update.value());
  }
}

// XdsServerConfigFetcher

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
      std::move(watcher), this, serving_status_notifier_, listening_address);
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
    std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface>
        watcher) {
  return StartRdsWatchInternal(resource_name, std::move(watcher),
                               false /* inc_ref */);
}

void XdsServerConfigFetcher::CancelRdsWatch(
    absl::string_view resource_name,
    XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher) {
  return CancelRdsWatchInternal(resource_name, watcher, false /* dec_ref */);
}

absl::optional<absl::StatusOr<XdsApi::RdsUpdate>>
XdsServerConfigFetcher::StartRdsWatchInternal(
    absl::string_view resource_name,
    std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface> watcher,
    bool inc_ref) {
  MutexLock lock(&rds_mu_);
  auto& watcher_state = rds_watchers_[std::string(resource_name)];
  auto watcher_ptr = watcher.get();
  watcher_state.rds_watchers.emplace(watcher_ptr, std::move(watcher));
  if (inc_ref) {
    // Start route config watch at the first listener reference
    if (++watcher_state.listener_refs == 1) {
      auto route_config_watcher =
          absl::make_unique<RouteConfigWatcher>(resource_name, this);
      watcher_state.route_config_watcher = route_config_watcher.get();
      xds_client_->WatchRouteConfigData(resource_name,
                                        std::move(route_config_watcher));
    }
  }
  return watcher_state.rds_update;
}

void XdsServerConfigFetcher::CancelRdsWatchInternal(
    absl::string_view resource_name,
    XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher, bool dec_ref) {
  MutexLock lock(&rds_mu_);
  CancelRdsWatchInternalLocked(resource_name, watcher, dec_ref);
}

void XdsServerConfigFetcher::CancelRdsWatchInternalLocked(
    absl::string_view resource_name,
    XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher, bool dec_ref) {
  auto rds_watcher_state_it = rds_watchers_.find(std::string(resource_name));
  if (rds_watcher_state_it == rds_watchers_.end()) return;
  auto it = rds_watcher_state_it->second.rds_watchers.find(watcher);
  if (it != rds_watcher_state_it->second.rds_watchers.end()) {
    rds_watcher_state_it->second.rds_watchers.erase(it);
  }
  if (dec_ref) {
    GPR_ASSERT(rds_watcher_state_it->second.listener_refs > 0);
    if (--rds_watcher_state_it->second.listener_refs == 0) {
      if (rds_watcher_state_it->second.route_config_watcher != nullptr) {
        xds_client_->CancelRouteConfigDataWatch(
            resource_name, rds_watcher_state_it->second.route_config_watcher,
            false);
      }
      rds_watchers_.erase(rds_watcher_state_it);
    }
  }
}

}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create(
    grpc_server_xds_status_notifier notifier, const grpc_channel_args* args) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_server_config_fetcher_xds_create()", 0, ());
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_core::RefCountedPtr<grpc_core::XdsClient> xds_client =
      grpc_core::XdsClient::GetOrCreate(args, &error);
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
