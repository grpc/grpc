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
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/work_serializer.h"

namespace grpc_core {

extern TraceFlag xds_client_trace;

class XdsClient : public InternallyRefCounted<XdsClient> {
 public:
  // Listener data watcher interface.  Implemented by callers.
  class ListenerWatcherInterface {
   public:
    virtual ~ListenerWatcherInterface() = default;

    virtual void OnListenerChanged(XdsApi::LdsUpdate listener) = 0;

    virtual void OnError(grpc_error* error) = 0;

    virtual void OnResourceDoesNotExist() = 0;
  };

  // RouteConfiguration data watcher interface.  Implemented by callers.
  class RouteConfigWatcherInterface {
   public:
    virtual ~RouteConfigWatcherInterface() = default;

    virtual void OnRouteConfigChanged(XdsApi::RdsUpdate route_config) = 0;

    virtual void OnError(grpc_error* error) = 0;

    virtual void OnResourceDoesNotExist() = 0;
  };

  // Cluster data watcher interface.  Implemented by callers.
  class ClusterWatcherInterface {
   public:
    virtual ~ClusterWatcherInterface() = default;

    virtual void OnClusterChanged(XdsApi::CdsUpdate cluster_data) = 0;

    virtual void OnError(grpc_error* error) = 0;

    virtual void OnResourceDoesNotExist() = 0;
  };

  // Endpoint data watcher interface.  Implemented by callers.
  class EndpointWatcherInterface {
   public:
    virtual ~EndpointWatcherInterface() = default;

    virtual void OnEndpointChanged(XdsApi::EdsUpdate update) = 0;

    virtual void OnError(grpc_error* error) = 0;

    virtual void OnResourceDoesNotExist() = 0;
  };

  // If *error is not GRPC_ERROR_NONE after construction, then there was
  // an error initializing the client.
  XdsClient(std::shared_ptr<WorkSerializer> work_serializer,
            const grpc_channel_args& channel_args, grpc_error** error);
  ~XdsClient();

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

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

  // Helpers for encoding the XdsClient object in channel args.
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsClient> GetFromChannelArgs(
      const grpc_channel_args& args);
  static grpc_channel_args* RemoveFromChannelArgs(
      const grpc_channel_args& args);

 private:
  // Contains a channel to the xds server and all the data related to the
  // channel.  Holds a ref to the xds client object.
  // TODO(roth): This is separate from the XdsClient object because it was
  // originally designed to be able to swap itself out in case the
  // balancer name changed.  Now that the balancer name is going to be
  // coming from the bootstrap file, we don't really need this level of
  // indirection unless we decide to support watching the bootstrap file
  // for changes.  At some point, if we decide that we're never going to
  // need to do that, then we can eliminate this class and move its
  // contents directly into the XdsClient class.
  class ChannelState : public InternallyRefCounted<ChannelState> {
   public:
    template <typename T>
    class RetryableCall;

    class AdsCallState;
    class LrsCallState;

    ChannelState(RefCountedPtr<XdsClient> xds_client, grpc_channel* channel);
    ~ChannelState();

    void Orphan() override;

    grpc_channel* channel() const { return channel_; }
    XdsClient* xds_client() const { return xds_client_.get(); }
    AdsCallState* ads_calld() const;
    LrsCallState* lrs_calld() const;

    void MaybeStartLrsCall();
    void StopLrsCall();

    bool HasActiveAdsCall() const;

    void StartConnectivityWatchLocked();
    void CancelConnectivityWatchLocked();

    void Subscribe(const std::string& type_url, const std::string& name);
    void Unsubscribe(const std::string& type_url, const std::string& name,
                     bool delay_unsubscription);

   private:
    class StateWatcher;

    // The owning xds client.
    RefCountedPtr<XdsClient> xds_client_;

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
  };

  struct RouteConfigState {
    std::map<RouteConfigWatcherInterface*,
             std::unique_ptr<RouteConfigWatcherInterface>>
        watchers;
    // The latest data seen from RDS.
    absl::optional<XdsApi::RdsUpdate> update;
  };

  struct ClusterState {
    std::map<ClusterWatcherInterface*, std::unique_ptr<ClusterWatcherInterface>>
        watchers;
    // The latest data seen from CDS.
    absl::optional<XdsApi::CdsUpdate> update;
  };

  struct EndpointState {
    std::map<EndpointWatcherInterface*,
             std::unique_ptr<EndpointWatcherInterface>>
        watchers;
    // The latest data seen from EDS.
    absl::optional<XdsApi::EdsUpdate> update;
  };

  struct LoadReportState {
    struct LocalityState {
      std::set<XdsClusterLocalityStats*> locality_stats;
      std::vector<XdsClusterLocalityStats::Snapshot> deleted_locality_stats;
    };

    std::set<XdsClusterDropStats*> drop_stats;
    XdsClusterDropStats::DroppedRequestsMap deleted_drop_stats;
    std::map<RefCountedPtr<XdsLocalityName>, LocalityState,
             XdsLocalityName::Less>
        locality_stats;
    grpc_millis last_report_time = ExecCtx::Get()->Now();
  };

  // Sends an error notification to all watchers.
  void NotifyOnError(grpc_error* error);

  XdsApi::ClusterLoadReportMap BuildLoadReportSnapshot(
      bool send_all_clusters, const std::set<std::string>& clusters);

  // Channel arg vtable functions.
  static void* ChannelArgCopy(void* p);
  static void ChannelArgDestroy(void* p);
  static int ChannelArgCmp(void* p, void* q);

  static const grpc_arg_pointer_vtable kXdsClientVtable;

  const grpc_millis request_timeout_;

  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_pollset_set* interested_parties_;

  std::unique_ptr<XdsBootstrap> bootstrap_;
  XdsApi api_;

  // The channel for communicating with the xds server.
  OrphanablePtr<ChannelState> chand_;

  // One entry for each watched LDS resource.
  std::map<std::string /*listener_name*/, ListenerState> listener_map_;
  // One entry for each watched RDS resource.
  std::map<std::string /*route_config_name*/, RouteConfigState>
      route_config_map_;
  // One entry for each watched CDS resource.
  std::map<std::string /*cluster_name*/, ClusterState> cluster_map_;
  // One entry for each watched EDS resource.
  std::map<std::string /*eds_service_name*/, EndpointState> endpoint_map_;

  // Load report data.
  std::map<
      std::pair<std::string /*cluster_name*/, std::string /*eds_service_name*/>,
      LoadReportState>
      load_report_map_;

  bool shutting_down_ = false;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_CLIENT_H */
