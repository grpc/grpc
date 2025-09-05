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

#include "src/core/xds/grpc/xds_ecds.h"

#include "absl/strings/str_cat.h"

namespace grpc_core {

using HttpFilter = XdsListenerResource::HttpConnectionManager::HttpFilter;

std::string XdsEcdsResource::ToString() const {
  return absl::StrCat("{config=", config.ToString(), "}");
}

const XdsHttpFilterImpl::FilterConfig& GetHttpFilterConfig(
    const HttpFilter& http_filter,
    const std::map<absl::string_view, std::shared_ptr<const XdsEcdsResource>>&
        ecds_resources) {
  return Match(
      http_filter.config,
      [&](const XdsHttpFilterImpl::FilterConfig& config)
          -> const XdsHttpFilterImpl::FilterConfig& {
        return config;
      },
      [&](const HttpFilter::UseEcds&)
          -> const XdsHttpFilterImpl::FilterConfig& {
        // Lookup the ECDS resource.  This is guaranteed to succeed,
        // because it's checked in the XdsDependencyManager.
        auto it = ecds_resources.find(http_filter.name);
        CHECK(it != ecds_resources.end());
        return it->second->config;
      });
}

}  // namespace grpc_core
