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

#include <grpc/support/port_platform.h>

#include <string.h>

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_client_grpc.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

namespace {

class GoogleCloud2ProdResolver : public Resolver {
 public:
  explicit GoogleCloud2ProdResolver(ResolverArgs args);

  void StartLocked() override;
  void RequestReresolutionLocked() override;
  void ResetBackoffLocked() override;
  void ShutdownLocked() override;

 private:
  // Represents an HTTP request to the metadata server.
  class MetadataQuery : public InternallyRefCounted<MetadataQuery> {
   public:
    MetadataQuery(RefCountedPtr<GoogleCloud2ProdResolver> resolver,
                  const char* path, grpc_polling_entity* pollent);
    ~MetadataQuery() override;

    void Orphan() override;

   private:
    static void OnHttpRequestDone(void* arg, grpc_error_handle error);

    // If error is not GRPC_ERROR_NONE, then it's not safe to look at response.
    virtual void OnDone(GoogleCloud2ProdResolver* resolver,
                        const grpc_http_response* response,
                        grpc_error_handle error) = 0;

    RefCountedPtr<GoogleCloud2ProdResolver> resolver_;
    OrphanablePtr<HttpRequest> http_request_;
    grpc_http_response response_;
    grpc_closure on_done_;
  };

  // A metadata server query to get the zone.
  class ZoneQuery : public MetadataQuery {
   public:
    ZoneQuery(RefCountedPtr<GoogleCloud2ProdResolver> resolver,
              grpc_polling_entity* pollent);

   private:
    void OnDone(GoogleCloud2ProdResolver* resolver,
                const grpc_http_response* response,
                grpc_error_handle error) override;
  };

  // A metadata server query to get the IPv6 address.
  class IPv6Query : public MetadataQuery {
   public:
    IPv6Query(RefCountedPtr<GoogleCloud2ProdResolver> resolver,
              grpc_polling_entity* pollent);

   private:
    void OnDone(GoogleCloud2ProdResolver* resolver,
                const grpc_http_response* response,
                grpc_error_handle error) override;
  };

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

  OrphanablePtr<ZoneQuery> zone_query_;
  absl::optional<std::string> zone_;

  OrphanablePtr<IPv6Query> ipv6_query_;
  absl::optional<bool> supports_ipv6_;
};

//
// GoogleCloud2ProdResolver::MetadataQuery
//

GoogleCloud2ProdResolver::MetadataQuery::MetadataQuery(
    RefCountedPtr<GoogleCloud2ProdResolver> resolver, const char* path,
    grpc_polling_entity* pollent)
    : resolver_(std::move(resolver)) {
  // Start HTTP request.
  GRPC_CLOSURE_INIT(&on_done_, OnHttpRequestDone, this, nullptr);
  Ref().release();  // Ref held by callback.
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                             const_cast<char*>("Google")};
  request.hdr_count = 1;
  request.hdrs = &header;
  auto uri = URI::Create("http", resolver_->metadata_server_name_, path,
                         {} /* query params */, "" /* fragment */);
  GPR_ASSERT(uri.ok());  // params are hardcoded
  grpc_arg resource_quota_arg = grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_RESOURCE_QUOTA),
      resolver_->resource_quota_.get(), grpc_resource_quota_arg_vtable());
  grpc_channel_args args = {1, &resource_quota_arg};
  http_request_ = HttpRequest::Get(
      std::move(*uri), &args, pollent, &request,
      ExecCtx::Get()->Now() + Duration::Seconds(10),  // 10s timeout
      &on_done_, &response_,
      RefCountedPtr<grpc_channel_credentials>(
          grpc_insecure_credentials_create()));
  http_request_->Start();
}

GoogleCloud2ProdResolver::MetadataQuery::~MetadataQuery() {
  grpc_http_response_destroy(&response_);
}

void GoogleCloud2ProdResolver::MetadataQuery::Orphan() {
  http_request_.reset();
  Unref();
}

void GoogleCloud2ProdResolver::MetadataQuery::OnHttpRequestDone(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<MetadataQuery*>(arg);
  // Hop back into WorkSerializer to call OnDone().
  // Note: We implicitly pass our ref to the callback here.
  (void)GRPC_ERROR_REF(error);
  self->resolver_->work_serializer_->Run(
      [self, error]() {
        self->OnDone(self->resolver_.get(), &self->response_, error);
        self->Unref();
      },
      DEBUG_LOCATION);
}

//
// GoogleCloud2ProdResolver::ZoneQuery
//

GoogleCloud2ProdResolver::ZoneQuery::ZoneQuery(
    RefCountedPtr<GoogleCloud2ProdResolver> resolver,
    grpc_polling_entity* pollent)
    : MetadataQuery(std::move(resolver), "/computeMetadata/v1/instance/zone",
                    pollent) {}

void GoogleCloud2ProdResolver::ZoneQuery::OnDone(
    GoogleCloud2ProdResolver* resolver, const grpc_http_response* response,
    grpc_error_handle error) {
  absl::StatusOr<std::string> zone;
  if (!GRPC_ERROR_IS_NONE(error)) {
    zone = absl::UnknownError(
        absl::StrCat("error fetching zone from metadata server: ",
                     grpc_error_std_string(error)));
  } else if (response->status != 200) {
    zone = absl::UnknownError(absl::StrFormat(
        "zone query received non-200 status: %d", response->status));
  } else {
    absl::string_view body(response->body, response->body_length);
    size_t i = body.find_last_of('/');
    if (i == body.npos) {
      zone = absl::UnknownError(
          absl::StrCat("could not parse zone from metadata server: ", body));
    } else {
      zone = std::string(body.substr(i + 1));
    }
  }
  if (!zone.ok()) {
    gpr_log(GPR_ERROR, "zone query failed: %s",
            zone.status().ToString().c_str());
    resolver->ZoneQueryDone("");
  } else {
    resolver->ZoneQueryDone(std::move(*zone));
  }
  GRPC_ERROR_UNREF(error);
}

