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

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/util/gcp_metadata_query.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"

namespace grpc_core {

namespace {

const char* kC2PAuthority = "traffic-director-c2p.xds.googleapis.com";

class GoogleCloud2ProdResolver final : public Resolver {
 public:
  explicit GoogleCloud2ProdResolver(ResolverArgs args);

  void StartLocked() override;
  void RequestReresolutionLocked() override;
  void ResetBackoffLocked() override;
  void ShutdownLocked() override;

 private:
  void ZoneQueryDone(std::string zone);
  void IPv6QueryDone(bool ipv6_supported);
  void StartXdsResolver();

  ResourceQuotaRefPtr resource_quota_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_polling_entity pollent_;
  bool using_dns_ = false;
  OrphanablePtr<Resolver> child_resolver_;
  std::string metadata_server_name_ = "metadata.google.internal.";
  bool shutdown_ = false;

  OrphanablePtr<GcpMetadataQuery> zone_query_;
  absl::optional<std::string> zone_;

  OrphanablePtr<GcpMetadataQuery> ipv6_query_;
  absl::optional<bool> supports_ipv6_;
};

//
// GoogleCloud2ProdResolver
//

bool XdsBootstrapConfigured() {
  return GetEnv("GRPC_XDS_BOOTSTRAP").has_value() ||
         GetEnv("GRPC_XDS_BOOTSTRAP_CONFIG").has_value();
}

GoogleCloud2ProdResolver::GoogleCloud2ProdResolver(ResolverArgs args)
    : resource_quota_(args.args.GetObjectRef<ResourceQuota>()),
      work_serializer_(std::move(args.work_serializer)),
      pollent_(grpc_polling_entity_create_from_pollset_set(args.pollset_set)) {
  absl::string_view name_to_resolve = absl::StripPrefix(args.uri.path(), "/");
  // If we're not running on GCP, we can't use DirectPath, so delegate
  // to the DNS resolver.
  const bool test_only_pretend_running_on_gcp =
      args.args
          .GetBool("grpc.testing.google_c2p_resolver_pretend_running_on_gcp")
          .value_or(false);
  const bool running_on_gcp =
      test_only_pretend_running_on_gcp || grpc_alts_is_running_on_gcp();
  const bool federation_enabled = XdsFederationEnabled();
  if (!running_on_gcp ||
      // If the client is already using xDS and federation is not enabled,
      // we can't use it here, because they may be talking to a completely
      // different xDS server than we want to.
      // TODO(roth): When we remove xDS federation env var protection,
      // remove this constraint.
      (!federation_enabled && XdsBootstrapConfigured())) {
    using_dns_ = true;
    child_resolver_ =
        CoreConfiguration::Get().resolver_registry().CreateResolver(
            absl::StrCat("dns:", name_to_resolve), args.args, args.pollset_set,
            work_serializer_, std::move(args.result_handler));
    CHECK(child_resolver_ != nullptr);
    return;
  }
  // Maybe override metadata server name for testing
  absl::optional<std::string> test_only_metadata_server_override =
      args.args.GetOwnedString(
          "grpc.testing.google_c2p_resolver_metadata_server_override");
  if (test_only_metadata_server_override.has_value() &&
      !test_only_metadata_server_override->empty()) {
    metadata_server_name_ = std::move(*test_only_metadata_server_override);
  }
  // Create xds resolver.
  std::string xds_uri =
      federation_enabled
          ? absl::StrCat("xds://", kC2PAuthority, "/", name_to_resolve)
          : absl::StrCat("xds:", name_to_resolve);
  child_resolver_ = CoreConfiguration::Get().resolver_registry().CreateResolver(
      xds_uri, args.args, args.pollset_set, work_serializer_,
      std::move(args.result_handler));
  CHECK(child_resolver_ != nullptr);
}

void GoogleCloud2ProdResolver::StartLocked() {
  if (using_dns_) {
    child_resolver_->StartLocked();
    return;
  }
  // Using xDS.  Start metadata server queries.
  zone_query_ = MakeOrphanable<GcpMetadataQuery>(
      metadata_server_name_, std::string(GcpMetadataQuery::kZoneAttribute),
      &pollent_,
      [resolver = RefAsSubclass<GoogleCloud2ProdResolver>()](
          std::string /* attribute */,
          absl::StatusOr<std::string> result) mutable {
        resolver->work_serializer_->Run(
            [resolver, result = std::move(result)]() mutable {
              resolver->ZoneQueryDone(result.ok() ? std::move(result).value()
                                                  : "");
            },
            DEBUG_LOCATION);
      },
      Duration::Seconds(10));
  ipv6_query_ = MakeOrphanable<GcpMetadataQuery>(
      metadata_server_name_, std::string(GcpMetadataQuery::kIPv6Attribute),
      &pollent_,
      [resolver = RefAsSubclass<GoogleCloud2ProdResolver>()](
          std::string /* attribute */,
          absl::StatusOr<std::string> result) mutable {
        resolver->work_serializer_->Run(
            [resolver, result = std::move(result)]() {
              // Check that the payload is non-empty in order to work around
              // the fact that there are buggy implementations of metadata
              // servers in the wild, which can in some cases return 200
              // plus an empty result when they should have returned 404.
              resolver->IPv6QueryDone(result.ok() && !result->empty());
            },
            DEBUG_LOCATION);
      },
      Duration::Seconds(10));
}

void GoogleCloud2ProdResolver::RequestReresolutionLocked() {
  if (child_resolver_ != nullptr) {
    child_resolver_->RequestReresolutionLocked();
  }
}

void GoogleCloud2ProdResolver::ResetBackoffLocked() {
  if (child_resolver_ != nullptr) {
    child_resolver_->ResetBackoffLocked();
  }
}

void GoogleCloud2ProdResolver::ShutdownLocked() {
  shutdown_ = true;
  zone_query_.reset();
  ipv6_query_.reset();
  child_resolver_.reset();
}

void GoogleCloud2ProdResolver::ZoneQueryDone(std::string zone) {
  zone_query_.reset();
  zone_ = std::move(zone);
  if (supports_ipv6_.has_value()) StartXdsResolver();
}

void GoogleCloud2ProdResolver::IPv6QueryDone(bool ipv6_supported) {
  ipv6_query_.reset();
  supports_ipv6_ = ipv6_supported;
  if (zone_.has_value()) StartXdsResolver();
}

void GoogleCloud2ProdResolver::StartXdsResolver() {
  if (shutdown_) {
    return;
  }
  // Construct bootstrap JSON.
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<uint64_t> dist(1, UINT64_MAX);
  Json::Object node = {
      {"id", Json::FromString(absl::StrCat("C2P-", dist(mt)))},
  };
  if (!zone_->empty()) {
    node["locality"] = Json::FromObject({
        {"zone", Json::FromString(*zone_)},
    });
  };
  if (*supports_ipv6_) {
    node["metadata"] = Json::FromObject({
        {"TRAFFICDIRECTOR_DIRECTPATH_C2P_IPV6_CAPABLE", Json::FromBool(true)},
    });
  }
  // Allow the TD server uri to be overridden for testing purposes.
  auto override_server =
      GetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI");
  const char* server_uri =
      override_server.has_value() && !override_server->empty()
          ? override_server->c_str()
          : "directpath-pa.googleapis.com";
  Json xds_server = Json::FromArray({
      Json::FromObject({
          {"server_uri", Json::FromString(server_uri)},
          {"channel_creds",
           Json::FromArray({
               Json::FromObject({
                   {"type", Json::FromString("google_default")},
               }),
           })},
          {"server_features",
           Json::FromArray({Json::FromString("ignore_resource_deletion")})},
      }),
  });
  Json bootstrap = Json::FromObject({
      {"xds_servers", xds_server},
      {"authorities",
       Json::FromObject({
           {kC2PAuthority, Json::FromObject({
                               {"xds_servers", std::move(xds_server)},
                           })},
       })},
      {"node", Json::FromObject(std::move(node))},
  });
  // Inject bootstrap JSON as fallback config.
  internal::SetXdsFallbackBootstrapConfig(JsonDump(bootstrap).c_str());
  // Now start xDS resolver.
  child_resolver_->StartLocked();
}

//
// Factory
//

class GoogleCloud2ProdResolverFactory final : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "google-c2p"; }

  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
      LOG(ERROR) << "google-c2p URI scheme does not support authorities";
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    return MakeOrphanable<GoogleCloud2ProdResolver>(std::move(args));
  }
};

// TODO(apolcyn): remove this class after user code has updated to the
// stable "google-c2p" URI scheme.
class ExperimentalGoogleCloud2ProdResolverFactory final
    : public ResolverFactory {
 public:
  absl::string_view scheme() const override {
    return "google-c2p-experimental";
  }

  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
      LOG(ERROR) << "google-c2p-experimental URI scheme does not support "
                    "authorities";
      return false;
    }
    return true;
  }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    if (!IsValidUri(args.uri)) return nullptr;
    return MakeOrphanable<GoogleCloud2ProdResolver>(std::move(args));
  }
};

}  // namespace

void RegisterCloud2ProdResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<GoogleCloud2ProdResolverFactory>());
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<ExperimentalGoogleCloud2ProdResolverFactory>());
}

}  // namespace grpc_core
