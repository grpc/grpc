//
//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_SERVER_CONFIG_FETCHER_H
#define GRPC_CORE_EXT_XDS_XDS_SERVER_CONFIG_FETCHER_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {

class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  class FilterChainMatchManager
      : public grpc_server_config_fetcher::ConnectionManager {
   public:
    FilterChainMatchManager(
        XdsServerConfigFetcher* server_config_fetcher,
        XdsApi::LdsUpdate::FilterChainMap filter_chain_map,
        absl::optional<XdsApi::LdsUpdate::FilterChainData> default_filter_chain,
        std::vector<std::string> resource_names)
        : server_config_fetcher_(server_config_fetcher),
          filter_chain_map_(std::move(filter_chain_map)),
          default_filter_chain_(std::move(default_filter_chain)),
          resource_names_(resource_names) {}

    ~FilterChainMatchManager() override;

    absl::StatusOr<grpc_server_config_fetcher::ConnectionConfiguration>
    UpdateChannelArgsForConnection(grpc_channel_args* args,
                                   grpc_endpoint* tcp) override;

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

    XdsServerConfigFetcher* server_config_fetcher_;
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
    virtual void OnRdsUpdate(absl::StatusOr<XdsApi::RdsUpdate> rds_update) = 0;
  };

  explicit XdsServerConfigFetcher(RefCountedPtr<XdsClient> xds_client,
                                  grpc_server_xds_status_notifier notifier);

  void StartWatch(std::string listening_address,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override;

  void CancelWatch(
      grpc_server_config_fetcher::WatcherInterface* watcher) override;

  absl::optional<absl::StatusOr<XdsApi::RdsUpdate>> StartRdsWatch(
      absl::string_view resource_name,
      std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface>
          watcher);

  void CancelRdsWatch(
      absl::string_view resource_name,
      XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher);

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
        XdsServerConfigFetcher* server_config_fetcher,
        grpc_server_xds_status_notifier serving_status_notifier,
        std::string listening_address);

    // Deleted due to special handling required for args_. Copy the channel args
    // if we ever need these.
    ListenerWatcher(const ListenerWatcher&) = delete;
    ListenerWatcher& operator=(const ListenerWatcher&) = delete;

    void OnListenerChanged(XdsApi::LdsUpdate listener) override;

    void OnError(grpc_error_handle error) override;

    void OnResourceDoesNotExist() override;

   private:
    class RdsUpdateWatcher : public RdsUpdateWatcherInterface {
     public:
      RdsUpdateWatcher(std::string resource_name, ListenerWatcher* parent);
      void OnRdsUpdate(absl::StatusOr<XdsApi::RdsUpdate> rds_update) override
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(
              parent_->server_config_fetcher_->rds_mu_);

     private:
      std::string resource_name_;
      ListenerWatcher* parent_;
    };

    void OnFatalError(absl::Status status);

    void UpdateFilterChainMatchManagerLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    const std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher_;
    XdsServerConfigFetcher* server_config_fetcher_;
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

  class RouteConfigWatcher : public XdsClient::RouteConfigWatcherInterface {
   public:
    explicit RouteConfigWatcher(absl::string_view resource_name,
                                XdsServerConfigFetcher* server_config_fetcher)
        : resource_name_(resource_name),
          server_config_fetcher_(server_config_fetcher) {}

    void OnRouteConfigChanged(XdsApi::RdsUpdate route_config) override;
    void OnError(grpc_error_handle error) override;
    void OnResourceDoesNotExist() override;

   private:
    std::string resource_name_;
    XdsServerConfigFetcher* server_config_fetcher_;
  };

  struct RdsUpdateWatcherState {
    std::map<RdsUpdateWatcherInterface*,
             std::unique_ptr<RdsUpdateWatcherInterface>>
        rds_watchers;
    RouteConfigWatcher* route_config_watcher = nullptr;
    absl::optional<absl::StatusOr<XdsApi::RdsUpdate>> rds_update;
    int listener_refs = 0;
  };

  absl::optional<absl::StatusOr<XdsApi::RdsUpdate>> StartRdsWatchInternal(
      absl::string_view resource_name,
      std::unique_ptr<XdsServerConfigFetcher::RdsUpdateWatcherInterface>
          watcher,
      bool inc_ref);
  void CancelRdsWatchInternal(
      absl::string_view resource_name,
      XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher, bool dec_ref);
  void CancelRdsWatchInternalLocked(
      absl::string_view resource_name,
      XdsServerConfigFetcher::RdsUpdateWatcherInterface* watcher, bool dec_ref)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(rds_mu_);

  RefCountedPtr<XdsClient> xds_client_;
  grpc_server_xds_status_notifier serving_status_notifier_;
  Mutex mu_;
  std::map<grpc_server_config_fetcher::WatcherInterface*, WatcherState>
      listener_watchers_ ABSL_GUARDED_BY(mu_);
  Mutex rds_mu_;
  std::map<std::string, RdsUpdateWatcherState> rds_watchers_
      ABSL_GUARDED_BY(rds_mu_);
  // long fake_rds_name_counter = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_SERVER_CONFIG_FETCHER_H
