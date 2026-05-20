//
// Copyright 2025 gRPC authors.
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

#include "src/core/xds/grpc/xds_http_filter.h"

namespace grpc_core {

RefCountedPtr<const FilterConfig> XdsHttpFilterImpl::MergeConfigs(
    RefCountedPtr<const FilterConfig> top_level_config,
    RefCountedPtr<const FilterConfig> virtual_host_override_config,
    RefCountedPtr<const FilterConfig> route_override_config,
    RefCountedPtr<const FilterConfig> cluster_weight_override_config) const {
  if (cluster_weight_override_config != nullptr) {
    return cluster_weight_override_config;
  }
  if (route_override_config != nullptr) {
    return route_override_config;
  }
  if (virtual_host_override_config != nullptr) {
    return virtual_host_override_config;
  }
  return top_level_config;
}

}  // namespace grpc_core
