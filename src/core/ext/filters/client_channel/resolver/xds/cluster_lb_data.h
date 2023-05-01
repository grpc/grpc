//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_CLUSTER_LB_DATA_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_CLUSTER_LB_DATA_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/service_config/service_config_call_data.h"

namespace grpc_core {

// Forward declaration, implementation is private
class XdsClusterMap;
class ClusterState;

class XdsClusterLbData : public ServiceConfigCallData::CallAttributeInterface {
 public:
  explicit XdsClusterLbData(RefCountedPtr<XdsClusterMap> cluster_map);

  bool LockClusterConfig(absl::string_view cluster_name);

  UniqueTypeName type() const override { return type_name(); }

  static XdsClusterLbData* from_call_data(
      const ServiceConfigCallData* call_data) {
    return static_cast<XdsClusterLbData*>(
        call_data->GetCallAttribute(type_name()));
  }

 private:
  static UniqueTypeName type_name() {
    static UniqueTypeName::Factory factory("xds_cluster_lb_data");
    return factory.Create();
  }
  RefCountedPtr<XdsClusterMap> cluster_map_;
  RefCountedPtr<ClusterState> locked_cluster_config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_XDS_CLUSTER_LB_DATA_H
