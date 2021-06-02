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
  // Listener data watcher interface.  Implemented by callers.
  class ListenerWatcherInterface {
   public:
    virtual ~ListenerWatcherInterface() = default;
    virtual void OnListenerChanged(XdsApi::LdsUpdate listener) = 0;
    virtual void OnError(grpc_error_handle error) = 0;
    virtual void OnResourceDoesNotExist() = 0;
  };

  // RouteConfiguration data watcher interface.  Implemented by callers.
  class RouteConfigWatcherInterface {
   public:
    virtual ~RouteConfigWatcherInterface() = default;
    virtual void OnRouteConfigChanged(XdsApi::RdsUpdate route_config) = 0;
    virtual void OnError(grpc_error_handle error) = 0;
    virtual void OnResourceDoesNotExist() = 0;
  };

  // Cluster data watcher interface.  Implemented by callers.
  class ClusterWatcherInterface {
   public:
    virtual ~ClusterWatcherInterface() = default;
    virtual void OnClusterChanged(XdsApi::CdsUpdate cluster_data) = 0;
    virtual void OnError(grpc_error_handle error) = 0;
    virtual void OnResourceDoesNotExist() = 0;
  };

  // Endpoint data watcher interface.  Implemented by callers.
  class EndpointWatcherInterface {
   public:
    virtual ~EndpointWatcherInterface() = default;
    virtual void OnEndpointChanged(XdsApi::EdsUpdate update) = 0;
    virtual void OnError(grpc_error_handle error) = 0;
    virtual void OnResourceDoesNotExist() = 0;
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

  // TODO(roth): When we add federation, there will be multiple channels
  // inside the XdsClient, and the set of channels may change over time,
  // but not every channel may use every one of the child channels, so
  // this API will need to change.  At minumum, we will need to hold a
  // ref to the parent channelz node so that we can update its list of
  // children as the set of xDS channels changes.  However, we may also
  // want to make this a bit more selective such that only those
  // channels on which a given parent channel is actually requesting
  // resources will actually be marked as its children.
  void AddChannelzLinkage(channelz::ChannelNode* parent_channelz_node);
  void RemoveChannelzLinkage(channelz::ChannelNode* parent_channelz_node);

  void Orphan() override;

  // Start and cancel listener data watch for a listener.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  void WatchListenerData(absl::string_view listener_name,
                         std::unique_ptr<ListenerWatcherInterface> watcher);
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
  void WatchRouteConfigData(
      absl::string_view route_config_name,
      std::unique_ptr<RouteConfigWatcherInterface> watcher);
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
                        std::unique_ptr<ClusterWatcherInterface> watcher);
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
                         std::unique_ptr<EndpointWatcherInterface> watcher);
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
  // Contains a channel to the xds server and all the data related to the
  // channel.  Holds a ref to the xds client object.
  //
  // Currently, there is only one ChannelState object per XdsClient
  // object, and it has essentially the same lifetime.  But in the
  // future, when we add federation support, a single XdsClient may have
  // multiple underlying channels to talk to different xDS servers.
  class ChannelState : public InternallyRefCounted<ChannelState> {
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

    void SubscribeLocked(const std::string& type_url, const std::string& name)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
    void UnsubscribeLocked(const std::string& type_url, const std::string& name,
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
    StateWatcher* watcher_ = nullptr;

    // The retryable XDS calls.
    OrphanablePtr<RetryableCall<AdsCallState>> ads_calld_;
    OrphanablePtr<RetryableCall<LrsCallState>> lrs_calld_;
  };

  struct ListenerState {
    std::map<ListenerWatcherInterface*,
             std::unique_ptr<ListenerWatcherInterface>>
        watchers;
    // The latest data seen from LDS.
    absl::optional<XdsApi::LdsUpdate> update;
    XdsApi::ResourceMetadata meta;
  };

  struct RouteConfigState {
    std::map<RouteConfigWatcherInterface*,
             std::unique_ptr<RouteConfigWatcherInterface>>
        watchers;
    // The latest data seen from RDS.
    absl::optional<XdsApi::RdsUpdate> update;
    XdsApi::ResourceMetadata meta;
  };

  struct ClusterState {
    std::map<ClusterWatcherInterface*, std::unique_ptr<ClusterWatcherInterface>>
        watchers;
    // The latest data seen from CDS.
    absl::optional<XdsApi::CdsUpdate> update;
    XdsApi::ResourceMetadata meta;
  };

  struct EndpointState {
    std::map<EndpointWatcherInterface*,
             std::unique_ptr<EndpointWatcherInterface>>
        watchers;
    // The latest data seen from EDS.
    absl::optional<XdsApi::EdsUpdate> update;
    XdsApi::ResourceMetadata meta;
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

  // Sends an error notification to all watchers.
  void NotifyOnErrorLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  XdsApi::ClusterLoadReportMap BuildLoadReportSnapshotLocked(
      bool send_all_clusters, const std::set<std::string>& clusters)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void UpdateResourceMetadataWithFailedParseResultLocked(
      grpc_millis update_time, const XdsApi::AdsParseResult& result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::unique_ptr<XdsBootstrap> bootstrap_;
  grpc_channel_args* args_;
  const grpc_millis request_timeout_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<CertificateProviderStore> certificate_provider_store_;
  XdsApi api_;

  Mutex mu_;

  // The channel for communicating with the xds server.
  OrphanablePtr<ChannelState> chand_ ABSL_GUARDED_BY(mu_);

  // One entry for each watched LDS resource.
  std::map<std::string /*listener_name*/, ListenerState> listener_map_
      ABSL_GUARDED_BY(mu_);
  // One entry for each watched RDS resource.
  std::map<std::string /*route_config_name*/, RouteConfigState>
      route_config_map_ ABSL_GUARDED_BY(mu_);
  // One entry for each watched CDS resource.
  std::map<std::string /*cluster_name*/, ClusterState> cluster_map_
      ABSL_GUARDED_BY(mu_);
  // One entry for each watched EDS resource.
  std::map<std::string /*eds_service_name*/, EndpointState> endpoint_map_
      ABSL_GUARDED_BY(mu_);

  // Load report data.
  std::map<
      std::pair<std::string /*cluster_name*/, std::string /*eds_service_name*/>,
      LoadReportState>
      load_report_map_ ABSL_GUARDED_BY(mu_);

  // Stores the most recent accepted resource version for each resource type.
  std::map<std::string /*type*/, std::string /*version*/> resource_version_map_
      ABSL_GUARDED_BY(mu_);

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
