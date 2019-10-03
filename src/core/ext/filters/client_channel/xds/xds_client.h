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

#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/set.h"
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

    virtual void OnClusterChanged(CdsUpdate cluster_data) = 0;

    virtual void OnError(grpc_error* error) = 0;
  };

  // Endpoint data watcher interface.  Implemented by callers.
  class EndpointWatcherInterface {
   public:
    virtual ~EndpointWatcherInterface() = default;

    virtual void OnEndpointChanged(EdsUpdate update) = 0;

    virtual void OnError(grpc_error* error) = 0;
  };

  XdsClient(grpc_combiner* combiner, grpc_pollset_set* interested_parties,
            const char* balancer_name, StringView server_name,
            UniquePtr<ServiceConfigWatcherInterface> watcher,
            const grpc_channel_args& channel_args);
  ~XdsClient();

  void Orphan() override;

  // Start and cancel cluster data watch for a cluster.
  void WatchClusterData(StringView cluster,
                        UniquePtr<ClusterWatcherInterface> watcher);
  void CancelClusterDataWatch(StringView cluster,
                              ClusterWatcherInterface* watcher);

  // Start and cancel endpoint data watch for a cluster.
  void WatchEndpointData(StringView cluster,
                         UniquePtr<EndpointWatcherInterface> watcher);
  void CancelEndpointDataWatch(StringView cluster,
                               EndpointWatcherInterface* watcher);

  // Adds and removes client stats for cluster.
  void AddClientStats(StringView cluster, XdsClientStats* client_stats);
  void RemoveClientStats(StringView cluster, XdsClientStats* client_stats);

  // Resets connection backoff state.
  void ResetBackoff();

  // Helpers for encoding the XdsClient object in channel args.
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsClient> GetFromChannelArgs(
      const grpc_channel_args& args);

 private:
  class ChannelState;

  struct ClusterState {
    Map<ClusterWatcherInterface*, UniquePtr<ClusterWatcherInterface>>
        cluster_watchers;
    Map<EndpointWatcherInterface*, UniquePtr<EndpointWatcherInterface>>
        endpoint_watchers;
    Set<XdsClientStats*> client_stats;
    // The latest data seen from EDS.
    EdsUpdate eds_update;
  };

  // Sends an error notification to all watchers.
  void NotifyOnError(grpc_error* error);

  // Channel arg vtable functions.
  static void* ChannelArgCopy(void* p);
  static void ChannelArgDestroy(void* p);
  static int ChannelArgCmp(void* p, void* q);

  static const grpc_arg_pointer_vtable kXdsClientVtable;

  grpc_combiner* combiner_;
  grpc_pollset_set* interested_parties_;

  UniquePtr<char> server_name_;
  UniquePtr<ServiceConfigWatcherInterface> service_config_watcher_;

  // The channel for communicating with the xds server.
  OrphanablePtr<ChannelState> chand_;

  // TODO(roth): When we need support for multiple clusters, replace
  // cluster_state_ with a map keyed by cluster name.
  ClusterState cluster_state_;
  // Map<StringView /*cluster*/, ClusterState, StringLess> clusters_;

  bool shutting_down_ = false;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_XDS_CLIENT_H */
