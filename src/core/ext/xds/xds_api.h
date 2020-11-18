/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_XDS_XDS_API_H
#define GRPC_CORE_EXT_XDS_XDS_API_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <set>

#include "absl/container/inlined_vector.h"
#include "absl/types/optional.h"
#include "re2/re2.h"

#include "upb/def.hpp"

#include <grpc/slice_buffer.h>

#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client_stats.h"

namespace grpc_core {

// TODO(yashykt): Check to see if xDS security is enabled. This will be
// removed once this feature is fully integration-tested and enabled by
// default.
bool XdsSecurityEnabled();

class XdsClient;

class XdsApi {
 public:
  static const char* kLdsTypeUrl;
  static const char* kRdsTypeUrl;
  static const char* kCdsTypeUrl;
  static const char* kEdsTypeUrl;

  struct Duration {
    int64_t seconds = 0;
    int32_t nanos = 0;
    bool operator==(const Duration& other) const {
      return (seconds == other.seconds && nanos == other.nanos);
    }
    std::string ToString() const {
      return absl::StrFormat("Duration seconds: %ld, nanos %d", seconds, nanos);
    }
  };

  // TODO(donnadionne): When we can use absl::variant<>, consider using that
  // for: PathMatcher, HeaderMatcher, cluster_name and weighted_clusters
  struct Route {
    // Matchers for this route.
    struct Matchers {
      struct PathMatcher {
        enum class PathMatcherType {
          PATH,    // path stored in string_matcher field
          PREFIX,  // prefix stored in string_matcher field
          REGEX,   // regex stored in regex_matcher field
        };
        PathMatcherType type;
        std::string string_matcher;
        std::unique_ptr<RE2> regex_matcher;
        bool case_sensitive = true;

        PathMatcher() = default;
        PathMatcher(const PathMatcher& other);
        PathMatcher& operator=(const PathMatcher& other);
        bool operator==(const PathMatcher& other) const;
        std::string ToString() const;
      };

      struct HeaderMatcher {
        enum class HeaderMatcherType {
          EXACT,    // value stored in string_matcher field
          REGEX,    // uses regex_match field
          RANGE,    // uses range_start and range_end fields
          PRESENT,  // uses present_match field
          PREFIX,   // prefix stored in string_matcher field
          SUFFIX,   // suffix stored in string_matcher field
        };
        std::string name;
        HeaderMatcherType type;
        int64_t range_start;
        int64_t range_end;
        std::string string_matcher;
        std::unique_ptr<RE2> regex_match;
        bool present_match;
        // invert_match field may or may not exisit, so initialize it to
        // false.
        bool invert_match = false;

        HeaderMatcher() = default;
        HeaderMatcher(const HeaderMatcher& other);
        HeaderMatcher& operator=(const HeaderMatcher& other);
        bool operator==(const HeaderMatcher& other) const;
        std::string ToString() const;
      };

      PathMatcher path_matcher;
      std::vector<HeaderMatcher> header_matchers;
      absl::optional<uint32_t> fraction_per_million;

      bool operator==(const Matchers& other) const {
        return (path_matcher == other.path_matcher &&
                header_matchers == other.header_matchers &&
                fraction_per_million == other.fraction_per_million);
      }
      std::string ToString() const;
    };

    Matchers matchers;

    // Action for this route.
    // TODO(roth): When we can use absl::variant<>, consider using that
    // here, to enforce the fact that only one of the two fields can be set.
    std::string cluster_name;
    struct ClusterWeight {
      std::string name;
      uint32_t weight;
      bool operator==(const ClusterWeight& other) const {
        return (name == other.name && weight == other.weight);
      }
      std::string ToString() const;
    };
    std::vector<ClusterWeight> weighted_clusters;
    // Storing the timeout duration from route action:
    // RouteAction.max_stream_duration.grpc_timeout_header_max or
    // RouteAction.max_stream_duration.max_stream_duration if the former is
    // not set.
    absl::optional<Duration> max_stream_duration;

    bool operator==(const Route& other) const {
      return (matchers == other.matchers &&
              cluster_name == other.cluster_name &&
              weighted_clusters == other.weighted_clusters &&
              max_stream_duration == other.max_stream_duration);
    }
    std::string ToString() const;
  };

  struct RdsUpdate {
    struct VirtualHost {
      std::vector<std::string> domains;
      std::vector<Route> routes;

      bool operator==(const VirtualHost& other) const {
        return domains == other.domains && routes == other.routes;
      }
    };

    std::vector<VirtualHost> virtual_hosts;

