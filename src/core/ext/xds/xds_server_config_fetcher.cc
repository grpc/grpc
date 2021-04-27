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

#include "absl/strings/str_replace.h"

#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

TraceFlag grpc_xds_server_config_fetcher_trace(false,
                                               "xds_server_config_fetcher");

namespace {

class FilterChainMatchManager
    : public grpc_server_config_fetcher::ConnectionManager {
 public:
  FilterChainMatchManager(
      RefCountedPtr<XdsClient> xds_client,
      XdsApi::LdsUpdate::FilterChainMap filter_chain_map,
      absl::optional<XdsApi::LdsUpdate::FilterChainData> default_filter_chain)
      : xds_client_(xds_client),
        filter_chain_map_(std::move(filter_chain_map)),
        default_filter_chain_(std::move(default_filter_chain)) {}

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

  const RefCountedPtr<XdsClient> xds_client_;
  const XdsApi::LdsUpdate::FilterChainMap filter_chain_map_;
  const absl::optional<XdsApi::LdsUpdate::FilterChainData>
      default_filter_chain_;
  Mutex mu_;
  std::map<const XdsApi::LdsUpdate::FilterChainData*, CertificateProviders>
      certificate_providers_map_ ABSL_GUARDED_BY(mu_);
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

absl::StatusOr<RefCountedPtr<XdsCertificateProvider>>
FilterChainMatchManager::CreateOrGetXdsCertificateProviderFromFilterChainData(
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
          .combined_validation_context
          .validation_context_certificate_provider_instance.instance_name;
  absl::string_view root_provider_cert_name =
      filter_chain->downstream_tls_context.common_tls_context
          .combined_validation_context
          .validation_context_certificate_provider_instance.certificate_name;
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
          .tls_certificate_certificate_provider_instance.instance_name;
  absl::string_view identity_provider_cert_name =
      filter_chain->downstream_tls_context.common_tls_context
          .tls_certificate_certificate_provider_instance.certificate_name;
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

absl::StatusOr<grpc_channel_args*>
FilterChainMatchManager::UpdateChannelArgsForConnection(grpc_channel_args* args,
                                                        grpc_endpoint* tcp) {
  const auto* filter_chain = FindFilterChainDataForDestinationIp(
      filter_chain_map_.destination_ip_vector, tcp);
  if (filter_chain == nullptr && default_filter_chain_.has_value()) {
    filter_chain = &default_filter_chain_.value();
  }
  if (filter_chain == nullptr) {
    grpc_channel_args_destroy(args);
    return absl::UnavailableError("No matching filter chain found");
  }
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

class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  explicit XdsServerConfigFetcher(RefCountedPtr<XdsClient> xds_client,
                                  grpc_server_xds_status_notifier notifier)
      : xds_client_(std::move(xds_client)), serving_status_notifier_(notifier) {
    GPR_ASSERT(xds_client_ != nullptr);
  }

  void StartWatch(std::string listening_address, grpc_channel_args* args,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override {
    grpc_server_config_fetcher::WatcherInterface* watcher_ptr = watcher.get();
    auto listener_watcher = absl::make_unique<ListenerWatcher>(
        std::move(watcher), args, xds_client_, serving_status_notifier_,
        listening_address);
    auto* listener_watcher_ptr = listener_watcher.get();
    listening_address = absl::StrReplaceAll(
        xds_client_->bootstrap().server_listener_resource_name_template(),
        {{"%s", listening_address}});
    xds_client_->WatchListenerData(listening_address,
                                   std::move(listener_watcher));
    MutexLock lock(&mu_);
    auto& watcher_state = watchers_[watcher_ptr];
    watcher_state.listening_address = listening_address;
    watcher_state.listener_watcher = listener_watcher_ptr;
  }

  void CancelWatch(
      grpc_server_config_fetcher::WatcherInterface* watcher) override {
    MutexLock lock(&mu_);
    auto it = watchers_.find(watcher);
    if (it != watchers_.end()) {
      // Cancel the watch on the listener before erasing
      xds_client_->CancelListenerDataWatch(it->second.listening_address,
                                           it->second.listener_watcher,
                                           false /* delay_unsubscription */);
      watchers_.erase(it);
    }
  }

  // Return the interested parties from the xds client so that it can be polled.
  grpc_pollset_set* interested_parties() override {
    return xds_client_->interested_parties();
  }

 private:
  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    explicit ListenerWatcher(
        std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
            server_config_watcher,
        grpc_channel_args* args, RefCountedPtr<XdsClient> xds_client,
        grpc_server_xds_status_notifier serving_status_notifier,
        std::string listening_address)
        : server_config_watcher_(std::move(server_config_watcher)),
          args_(args),
          xds_client_(std::move(xds_client)),
          serving_status_notifier_(serving_status_notifier),
          listening_address_(std::move(listening_address)) {}

    ~ListenerWatcher() override { grpc_channel_args_destroy(args_); }

    // Deleted due to special handling required for args_. Copy the channel args
    // if we ever need these.
    ListenerWatcher(const ListenerWatcher&) = delete;
    ListenerWatcher& operator=(const ListenerWatcher&) = delete;

    void OnListenerChanged(XdsApi::LdsUpdate listener) override {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_server_config_fetcher_trace)) {
        gpr_log(
            GPR_INFO,
            "[ListenerWatcher %p] Received LDS update from xds client %p: %s",
            this, xds_client_.get(), listener.ToString().c_str());
      }
      if (listener.address != listening_address_) {
        OnFatalError(absl::FailedPreconditionError(
            "Address in LDS update does not match listening address"));
        return;
      }
      if (filter_chain_match_manager_ == nullptr) {
        if (serving_status_notifier_.on_serving_status_update != nullptr) {
          serving_status_notifier_.on_serving_status_update(
              serving_status_notifier_.user_data, listening_address_.c_str(),
              GRPC_STATUS_OK, "");
        } else {
          gpr_log(GPR_INFO,
                  "xDS Listener resource obtained; will start serving on %s",
                  listening_address_.c_str());
        }
      }
      if (filter_chain_match_manager_ == nullptr ||
          !(listener.filter_chain_map ==
                filter_chain_match_manager_->filter_chain_map() &&
            listener.default_filter_chain ==
                filter_chain_match_manager_->default_filter_chain())) {
        filter_chain_match_manager_ = MakeRefCounted<FilterChainMatchManager>(
            xds_client_, std::move(listener.filter_chain_map),
            std::move(listener.default_filter_chain));
        server_config_watcher_->UpdateConnectionManager(
            filter_chain_match_manager_);
      }
    }

    void OnError(grpc_error_handle error) override {
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
              GRPC_STATUS_UNAVAILABLE, grpc_error_std_string(error).c_str());
        } else {
          gpr_log(
              GPR_ERROR,
              "ListenerWatcher:%p error obtaining xDS Listener resource: %s; "
              "not serving on %s",
              this, grpc_error_std_string(error).c_str(),
              listening_address_.c_str());
        }
      }
      GRPC_ERROR_UNREF(error);
    }

    void OnFatalError(absl::Status status) {
      gpr_log(
          GPR_ERROR,
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
            static_cast<grpc_status_code>(status.raw_code()),
            std::string(status.message()).c_str());
      }
    }

    void OnResourceDoesNotExist() override {
      OnFatalError(absl::NotFoundError("Requested listener does not exist"));
    }

   private:
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher_;
    grpc_channel_args* args_;
    RefCountedPtr<XdsClient> xds_client_;
    grpc_server_xds_status_notifier serving_status_notifier_;
    std::string listening_address_;
    RefCountedPtr<FilterChainMatchManager> filter_chain_match_manager_;
  };

  struct WatcherState {
    std::string listening_address;
    ListenerWatcher* listener_watcher = nullptr;
  };

  RefCountedPtr<XdsClient> xds_client_;
  grpc_server_xds_status_notifier serving_status_notifier_;
  Mutex mu_;
  std::map<grpc_server_config_fetcher::WatcherInterface*, WatcherState>
      watchers_ ABSL_GUARDED_BY(mu_);
};

}  // namespace
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
