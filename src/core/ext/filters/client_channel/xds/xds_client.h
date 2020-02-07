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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_CLIENT_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_CLIENT_H

#include <grpc/support/port_platform.h>

#include <set>

#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/ext/filters/client_channel/xds/xds_bootstrap.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/optional.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/string_view.h"
#include "src/core/lib/iomgr/combiner.h"

namespace grpc_core {

extern TraceFlag xds_client_trace;

class XdsClient : public InternallyRefCounted<XdsClient> {
 public:
  // Service config watcher interface.  Implemented by callers.
  class ServiceConfigWatcherInterface {
   public:
    virtual ~ServiceConfigWatcherInterface() = default;

    virtual void OnServiceConfigChanged(
        RefCountedPtr<ServiceConfig> service_config) = 0;

    virtual void OnError(grpc_error* error) = 0;
  };

  // Cluster data watcher interface.  Implemented by callers.
  class ClusterWatcherInterface {
   public:
    virtual ~ClusterWatcherInterface() = default;

    virtual void OnClusterChanged(XdsApi::CdsUpdate cluster_data) = 0;

    virtual void OnError(grpc_error* error) = 0;
  };

  // Endpoint data watcher interface.  Implemented by callers.
  class EndpointWatcherInterface {
   public:
    virtual ~EndpointWatcherInterface() = default;

    virtual void OnEndpointChanged(XdsApi::EdsUpdate update) = 0;

    virtual void OnError(grpc_error* error) = 0;
  };

  // If *error is not GRPC_ERROR_NONE after construction, then there was
  // an error initializing the client.
  XdsClient(Combiner* combiner, grpc_pollset_set* interested_parties,
            StringView server_name,
            std::unique_ptr<ServiceConfigWatcherInterface> watcher,
            const grpc_channel_args& channel_args, grpc_error** error);
  ~XdsClient();

  void Orphan() override;

  // Start and cancel cluster data watch for a cluster.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  void WatchClusterData(StringView cluster_name,
                        std::unique_ptr<ClusterWatcherInterface> watcher);
  void CancelClusterDataWatch(StringView cluster_name,
                              ClusterWatcherInterface* watcher);

  // Start and cancel endpoint data watch for a cluster.
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  void WatchEndpointData(StringView eds_service_name,
                         std::unique_ptr<EndpointWatcherInterface> watcher);
  void CancelEndpointDataWatch(StringView eds_service_name,
                               EndpointWatcherInterface* watcher);

  // Adds and removes client stats for \a cluster_name.
  void AddClientStats(StringView /*lrs_server*/, StringView cluster_name,
                      XdsClientStats* client_stats);
  void RemoveClientStats(StringView /*lrs_server*/, StringView cluster_name,
                         XdsClientStats* client_stats);

  // Resets connection backoff state.
  void ResetBackoff();

  // Helpers for encoding the XdsClient object in channel args.
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsClient> GetFromChannelArgs(
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
    void Unsubscribe(const std::string& type_url, const std::string& name);

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

  struct ClusterState {
    std::map<ClusterWatcherInterface*, std::unique_ptr<ClusterWatcherInterface>>
        watchers;
    // The latest data seen from CDS.
    Optional<XdsApi::CdsUpdate> update;
  };

  struct EndpointState {
    std::map<EndpointWatcherInterface*,
             std::unique_ptr<EndpointWatcherInterface>>
        watchers;
    std::set<XdsClientStats*> client_stats;
    // The latest data seen from EDS.
    XdsApi::EdsUpdate update;
  };

  // Sends an error notification to all watchers.
  void NotifyOnError(grpc_error* error);

  grpc_error* CreateServiceConfig(
      const std::string& cluster_name,
      RefCountedPtr<ServiceConfig>* service_config) const;

  std::map<StringView, std::set<XdsClientStats*>, StringLess> ClientStatsMap()
      const;

  // Channel arg vtable functions.
  static void* ChannelArgCopy(void* p);
  static void ChannelArgDestroy(void* p);
  static int ChannelArgCmp(void* p, void* q);

  static const grpc_arg_pointer_vtable kXdsClientVtable;

  const grpc_millis request_timeout_;

  grpc_core::UniquePtr<char> build_version_;

  Combiner* combiner_;
  grpc_pollset_set* interested_parties_;

  std::unique_ptr<XdsBootstrap> bootstrap_;
  XdsApi api_;

  const std::string server_name_;

  std::unique_ptr<ServiceConfigWatcherInterface> service_config_watcher_;

  // The channel for communicating with the xds server.
  OrphanablePtr<ChannelState> chand_;

  std::string route_config_name_;
  std::string cluster_name_;
  // All the received clusters are cached, no matter they are watched or not.
  std::map<std::string /*cluster_name*/, ClusterState> cluster_map_;
  // Only the watched EDS service names are stored.
  std::map<std::string /*eds_service_name*/, EndpointState> endpoint_map_;

  bool shutting_down_ = false;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_CLIENT_H */
