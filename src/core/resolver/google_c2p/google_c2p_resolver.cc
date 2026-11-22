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

#include "src/core/resolver/google_c2p/google_c2p_resolver.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/json.h>
#include <grpc/support/string_util.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <utility>

#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/alts/check_gcp_environment.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/env.h"
#include "src/core/util/gcp_metadata_query.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/no_destruct.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc_builder.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace grpc_core {

namespace {

const char* kC2PAuthority = "traffic-director-c2p.xds.googleapis.com";
constexpr absl::string_view kDefaultUniverseDomain = "googleapis.com";

struct GoogleC2PConfig {
  Mutex mu;
  std::optional<std::string> universe_domain ABSL_GUARDED_BY(mu);
  std::shared_ptr<GrpcXdsBootstrap> bootstrap ABSL_GUARDED_BY(mu);
};

GoogleC2PConfig* GetGoogleC2PConfig() {
  static NoDestruct<GoogleC2PConfig> config;
  return config.get();
}

std::string GetOrSetDefaultUniverseDomain() {
  GoogleC2PConfig* config = GetGoogleC2PConfig();
  MutexLock lock(config->mu);
  if (!config->universe_domain.has_value()) {
    config->universe_domain = std::string(kDefaultUniverseDomain);
  }
  return *config->universe_domain;
}

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
  std::shared_ptr<GrpcXdsBootstrap> ConstructBootstrap() const;

  ResourceQuotaRefPtr resource_quota_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_polling_entity pollent_;
  const bool force_xds_;
  bool using_dns_ = false;
  OrphanablePtr<Resolver> child_resolver_;
  std::string metadata_server_name_ = "metadata.google.internal.";
  bool shutdown_ = false;

  // Used to hold state between ctor and StartLocked().
  std::string xds_uri_;
  std::string universe_domain_;
  ChannelArgs args_;

  OrphanablePtr<GcpMetadataQuery> zone_query_;
  std::string zone_;
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
      pollent_(grpc_polling_entity_create_from_pollset_set(args.pollset_set)),
      force_xds_(args.uri.query_parameter_map().find("force-xds") !=
                 args.uri.query_parameter_map().end()),
      args_(std::move(args.args)) {
  absl::string_view name_to_resolve = absl::StripPrefix(args.uri.path(), "/");
  const bool federation_enabled = XdsFederationEnabled();
  if (!force_xds_) {
    // If we're not running on GCP, we can't use DirectPath, so delegate
    // to the DNS resolver.
    const bool test_only_pretend_running_on_gcp =
        args_.GetBool("grpc.testing.google_c2p_resolver_pretend_running_on_gcp")
            .value_or(false);
    const bool running_on_gcp =
        test_only_pretend_running_on_gcp || grpc_alts_is_running_on_gcp();
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
              absl::StrCat("dns:", name_to_resolve), args_, args.pollset_set,
              work_serializer_, std::move(args.result_handler));
      GRPC_CHECK(child_resolver_ != nullptr);
      return;
    }
    // Maybe override metadata server name for testing
    std::optional<std::string> test_only_metadata_server_override =
        args_.GetOwnedString(
            "grpc.testing.google_c2p_resolver_metadata_server_override");
    if (test_only_metadata_server_override.has_value() &&
        !test_only_metadata_server_override->empty()) {
      metadata_server_name_ = std::move(*test_only_metadata_server_override);
    }
  }
  // Lock in the universe domain (defaulting to "googleapis.com" if not set).
  universe_domain_ = GetOrSetDefaultUniverseDomain();
  // Create xds resolver.
  xds_uri_ = federation_enabled
                 ? absl::StrCat("xds://", kC2PAuthority, "/", name_to_resolve)
                 : absl::StrCat("xds:", name_to_resolve);
  child_resolver_ = CoreConfiguration::Get().resolver_registry().CreateResolver(
      xds_uri_, args_, args.pollset_set, work_serializer_,
      std::move(args.result_handler));
  GRPC_CHECK(child_resolver_ != nullptr);
}

void GoogleCloud2ProdResolver::StartLocked() {
  if (force_xds_) {
    StartXdsResolver();
    return;
  }
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
        resolver->work_serializer_->Run([resolver,
                                         result = std::move(result)]() mutable {
          resolver->ZoneQueryDone(result.ok() ? std::move(result).value() : "");
        });
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
  child_resolver_.reset();
}

void GoogleCloud2ProdResolver::ZoneQueryDone(std::string zone) {
  zone_query_.reset();
  zone_ = std::move(zone);
  StartXdsResolver();
}

