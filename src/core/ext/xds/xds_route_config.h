//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_ROUTE_CONFIG_H
#define GRPC_CORE_EXT_XDS_XDS_ROUTE_CONFIG_H

#include <grpc/support/port_platform.h>

#include <map>
#include <string>
#include <vector>

#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "envoy/config/route/v3/route.upb.h"
#include "envoy/config/route/v3/route.upbdefs.h"
#include "re2/re2.h"

#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/ext/xds/xds_resource_type_impl.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

bool XdsRbacEnabled();

struct XdsRouteConfigResource {
  using TypedPerFilterConfig =
      std::map<std::string, XdsHttpFilterImpl::FilterConfig>;

  struct RetryPolicy {
    internal::StatusCodeSet retry_on;
    uint32_t num_retries;

    struct RetryBackOff {
      Duration base_interval;
      Duration max_interval;

      bool operator==(const RetryBackOff& other) const {
        return base_interval == other.base_interval &&
               max_interval == other.max_interval;
      }
      std::string ToString() const;
    };
    RetryBackOff retry_back_off;

    bool operator==(const RetryPolicy& other) const {
      return (retry_on == other.retry_on && num_retries == other.num_retries &&
              retry_back_off == other.retry_back_off);
    }
    std::string ToString() const;
  };

  // TODO(donnadionne): When we can use absl::variant<>, consider using that
  // for: PathMatcher, HeaderMatcher, cluster_name and weighted_clusters
  struct Route {
    // Matchers for this route.
    struct Matchers {
      StringMatcher path_matcher;
      std::vector<HeaderMatcher> header_matchers;
      absl::optional<uint32_t> fraction_per_million;

      bool operator==(const Matchers& other) const {
        return path_matcher == other.path_matcher &&
               header_matchers == other.header_matchers &&
               fraction_per_million == other.fraction_per_million;
      }
      std::string ToString() const;
    };

    Matchers matchers;

    struct UnknownAction {
      bool operator==(const UnknownAction& /* other */) const { return true; }
    };

    struct RouteAction {
      struct HashPolicy {
        enum Type { HEADER, CHANNEL_ID };
        Type type;
        bool terminal = false;
        // Fields used for type HEADER.
        std::string header_name;
        std::unique_ptr<RE2> regex = nullptr;
        std::string regex_substitution;

        HashPolicy() {}

        // Copyable.
        HashPolicy(const HashPolicy& other);
        HashPolicy& operator=(const HashPolicy& other);

        // Moveable.
        HashPolicy(HashPolicy&& other) noexcept;
        HashPolicy& operator=(HashPolicy&& other) noexcept;

        bool operator==(const HashPolicy& other) const;
        std::string ToString() const;
      };

      struct ClusterWeight {
        std::string name;
        uint32_t weight;
        TypedPerFilterConfig typed_per_filter_config;

        bool operator==(const ClusterWeight& other) const {
          return name == other.name && weight == other.weight &&
                 typed_per_filter_config == other.typed_per_filter_config;
        }
        std::string ToString() const;
      };

      std::vector<HashPolicy> hash_policies;
      absl::optional<RetryPolicy> retry_policy;

      // Action for this route.
      // TODO(roth): When we can use absl::variant<>, consider using that
      // here, to enforce the fact that only one of the two fields can be set.
      std::string cluster_name;
      std::vector<ClusterWeight> weighted_clusters;
      // Storing the timeout duration from route action:
      // RouteAction.max_stream_duration.grpc_timeout_header_max or
      // RouteAction.max_stream_duration.max_stream_duration if the former is
      // not set.
      absl::optional<Duration> max_stream_duration;

      bool operator==(const RouteAction& other) const {
        return hash_policies == other.hash_policies &&
               retry_policy == other.retry_policy &&
               cluster_name == other.cluster_name &&
               weighted_clusters == other.weighted_clusters &&
               max_stream_duration == other.max_stream_duration;
      }
      std::string ToString() const;
    };

    struct NonForwardingAction {
      bool operator==(const NonForwardingAction& /* other */) const {
        return true;
      }
    };

    absl::variant<UnknownAction, RouteAction, NonForwardingAction> action;
    TypedPerFilterConfig typed_per_filter_config;

    bool operator==(const Route& other) const {
      return matchers == other.matchers && action == other.action &&
             typed_per_filter_config == other.typed_per_filter_config;
    }
    std::string ToString() const;
  };

  struct VirtualHost {
    std::vector<std::string> domains;
    std::vector<Route> routes;
    TypedPerFilterConfig typed_per_filter_config;

    bool operator==(const VirtualHost& other) const {
      return domains == other.domains && routes == other.routes &&
             typed_per_filter_config == other.typed_per_filter_config;
    }
  };

  std::vector<VirtualHost> virtual_hosts;

  bool operator==(const XdsRouteConfigResource& other) const {
    return virtual_hosts == other.virtual_hosts;
  }
  std::string ToString() const;

  static grpc_error_handle Parse(
      const XdsEncodingContext& context,
      const envoy_config_route_v3_RouteConfiguration* route_config,
      XdsRouteConfigResource* rds_update);
};

class XdsRouteConfigResourceType
    : public XdsResourceTypeImpl<XdsRouteConfigResourceType,
                                 XdsRouteConfigResource> {
 public:
  absl::string_view type_url() const override {
    return "envoy.config.route.v3.RouteConfiguration";
  }
  absl::string_view v2_type_url() const override {
    return "envoy.api.v2.RouteConfiguration";
  }

  absl::StatusOr<DecodeResult> Decode(const XdsEncodingContext& context,
                                      absl::string_view serialized_resource,
                                      bool /*is_v2*/) const override;

  void InitUpbSymtab(upb_symtab* symtab) const override {
    envoy_config_route_v3_RouteConfiguration_getmsgdef(symtab);
  }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_ROUTE_CONFIG_H
