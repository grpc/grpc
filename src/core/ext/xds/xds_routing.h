//
//
// Copyright 2021 gRPC authors.
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
//

#ifndef GRPC_CORE_EXT_XDS_XDS_ROUTING_H
#define GRPC_CORE_EXT_XDS_XDS_ROUTING_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_api.h"
#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

class XdsRouting {
 public:
  class VirtualHostListIterator {
   public:
    virtual ~VirtualHostListIterator() = default;
    // Returns the number of virtual hosts in the list.
    virtual size_t Size() const = 0;
    // Returns the domain list for the virtual host at the specified index.
    virtual const std::vector<std::string>& GetDomainsForVirtualHost(
        size_t index) const = 0;
  };

  class RouteListIterator {
   public:
    virtual ~RouteListIterator() = default;
    // Number of routes.
    virtual size_t Size() const = 0;
    // Returns the matchers for the route at the specified index.
    virtual const XdsApi::Route::Matchers& GetMatchersForRoute(
        size_t index) const = 0;
  };

  // Returns the index of the selected virtual host in the list.
  static absl::optional<size_t> FindVirtualHostForDomain(
      const VirtualHostListIterator& vhost_iterator, absl::string_view domain);

  // Returns the index in route_list_iterator to use for a request with
  // the specified path and metadata, or nullopt if no route matches.
  static absl::optional<size_t> GetRouteForRequest(
      const RouteListIterator& route_list_iterator, absl::string_view path,
      grpc_metadata_batch* initial_metadata);

  // Returns true if \a domain_pattern is a valid domain pattern, false
  // otherwise.
  static bool IsValidDomainPattern(absl::string_view domain_pattern);

  // Returns the metadata value(s) for the specified key.
  // As special cases, binary headers return a value of absl::nullopt, and
  // "content-type" header returns "application/grpc".
  static absl::optional<absl::string_view> GetHeaderValue(
      grpc_metadata_batch* initial_metadata, absl::string_view header_name,
      std::string* concatenated_value);

  struct GeneratePerHttpFilterConfigsResult {
    std::map<std::string, std::vector<std::string>> per_filter_configs;
    grpc_error_handle error;
    grpc_channel_args* args; // Guaranteed to be nullptr if error is GRPC_ERROR_NONE
  };
  // Generates a map of per_filter_configs that goes from the field name to the list of elements for that field
  static GeneratePerHttpFilterConfigsResult GeneratePerHTTPFilterConfigs(const std::vector<XdsApi::LdsUpdate::HttpConnectionManager::HttpFilter> &filters, grpc_channel_args* args, const XdsApi::RdsUpdate::VirtualHost& vhost, const XdsApi::Route& route,
    const XdsApi::Route::RouteAction::ClusterWeight* cluster_weight);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_ROUTING_H