    bool operator==(const RdsUpdate& other) const {
      return virtual_hosts == other.virtual_hosts;
    }
    std::string ToString() const;
    VirtualHost* FindVirtualHostForDomain(const std::string& domain);
  };

  struct StringMatcher {
    enum class StringMatcherType {
      EXACT,       // value stored in string_matcher_field
      PREFIX,      // value stored in string_matcher_field
      SUFFIX,      // value stored in string_matcher_field
      SAFE_REGEX,  // use regex_match field
      CONTAINS,    // value stored in string_matcher_field
    };
    StringMatcherType type;
    std::string string_matcher;
    std::unique_ptr<RE2> regex_match;
    bool ignore_case;

    StringMatcher() = default;
    StringMatcher(const StringMatcher& other);
    StringMatcher& operator=(const StringMatcher& other);
    bool operator==(const StringMatcher& other) const;
  };

  struct CommonTlsContext {
    struct CertificateValidationContext {
      std::vector<StringMatcher> match_subject_alt_names;

      bool operator==(const CertificateValidationContext& other) const {
        return match_subject_alt_names == other.match_subject_alt_names;
      }
    };

    struct CertificateProviderInstance {
      std::string instance_name;
      std::string certificate_name;

      bool operator==(const CertificateProviderInstance& other) const {
        return instance_name == other.instance_name &&
               certificate_name == other.certificate_name;
      }
    };

    struct CombinedCertificateValidationContext {
      CertificateValidationContext default_validation_context;
      CertificateProviderInstance
          validation_context_certificate_provider_instance;

      bool operator==(const CombinedCertificateValidationContext& other) const {
        return default_validation_context == other.default_validation_context &&
               validation_context_certificate_provider_instance ==
                   other.validation_context_certificate_provider_instance;
      }
    };

    CertificateProviderInstance tls_certificate_certificate_provider_instance;
    CombinedCertificateValidationContext combined_validation_context;

    bool operator==(const CommonTlsContext& other) const {
      return tls_certificate_certificate_provider_instance ==
                 other.tls_certificate_certificate_provider_instance &&
             combined_validation_context == other.combined_validation_context;
    }
  };

  // TODO(roth): When we can use absl::variant<>, consider using that
  // here, to enforce the fact that only one of the two fields can be set.
  struct LdsUpdate {
    // The name to use in the RDS request.
    std::string route_config_name;
    // Storing the Http Connection Manager Common Http Protocol Option
    // max_stream_duration
    Duration http_max_stream_duration;
    // The RouteConfiguration to use for this listener.
    // Present only if it is inlined in the LDS response.
    absl::optional<RdsUpdate> rds_update;

    bool operator==(const LdsUpdate& other) const {
      return route_config_name == other.route_config_name &&
             rds_update == other.rds_update &&
             http_max_stream_duration == other.http_max_stream_duration;
    }
  };

  using LdsUpdateMap = std::map<std::string /*server_name*/, LdsUpdate>;

  using RdsUpdateMap = std::map<std::string /*route_config_name*/, RdsUpdate>;

  struct CdsUpdate {
    // The name to use in the EDS request.
    // If empty, the cluster name will be used.
    std::string eds_service_name;
    // Tls Context used by clients
    CommonTlsContext common_tls_context;
    // The LRS server to use for load reporting.
    // If not set, load reporting will be disabled.
    // If set to the empty string, will use the same server we obtained the CDS
    // data from.
    absl::optional<std::string> lrs_load_reporting_server_name;
    // Maximum number of outstanding requests can be made to the upstream
    // cluster.
    uint32_t max_concurrent_requests = 1024;

    bool operator==(const CdsUpdate& other) const {
      return eds_service_name == other.eds_service_name &&
             common_tls_context == other.common_tls_context &&
             lrs_load_reporting_server_name ==
                 other.lrs_load_reporting_server_name &&
             max_concurrent_requests == other.max_concurrent_requests;
    }
  };

  using CdsUpdateMap = std::map<std::string /*cluster_name*/, CdsUpdate>;

  struct EdsUpdate {
    struct Priority {
      struct Locality {
        RefCountedPtr<XdsLocalityName> name;
        uint32_t lb_weight;
        ServerAddressList endpoints;

        bool operator==(const Locality& other) const {
          return *name == *other.name && lb_weight == other.lb_weight &&
                 endpoints == other.endpoints;
        }
        bool operator!=(const Locality& other) const {
          return !(*this == other);
        }
        std::string ToString() const;
      };

