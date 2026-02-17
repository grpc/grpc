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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_ROUTING_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_ROUTING_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_route_config.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class XdsRouting final {
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
    virtual const XdsRouteConfigResource::Route::Matchers& GetMatchersForRoute(
        size_t index) const = 0;
  };

  // Returns the index of the selected virtual host in the list.
  static std::optional<size_t> FindVirtualHostForDomain(
      const VirtualHostListIterator& vhost_iterator, absl::string_view domain);

  // Returns the index in route_list_iterator to use for a request with
  // the specified path and metadata, or nullopt if no route matches.
  static std::optional<size_t> GetRouteForRequest(
      const RouteListIterator& route_list_iterator, absl::string_view path,
      grpc_metadata_batch* initial_metadata);

  // Returns true if \a domain_pattern is a valid domain pattern, false
  // otherwise.
  static bool IsValidDomainPattern(absl::string_view domain_pattern);

  // Returns the metadata value(s) for the specified key.
  // As special cases, binary headers return a value of std::nullopt, and
  // "content-type" header returns "application/grpc".
  static std::optional<absl::string_view> GetHeaderValue(
      grpc_metadata_batch* initial_metadata, absl::string_view header_name,
      std::string* concatenated_value);

  // Logic for building a filter chain for a given route.  Caching is
  // done to avoid unnecessary work while iterating over the list of
  // routes in a given VirtualHost.
  //
  // TODO(roth): Currently, this class uses the xds_resolver tracer for
  // logging.  When we change the server side to use the new filter
  // config structure, add a new tracer and use that instead, so that it
  // can be used on both the client and server side.
  class PerRouteFilterChainBuilder {
   public:
    // The add_last_filter() callback is called on the builder after
    // adding all of the xDS HTTP filters and right before building the
    // filter chain.  May be null if not needed.
    PerRouteFilterChainBuilder(
        const std::vector<
            XdsListenerResource::HttpConnectionManager::HttpFilter>&
            hcm_filter_configs,
        const XdsHttpFilterRegistry& http_filter_registry,
        const XdsRouteConfigResource::VirtualHost& vhost,
        FilterChainBuilder& builder,
        absl::AnyInvocable<void(FilterChainBuilder&)> add_last_filter,
        const Blackboard* old_blackboard, Blackboard* new_blackboard);

    // Builds a filter chain for a route that has an individual cluster
    // or a ClusterSpecifierPlugin.
    absl::StatusOr<RefCountedPtr<const FilterChain>> BuildFilterChainForRoute(
        const XdsRouteConfigResource::Route& route);

    // Builds a filter chain for a route that uses WeightedClusters.
    // The set_filter_chain_for_cluster_weight() function will be called
    // once for each index in the WeightedClusters list.
    void BuildFilterChainForRouteWithWeightedClusters(
        const XdsRouteConfigResource::Route& route,
        absl::FunctionRef<
            void(size_t, absl::StatusOr<RefCountedPtr<const FilterChain>>)>
            set_filter_chain_for_cluster_weight);

   private:
    absl::StatusOr<RefCountedPtr<const FilterChain>> GetDefaultFilterChain();

    const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
        hcm_filter_configs_;
    const XdsRouteConfigResource::VirtualHost& vhost_;
    FilterChainBuilder& builder_;
    absl::AnyInvocable<void(FilterChainBuilder&)> add_last_filter_;
    const Blackboard* old_blackboard_;
    Blackboard* new_blackboard_;

    // Same size as hcm_filter_configs_.
    std::vector<const XdsHttpFilterImpl*> filter_impls_;

    // Cached default filter chain, to be used for any route that does
    // not have any filter config overrides.
    absl::StatusOr<RefCountedPtr<const FilterChain>> default_filter_chain_ =
        nullptr;
  };

  struct GeneratePerHttpFilterConfigsResult {
    // Map of service config field name to list of elements for that field.
    std::map<std::string, std::vector<std::string>> per_filter_configs;
    ChannelArgs args;
  };

  // Generates per-HTTP filter configs for a method config.
  static absl::StatusOr<GeneratePerHttpFilterConfigsResult>
  GeneratePerHTTPFilterConfigsForMethodConfig(
      const XdsHttpFilterRegistry& http_filter_registry,
      const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
          http_filters,
      const XdsRouteConfigResource::VirtualHost& vhost,
      const XdsRouteConfigResource::Route& route,
      const XdsRouteConfigResource::Route::RouteAction::ClusterWeight*
          cluster_weight,
      const ChannelArgs& args);

  // Generates per-HTTP filter configs for the top-level service config.
  static absl::StatusOr<GeneratePerHttpFilterConfigsResult>
  GeneratePerHTTPFilterConfigsForServiceConfig(
      const XdsHttpFilterRegistry& http_filter_registry,
      const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
          http_filters,
      const ChannelArgs& args);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_ROUTING_H
