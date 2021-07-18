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

#ifndef GRPC_CORE_EXT_XDS_XDS_MAX_RETRIES_MAP_H
#define GRPC_CORE_EXT_XDS_XDS_MAX_RETRIES_MAP_H

#include <grpc/support/port_platform.h>

#include <map>

#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted.h"

namespace grpc_core {

class XdsMaxRetriesMap : public RefCounted<XdsMaxRetriesMap> {
 public:
  XdsMaxRetriesMap() {}
  void XdsMaxRetriesMapAdd(const std::string cluster, uint32_t max_retries) {
    cluster_max_retries_map_[cluster] = max_retries;
  }
  bool XdsMaxRetriesMapUpdate(const std::string cluster, uint32_t max_retries) {
    auto iter = cluster_max_retries_map_.find(cluster);
    if (iter == cluster_max_retries_map_.end()) return false;
    iter->second = max_retries;
    return true;
  }
  grpc_arg MakeChannelArg() const;
  static RefCountedPtr<XdsMaxRetriesMap> GetFromChannelArgs(
      const grpc_channel_args* args);

 private:
  std::map<std::string, uint32_t> cluster_max_retries_map_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_MAX_RETRIES_MAP_H