//
// GoogleCloud2ProdResolver::IPv6Query
//

GoogleCloud2ProdResolver::IPv6Query::IPv6Query(
    RefCountedPtr<GoogleCloud2ProdResolver> resolver,
    grpc_polling_entity* pollent)
    : MetadataQuery(std::move(resolver),
                    "/computeMetadata/v1/instance/network-interfaces/0/ipv6s",
                    pollent) {}

void GoogleCloud2ProdResolver::IPv6Query::OnDone(
    GoogleCloud2ProdResolver* resolver, const grpc_http_response* response,
    grpc_error_handle error) {
  if (!GRPC_ERROR_IS_NONE(error)) {
    gpr_log(GPR_ERROR, "error fetching IPv6 address from metadata server: %s",
            grpc_error_std_string(error).c_str());
  }
  resolver->IPv6QueryDone(GRPC_ERROR_IS_NONE(error) && response->status == 200);
  GRPC_ERROR_UNREF(error);
}

//
// GoogleCloud2ProdResolver
//

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
  if (!running_on_gcp ||
      // If the client is already using xDS, we can't use it here, because
      // they may be talking to a completely different xDS server than we
      // want to.
      // TODO(roth): When we implement xDS federation, remove this constraint.
      UniquePtr<char>(gpr_getenv("GRPC_XDS_BOOTSTRAP")) != nullptr ||
      UniquePtr<char>(gpr_getenv("GRPC_XDS_BOOTSTRAP_CONFIG")) != nullptr) {
    using_dns_ = true;
    child_resolver_ =
        CoreConfiguration::Get().resolver_registry().CreateResolver(
            absl::StrCat("dns:", name_to_resolve).c_str(), args.args,
            args.pollset_set, work_serializer_, std::move(args.result_handler));
    GPR_ASSERT(child_resolver_ != nullptr);
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
  child_resolver_ = CoreConfiguration::Get().resolver_registry().CreateResolver(
      absl::StrCat("xds:", name_to_resolve).c_str(), args.args,
      args.pollset_set, work_serializer_, std::move(args.result_handler));
  GPR_ASSERT(child_resolver_ != nullptr);
}

void GoogleCloud2ProdResolver::StartLocked() {
  if (using_dns_) {
    child_resolver_->StartLocked();
    return;
  }
  // Using xDS.  Start metadata server queries.
  zone_query_ = MakeOrphanable<ZoneQuery>(Ref(), &pollent_);
  ipv6_query_ = MakeOrphanable<IPv6Query>(Ref(), &pollent_);
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
      {"id", absl::StrCat("C2P-", dist(mt))},
  };
  if (!zone_->empty()) {
    node["locality"] = Json::Object{
        {"zone", *zone_},
    };
  };
  if (*supports_ipv6_) {
    node["metadata"] = Json::Object{
        {"TRAFFICDIRECTOR_DIRECTPATH_C2P_IPV6_CAPABLE", true},
    };
  }
  // Allow the TD server uri to be overridden for testing purposes.
  UniquePtr<char> override_server(
      gpr_getenv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI"));
  const char* server_uri =
      override_server != nullptr && strlen(override_server.get()) > 0
          ? override_server.get()
          : "directpath-pa.googleapis.com";
  Json xds_server = Json::Array{
      Json::Object{
          {"server_uri", server_uri},
          {"channel_creds",
           Json::Array{
               Json::Object{
                   {"type", "google_default"},
               },
           }},
          {"server_features", Json::Array{"xds_v3"}},
      },
  };
  Json bootstrap = Json::Object{
      {"xds_servers", xds_server},
      {"authorities",
       Json::Object{
           {"traffic-director-c2p.xds.googleapis.com",
            Json::Object{
                {"xds_servers", std::move(xds_server)},
            }},
       }},
      {"node", std::move(node)},
  };
  // Inject bootstrap JSON as fallback config.
  internal::SetXdsFallbackBootstrapConfig(bootstrap.Dump().c_str());
  // Now start xDS resolver.
  child_resolver_->StartLocked();
}

//
// Factory
//

class GoogleCloud2ProdResolverFactory : public ResolverFactory {
 public:
  absl::string_view scheme() const override { return "google-c2p"; }

  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
      gpr_log(GPR_ERROR, "google-c2p URI scheme does not support authorities");
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
class ExperimentalGoogleCloud2ProdResolverFactory : public ResolverFactory {
 public:
  absl::string_view scheme() const override {
    return "google-c2p-experimental";
  }

  bool IsValidUri(const URI& uri) const override {
    if (GPR_UNLIKELY(!uri.authority().empty())) {
      gpr_log(
          GPR_ERROR,
          "google-c2p-experimental URI scheme does not support authorities");
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
      absl::make_unique<GoogleCloud2ProdResolverFactory>());
  builder->resolver_registry()->RegisterResolverFactory(
      absl::make_unique<ExperimentalGoogleCloud2ProdResolverFactory>());
}

}  // namespace grpc_core