      std::map<XdsLocalityName*, Locality, XdsLocalityName::Less> localities;

      bool operator==(const Priority& other) const;
      std::string ToString() const;
    };
    using PriorityList = absl::InlinedVector<Priority, 2>;

    // There are two phases of accessing this class's content:
    // 1. to initialize in the control plane combiner;
    // 2. to use in the data plane combiner.
    // So no additional synchronization is needed.
    class DropConfig : public RefCounted<DropConfig> {
     public:
      struct DropCategory {
        bool operator==(const DropCategory& other) const {
          return name == other.name &&
                 parts_per_million == other.parts_per_million;
        }

        std::string name;
        const uint32_t parts_per_million;
      };

      using DropCategoryList = absl::InlinedVector<DropCategory, 2>;

      void AddCategory(std::string name, uint32_t parts_per_million) {
        drop_category_list_.emplace_back(
            DropCategory{std::move(name), parts_per_million});
        if (parts_per_million == 1000000) drop_all_ = true;
      }

      // The only method invoked from outside the WorkSerializer (used in
      // the data plane).
      bool ShouldDrop(const std::string** category_name) const;

      const DropCategoryList& drop_category_list() const {
        return drop_category_list_;
      }

      bool drop_all() const { return drop_all_; }

      bool operator==(const DropConfig& other) const {
        return drop_category_list_ == other.drop_category_list_;
      }
      bool operator!=(const DropConfig& other) const {
        return !(*this == other);
      }

      std::string ToString() const;

     private:
      DropCategoryList drop_category_list_;
      bool drop_all_ = false;
    };

    PriorityList priorities;
    RefCountedPtr<DropConfig> drop_config;

    bool operator==(const EdsUpdate& other) const {
      return priorities == other.priorities &&
             *drop_config == *other.drop_config;
    }
    std::string ToString() const;
  };

  using EdsUpdateMap = std::map<std::string /*eds_service_name*/, EdsUpdate>;

  struct ClusterLoadReport {
    XdsClusterDropStats::Snapshot dropped_requests;
    std::map<RefCountedPtr<XdsLocalityName>, XdsClusterLocalityStats::Snapshot,
             XdsLocalityName::Less>
        locality_stats;
    grpc_millis load_report_interval;
  };
  using ClusterLoadReportMap = std::map<
      std::pair<std::string /*cluster_name*/, std::string /*eds_service_name*/>,
      ClusterLoadReport>;

  XdsApi(XdsClient* client, TraceFlag* tracer, const XdsBootstrap::Node* node);

  // Creates an ADS request.
  // Takes ownership of \a error.
  grpc_slice CreateAdsRequest(const XdsBootstrap::XdsServer& server,
                              const std::string& type_url,
                              const std::set<absl::string_view>& resource_names,
                              const std::string& version,
                              const std::string& nonce, grpc_error* error,
                              bool populate_node);

  // Parses an ADS response.
  // If the response can't be parsed at the top level, the resulting
  // type_url will be empty.
  struct AdsParseResult {
    grpc_error* parse_error = GRPC_ERROR_NONE;
    std::string version;
    std::string nonce;
    std::string type_url;
    LdsUpdateMap lds_update_map;
    RdsUpdateMap rds_update_map;
    CdsUpdateMap cds_update_map;
    EdsUpdateMap eds_update_map;
  };
  AdsParseResult ParseAdsResponse(
      const grpc_slice& encoded_response,
      const std::set<absl::string_view>& expected_listener_names,
      const std::set<absl::string_view>& expected_route_configuration_names,
      const std::set<absl::string_view>& expected_cluster_names,
      const std::set<absl::string_view>& expected_eds_service_names);

  // Creates an initial LRS request.
  grpc_slice CreateLrsInitialRequest(const XdsBootstrap::XdsServer& server);

  // Creates an LRS request sending a client-side load report.
  grpc_slice CreateLrsRequest(ClusterLoadReportMap cluster_load_report_map);

  // Parses the LRS response and returns \a
  // load_reporting_interval for client-side load reporting. If there is any
  // error, the output config is invalid.
  grpc_error* ParseLrsResponse(const grpc_slice& encoded_response,
                               bool* send_all_clusters,
                               std::set<std::string>* cluster_names,
                               grpc_millis* load_reporting_interval);

 private:
  XdsClient* client_;
  TraceFlag* tracer_;
  const XdsBootstrap::Node* node_;  // Do not own.
  upb::SymbolTable symtab_;
  const std::string build_version_;
  const std::string user_agent_name_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_XDS_XDS_API_H */
