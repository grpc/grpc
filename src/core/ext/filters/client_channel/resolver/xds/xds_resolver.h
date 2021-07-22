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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_XDS_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_XDS_RESOLVER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {

extern const char* kXdsClusterAttribute;

// Client channel map to store the max_retries number for each cluster.
// The map is passed to CDS LB policy which will set the max_retries in the map
// when receiving updates.
#define GRPC_ARG_CLUSTER_MAX_RETRIES_MAP "grpc.xds_cluster_max_retries_map"

class XdsClusterMaxRetriesMap {
 public:
  class ClusterMaxRetries {
   public:
    virtual ~ClusterMaxRetries() = default;

    virtual void SetMaxRetries(uint32_t max_retries) = 0;
  };

  virtual ~XdsClusterMaxRetriesMap() = default;

  virtual std::unique_ptr<ClusterMaxRetries> GetCluster(
      const std::string& cluster) = 0;

  grpc_arg MakeChannelArg() const;

  static XdsClusterMaxRetriesMap* GetFromChannelArgs(
      const grpc_channel_args* args);
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_XDS_RESOLVER_H */
