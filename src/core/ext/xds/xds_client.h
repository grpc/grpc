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

#ifndef GRPC_CORE_EXT_XDS_XDS_CLIENT_H
#define GRPC_CORE_EXT_XDS_XDS_CLIENT_H

#include <grpc/support/port_platform.h>

#include <set>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

extern TraceFlag grpc_xds_client_trace;
extern TraceFlag grpc_xds_client_refcount_trace;

class XdsClient : public DualRefCounted<XdsClient> {
 public:
  // Resource watcher interface.  Implemented by callers.
  class ResourceWatcherInterface : public RefCounted<ResourceWatcherInterface> {
   public:
    virtual void OnResourceChanged(
        const XdsResourceType::ResourceData* resource)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) = 0;
    virtual void OnError(grpc_error_handle error)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) = 0;
    virtual void OnResourceDoesNotExist()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) = 0;
  };

  // TODO(roth): Consider removing these resource-type-specific APIs in
  // favor of some mechanism for automatic type-deduction for the generic
  // API.

  // Listener data watcher interface.  Implemented by callers.
  class ListenerWatcherInterface : public ResourceWatcherInterface {
   public:
    virtual void OnListenerChanged(XdsListenerResource listener) = 0;
   private:
    void OnResourceChanged(const XdsResourceType::ResourceData* resource)
        override {
      OnListenerChanged(
          static_cast<const XdsListenerResourceType::ListenerData*>(resource)
              ->resource);
    }
  };

  // RouteConfiguration data watcher interface.  Implemented by callers.
  class RouteConfigWatcherInterface : public ResourceWatcherInterface {
   public:
    virtual void OnRouteConfigChanged(XdsRouteConfigResource route_config) = 0;
   private:
    void OnResourceChanged(const XdsResourceType::ResourceData* resource)
        override {
      OnRouteConfigChanged(
          static_cast<const XdsRouteConfigResourceType::RouteConfigData*>(resource)
              ->resource);
    }
  };

  // Cluster data watcher interface.  Implemented by callers.
  class ClusterWatcherInterface : public ResourceWatcherInterface {
   public:
    virtual void OnClusterChanged(XdsClusterResource cluster_data) = 0;
   private:
    void OnResourceChanged(const XdsResourceType::ResourceData* resource)
        override {
      OnClusterChanged(
          static_cast<const XdsClusterResourceType::ClusterData*>(resource)
              ->resource);
    }
  };

  // Endpoint data watcher interface.  Implemented by callers.
  class EndpointWatcherInterface : public ResourceWatcherInterface {
   public:
    virtual void OnEndpointChanged(XdsEndpointResource update) = 0;
   private:
    void OnResourceChanged(const XdsResourceType::ResourceData* resource)
        override {
      OnEndpointChanged(
          static_cast<const XdsEndpointResourceType::EndpointData*>(resource)
              ->resource);
    }
  };

  // Factory function to get or create the global XdsClient instance.
  // If *error is not GRPC_ERROR_NONE upon return, then there was
  // an error initializing the client.
  static RefCountedPtr<XdsClient> GetOrCreate(const grpc_channel_args* args,
                                              grpc_error_handle* error);

  // Most callers should not instantiate directly.  Use GetOrCreate() instead.
  XdsClient(std::unique_ptr<XdsBootstrap> bootstrap,
            const grpc_channel_args* args);
  ~XdsClient() override;

  const XdsBootstrap& bootstrap() const {
    // bootstrap_ is guaranteed to be non-null since XdsClient::GetOrCreate()
    // would return a null object if bootstrap_ was null.
    return *bootstrap_;
  }

  CertificateProviderStore& certificate_provider_store() {
    return *certificate_provider_store_;
  }

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

  void Orphan() override;

  // Start and cancel watch for a resource.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  void WatchResource(const XdsResourceType* type, absl::string_view name,
                     RefCountedPtr<ResourceWatcherInterface> watcher);
  void CancelResourceWatch(const XdsResourceType* type, 
                           absl::string_view listener_name,
                           ResourceWatcherInterface* watcher,
                           bool delay_unsubscription = false);

  // Start and cancel listener data watch for a listener.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  void WatchListenerData(absl::string_view listener_name,
                         RefCountedPtr<ListenerWatcherInterface> watcher);
  void CancelListenerDataWatch(absl::string_view listener_name,
                               ListenerWatcherInterface* watcher,
                               bool delay_unsubscription = false);

  // Start and cancel route config data watch for a listener.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  void WatchRouteConfigData(absl::string_view route_config_name,
                            RefCountedPtr<RouteConfigWatcherInterface> watcher);
  void CancelRouteConfigDataWatch(absl::string_view route_config_name,
                                  RouteConfigWatcherInterface* watcher,
                                  bool delay_unsubscription = false);

  // Start and cancel cluster data watch for a cluster.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  void WatchClusterData(absl::string_view cluster_name,
                        RefCountedPtr<ClusterWatcherInterface> watcher);
  void CancelClusterDataWatch(absl::string_view cluster_name,
                              ClusterWatcherInterface* watcher,
                              bool delay_unsubscription = false);

  // Start and cancel endpoint data watch for a cluster.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  void WatchEndpointData(absl::string_view eds_service_name,
                         RefCountedPtr<EndpointWatcherInterface> watcher);
  void CancelEndpointDataWatch(absl::string_view eds_service_name,
                               EndpointWatcherInterface* watcher,
                               bool delay_unsubscription = false);

  // Adds and removes drop stats for cluster_name and eds_service_name.
  RefCountedPtr<XdsClusterDropStats> AddClusterDropStats(
      absl::string_view lrs_server, absl::string_view cluster_name,
      absl::string_view eds_service_name);
  void RemoveClusterDropStats(absl::string_view /*lrs_server*/,
                              absl::string_view cluster_name,
                              absl::string_view eds_service_name,
                              XdsClusterDropStats* cluster_drop_stats);

  // Adds and removes locality stats for cluster_name and eds_service_name
  // for the specified locality.
  RefCountedPtr<XdsClusterLocalityStats> AddClusterLocalityStats(
      absl::string_view lrs_server, absl::string_view cluster_name,
      absl::string_view eds_service_name,
      RefCountedPtr<XdsLocalityName> locality);
  void RemoveClusterLocalityStats(
      absl::string_view /*lrs_server*/, absl::string_view cluster_name,
      absl::string_view eds_service_name,
      const RefCountedPtr<XdsLocalityName>& locality,
      XdsClusterLocalityStats* cluster_locality_stats);

  // Resets connection backoff state.
  void ResetBackoff();

  // Dumps the active xDS config in JSON format.
  // Individual xDS resource is encoded as envoy.admin.v3.*ConfigDump. Returns
  // envoy.service.status.v3.ClientConfig which also includes the config
  // status (e.g., CLIENT_REQUESTED, CLIENT_ACKED, CLIENT_NACKED).
  //
  // Expected to be invoked by wrapper languages in their CSDS service
  // implementation.
  std::string DumpClientConfigBinary();

  // Helpers for encoding the XdsClient object in channel args.
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsClient> GetFromChannelArgs(
      const grpc_channel_args& args);

 private:
  struct XdsResourceName {
    std::string authority;
    std::string id;
  };

  // Contains a channel to the xds server and all the data related to the
  // channel.  Holds a ref to the xds client object.
  class ChannelState : public DualRefCounted<ChannelState> {
   public:
    template <typename T>
    class RetryableCall;

    class AdsCallState;
    class LrsCallState;

    ChannelState(WeakRefCountedPtr<XdsClient> xds_client,
                 const XdsBootstrap::XdsServer& server);
    ~ChannelState() override;

    void Orphan() override;

    grpc_channel* channel() const { return channel_; }
    XdsClient* xds_client() const { return xds_client_.get(); }
    AdsCallState* ads_calld() const;
    LrsCallState* lrs_calld() const;

    void MaybeStartLrsCall();
    void StopLrsCall();

    bool HasAdsCall() const;
    bool HasActiveAdsCall() const;

    void StartConnectivityWatchLocked();
    void CancelConnectivityWatchLocked();

    void SubscribeLocked(const XdsResourceType* type,
                         const XdsResourceName& name)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
    void UnsubscribeLocked(const XdsResourceType* type,
                           const XdsResourceName& name,
                           bool delay_unsubscription)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

   private:
    class StateWatcher;

    // The owning xds client.
    WeakRefCountedPtr<XdsClient> xds_client_;

    const XdsBootstrap::XdsServer& server_;

    // The channel and its status.
    grpc_channel* channel_;
    bool shutting_down_ = false;
    StateWatcher* watcher_;

    // The retryable XDS calls.
    OrphanablePtr<RetryableCall<AdsCallState>> ads_calld_;
    OrphanablePtr<RetryableCall<LrsCallState>> lrs_calld_;

    // Stores the most recent accepted resource version for each resource type.
    std::map<const XdsResourceType*, std::string /*version*/>
        resource_type_version_map_;
  };

  struct ResourceState {
    std::map<ResourceWatcherInterface*, RefCountedPtr<ResourceWatcherInterface>>
        watchers;
    // The latest data seen for the resource.
    std::unique_ptr<XdsResourceType::ResourceData> resource;
    XdsApi::ResourceMetadata meta;
  };

  struct AuthorityState {
    RefCountedPtr<ChannelState> channel_state;

    std::map<const XdsResourceType*,
             std::map<std::string /*id*/, ResourceState>> resource_map;

// FIXME: remove?
    bool HasSubscribedResources() { return !resource_map.empty(); }
  };

  struct LoadReportState {
    struct LocalityState {
      XdsClusterLocalityStats* locality_stats = nullptr;
      XdsClusterLocalityStats::Snapshot deleted_locality_stats;
    };

    XdsClusterDropStats* drop_stats = nullptr;
    XdsClusterDropStats::Snapshot deleted_drop_stats;
    std::map<RefCountedPtr<XdsLocalityName>, LocalityState,
             XdsLocalityName::Less>
        locality_stats;
    grpc_millis last_report_time = ExecCtx::Get()->Now();
  };

  class Notifier;

  // Sends an error notification to all watchers.
  void NotifyOnErrorLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  static absl::StatusOr<XdsResourceName> ParseXdsResourceName(
      absl::string_view name, const XdsResourceType* type);
  static std::string ConstructFullXdsResourceName(absl::string_view authority,
                                                  absl::string_view resource_type,
                                                  absl::string_view id);

  XdsApi::ClusterLoadReportMap BuildLoadReportSnapshotLocked(
      bool send_all_clusters, const std::set<std::string>& clusters)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  RefCountedPtr<ChannelState> GetOrCreateChannelStateLocked(
      const XdsBootstrap::XdsServer& server) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::unique_ptr<XdsBootstrap> bootstrap_;
  grpc_channel_args* args_;
  const grpc_millis request_timeout_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<CertificateProviderStore> certificate_provider_store_;
  XdsApi api_;
  WorkSerializer work_serializer_;

  Mutex mu_;

  //  Map of existing xDS server channels.
  std::map<XdsBootstrap::XdsServer, ChannelState*> xds_server_channel_map_
      ABSL_GUARDED_BY(mu_);

  std::map<std::string /*authority*/, AuthorityState> authority_state_map_
      ABSL_GUARDED_BY(mu_);

  // Load report data.
  std::map<
      std::pair<std::string /*cluster_name*/, std::string /*eds_service_name*/>,
      LoadReportState>
      load_report_map_ ABSL_GUARDED_BY(mu_);

  // Stores started watchers whose resource name was not parsed successfully,
  // waiting to be cancelled or reset in Orphan().
  std::map<ResourceWatcherInterface*, RefCountedPtr<ResourceWatcherInterface>>
      invalid_watchers_ ABSL_GUARDED_BY(mu_);

  bool shutting_down_ ABSL_GUARDED_BY(mu_) = false;
};

namespace internal {
void SetXdsChannelArgsForTest(grpc_channel_args* args);
void UnsetGlobalXdsClientForTest();
// Sets bootstrap config to be used when no env var is set.
// Does not take ownership of config.
void SetXdsFallbackBootstrapConfig(const char* config);
}  // namespace internal

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_CLIENT_H
