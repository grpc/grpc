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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_ECDS_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_ECDS_H

#include <map>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "src/core/xds/xds_client/xds_resource_type_impl.h"

namespace grpc_core {

struct XdsEcdsResource : public XdsResourceType::ResourceData {
  XdsHttpFilterImpl::FilterConfig config;
  std::set<std::string> ecds_resources_needed;

  bool operator==(const XdsEcdsResource& other) const {
    return config == other.config &&
           ecds_resources_needed == other.ecds_resources_needed;
  }

  std::string ToString() const;
};

// Utility function to get an HTTP filter config, either inlined or via
// a separate ECDS resource map.
const XdsHttpFilterImpl::FilterConfig& GetHttpFilterConfig(
    const XdsListenerResource::HttpConnectionManager::HttpFilter& http_filter,
    const std::map<absl::string_view, std::shared_ptr<const XdsEcdsResource>>&
        ecds_resources);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_ECDS_H