std::shared_ptr<GrpcXdsBootstrap> GoogleCloud2ProdResolver::ConstructBootstrap()
    const {
  // Construct bootstrap JSON.
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<uint64_t> dist(1, UINT64_MAX);
  Json::Object node = {
      {"id", Json::FromString(absl::StrCat("C2P-", dist(mt)))},
  };
  if (!zone_.empty()) {
    node["locality"] = Json::FromObject({
        {"zone", Json::FromString(zone_)},
    });
  };
  // Enable dualstack endpoints from TD
  node["metadata"] = Json::FromObject({
      {"TRAFFICDIRECTOR_DIRECTPATH_C2P_IPV6_CAPABLE", Json::FromBool(true)},
  });
  // Allow the TD server uri to be overridden for testing purposes.
  // Evaluated after default universe domain locking so tests still exercise
  // the default value logic.
  auto override_server =
      GetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI");
  std::string server_uri =
      override_server.has_value() && !override_server->empty()
          ? std::move(*override_server)
          : absl::StrCat("directpath-pa.", universe_domain_);
  Json xds_server = Json::FromArray({
      Json::FromObject({
          {"server_uri", Json::FromString(std::move(server_uri))},
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
  return GrpcXdsBootstrapBuilder::Build(JsonDump(bootstrap)).value();
}

void GoogleCloud2ProdResolver::StartXdsResolver() {
  if (shutdown_) return;
  // Create XdsClient here with a custom bootstrap for C2P.  When the
  // xds resolver calls GrpcXdsClient::GetOrCreate(), it will get back
  // the same instance rather than creating one with the normal
  // bootstrap config from env vars.
  GoogleC2PConfig* config = GetGoogleC2PConfig();
  std::shared_ptr<GrpcXdsBootstrap> bootstrap;
  {
    MutexLock lock(config->mu);
    if (config->bootstrap == nullptr) {
      config->bootstrap = ConstructBootstrap();
    }
    bootstrap = config->bootstrap;
  }
  auto xds_client = GrpcXdsClient::GetOrCreate(
      xds_uri_, args_, "google-c2p resolver", std::move(bootstrap));
  args_ = ChannelArgs();  // Don't need the args anymore.
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

absl::Status SetGoogleC2PUniverseDomain(absl::string_view universe_domain) {
  if (universe_domain.empty()) {
    return absl::InvalidArgumentError("universe domain must not be empty");
  }
  GoogleC2PConfig* config = GetGoogleC2PConfig();
  MutexLock lock(config->mu);
  if (!config->universe_domain.has_value()) {
    config->universe_domain = std::string(universe_domain);
    return absl::OkStatus();
  }
  if (*config->universe_domain == universe_domain) {
    return absl::OkStatus();
  }
  return absl::FailedPreconditionError(
      absl::StrCat("universe domain already set to ", *config->universe_domain,
                   "; cannot change to ", universe_domain));
}

std::optional<std::string> GetGoogleC2PUniverseDomainForTesting() {
  GoogleC2PConfig* config = GetGoogleC2PConfig();
  MutexLock lock(config->mu);
  return config->universe_domain;
}

std::optional<std::string> GetGoogleC2PBootstrapServerUriForTesting() {
  GoogleC2PConfig* config = GetGoogleC2PConfig();
  MutexLock lock(config->mu);
  if (config->bootstrap == nullptr) return std::nullopt;
  auto servers = config->bootstrap->servers();
  if (servers.empty()) return std::nullopt;
  return servers.front()->target()->server_uri();
}

void ResetGoogleC2PResolverForTesting() {
  GoogleC2PConfig* config = GetGoogleC2PConfig();
  MutexLock lock(config->mu);
  config->universe_domain.reset();
  config->bootstrap.reset();
}

void RegisterCloud2ProdResolver(CoreConfiguration::Builder* builder) {
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<GoogleCloud2ProdResolverFactory>());
  builder->resolver_registry()->RegisterResolverFactory(
      std::make_unique<ExperimentalGoogleCloud2ProdResolverFactory>());
}

}  // namespace grpc_core

grpc_status_code grpc_google_c2p_set_universe_domain(
    const char* universe_domain, char** error_message) {
  if (error_message != nullptr) {
    *error_message = nullptr;
  }
  if (universe_domain == nullptr) {
    if (error_message != nullptr) {
      *error_message = gpr_strdup("universe domain must not be empty");
    }
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  absl::Status status = grpc_core::SetGoogleC2PUniverseDomain(universe_domain);
  if (status.ok()) {
    return GRPC_STATUS_OK;
  }
  if (error_message != nullptr) {
    *error_message = gpr_strdup(std::string(status.message()).c_str());
  }
  switch (status.code()) {
    case absl::StatusCode::kInvalidArgument:
      return GRPC_STATUS_INVALID_ARGUMENT;
    case absl::StatusCode::kFailedPrecondition:
      return GRPC_STATUS_FAILED_PRECONDITION;
    default:
      return GRPC_STATUS_UNKNOWN;
  }
}
