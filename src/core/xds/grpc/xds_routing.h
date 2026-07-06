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

  // Logic for building filter chains for each route within a
  // RouteConfiguration.  Caching is done to avoid unnecessary work while
  // iterating over the list of routes.
  //
  // TODO(roth): Currently, this class uses the xds_resolver tracer for
  // logging.  When we change the server side to use the new filter
  // config structure, add a new tracer and use that instead, so that it
  // can be used on both the client and server side.
  class RouteConfigFilterChainBuilder {
   public:
    // Builds filter chains for each route within a VirtualHost.
    class VirtualHostFilterChainBuilder {
     public:
      // Builds filter chains for each ClusterWeight within a route.
      class WeightedClusterRouteFilterChainBuilder {
       public:
        WeightedClusterRouteFilterChainBuilder(
            VirtualHostFilterChainBuilder& vhost_builder,
            const XdsRouteConfigResource::Route& route)
            : vhost_builder_(vhost_builder), route_(route) {}

        // Builds a filter chain for a ClusterWeight.
        absl::StatusOr<RefCountedPtr<const FilterChain>>
        BuildFilterChainForClusterWeight(
            const XdsRouteConfigResource::Route::RouteAction::ClusterWeight&
                cluster_weight);

       private:
        absl::StatusOr<RefCountedPtr<const FilterChain>> GetRouteFilterChain();

        VirtualHostFilterChainBuilder& vhost_builder_;
        const XdsRouteConfigResource::Route& route_;

        // Cached filter chain for the route, to be used for any ClusterWeight
        // that does not have any filter config overrides.
        absl::StatusOr<RefCountedPtr<const FilterChain>> route_filter_chain_ =
            nullptr;
      };

      VirtualHostFilterChainBuilder(
          RouteConfigFilterChainBuilder& route_config_builder,
          const XdsRouteConfigResource::VirtualHost& vhost)
          : route_config_builder_(route_config_builder), vhost_(vhost) {}

      // Builds a filter chain for a route that has an individual cluster
      // or a ClusterSpecifierPlugin.
      absl::StatusOr<RefCountedPtr<const FilterChain>> BuildFilterChainForRoute(
          const XdsRouteConfigResource::Route& route);

      // Returns a filter chain builder for a given route that uses
      // WeightedClusters.
      WeightedClusterRouteFilterChainBuilder
      MakeWeightedClusterRouteFilterChainBuilder(
          const XdsRouteConfigResource::Route& route) {
        return WeightedClusterRouteFilterChainBuilder(*this, route);
      }

     private:
      absl::StatusOr<RefCountedPtr<const FilterChain>>
      GetVirtualHostFilterChain();

      RouteConfigFilterChainBuilder& route_config_builder_;
      const XdsRouteConfigResource::VirtualHost& vhost_;

      // Cached filter chain for the virtual host, to be used for any route
      // that does not have any filter config overrides.
      absl::StatusOr<RefCountedPtr<const FilterChain>> vhost_filter_chain_ =
          nullptr;
    };

    // The add_last_filter() callback is called on the builder after
    // adding all of the xDS HTTP filters and right before building the
    // filter chain.  May be null if not needed.
    RouteConfigFilterChainBuilder(
        const std::vector<
            XdsListenerResource::HttpConnectionManager::HttpFilter>&
            hcm_filter_configs,
        const XdsHttpFilterRegistry& http_filter_registry,
        FilterChainBuilder& builder,
        absl::AnyInvocable<void(FilterChainBuilder&)> add_last_filter,
        Blackboard& blackboard);

    // Returns a filter chain builder for a given virtual host.
    VirtualHostFilterChainBuilder MakeVirtualHostFilterChainBuilder(
        const XdsRouteConfigResource::VirtualHost& vhost) {
      return VirtualHostFilterChainBuilder(*this, vhost);
    }

   private:
    absl::StatusOr<RefCountedPtr<const FilterChain>> GetDefaultFilterChain();

    const std::vector<XdsListenerResource::HttpConnectionManager::HttpFilter>&
        hcm_filter_configs_;
    FilterChainBuilder& builder_;
    absl::AnyInvocable<void(FilterChainBuilder&)> add_last_filter_;
    Blackboard& blackboard_;

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
