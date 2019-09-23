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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_CLIENT_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_CLIENT_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/string_view.h"

namespace grpc_core {

extern TraceFlag xds_client_trace;

class XdsClient : public RefCounted<XdsClient> {
 public:
  // Data containing configuration for a cluster.
  struct ClusterData {
  };

  // Data containing locality and endpoint information for a cluster.
  struct EndpointData {
  };

  // Service config watcher interface.  Implemented by callers.
  class ServiceConfigWatcherInterface {
   public:
    virtual ~ServiceConfigWatcherInterface() = default;

    virtual OnServiceConfigChanged(RefCountedPtr<ServiceConfig> service_config)
        GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  // Cluster data watcher interface.  Implemented by callers.
  class ClusterWatcherInterface {
   public:
    virtual ~ClusterWatcherInterface() = default;

    virtual OnClusterChanged(ClusterData cluster_data) GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  // Endpoint data watcher interface.  Implemented by callers.
  class EndpointWatcherInterface {
   public:
    virtual ~EndpointWatcherInterface() = default;

    virtual OnEndpointChanged(EndpointData endpoint_data) GRPC_ABSTRACT;

    GRPC_ABSTRACT_BASE_CLASS
  };

  XdsClient();
  ~XdsClient();

  // Start and cancel service config watch for a server name.
  void WatchServiceConfig(StringView server_name,
                          UniquePtr<ServiceConfigWatcherInterface> watcher);
  void CancelServiceConfigWatch(ServiceConfigWatcherInterface* watcher);

  // Start and cancel cluster data watch for a cluster.
  void WatchClusterData(StringView cluster,
                        UniquePtr<ClusterWatcherInterface> watcher);
  void CancelClusterDataWatch(ClusterWatcherInterface* watcher);

  // Start and cancel endpoint data watch for a cluster.
  void WatchEndpointData(StringView cluster,
                         UniquePtr<EndpointWatcherInterface> watcher);
  void CancelEndpointDataWatch(EndpointWatcherInterface* watcher);

  // Adds and removes client stats for cluster.
  void AddClientStats(StringView cluster, XdsClientStats* client_stats);
  void RemoveClientStats(StringView cluster, XdsClientStats* client_stats);

  // Resets connection backoff.
  void ResetBackoff();

  // Helpers for encoding the XdsClient object in channel args.
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsClient> GetFromChannelArgs(
      const grpc_channel_args& args);
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_XDS_CLIENT_H */
