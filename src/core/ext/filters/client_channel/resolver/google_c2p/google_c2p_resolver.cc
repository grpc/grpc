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

#include <random>

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

namespace {

class GoogleCloud2ProdResolver : public Resolver {
 public:
  explicit GoogleCloud2ProdResolver(ResolverArgs args);

  ~GoogleCloud2ProdResolver() { grpc_channel_args_destroy(channel_args_); }

  void StartLocked() override;
  void RequestReresolutionLocked() override;
  void ResetBackoffLocked() override;
  void ShutdownLocked() override;

 private:
  // Represents an HTTP request to the metadata server.
  class MetadataQuery : public InternallyRefCounted<MetadataQuery> {
   public:
    MetadataQuery(RefCountedPtr<GoogleCloud2ProdResolver> resolver,
                  const char* address, const char* path,
                  grpc_polling_entity* pollent);
    ~MetadataQuery() override;

    void Orphan() override;

   private:
    static void OnHttpRequestDone(void* arg, grpc_error_handle error);

    // If error is not GRPC_ERROR_NONE, then it's not safe to look at response.
    virtual void OnDone(GoogleCloud2ProdResolver* resolver,
                        const grpc_http_response* response,
                        grpc_error_handle error) = 0;

    RefCountedPtr<GoogleCloud2ProdResolver> resolver_;
    OrphanablePtr<HttpCli> httpcli_;
    grpc_httpcli_response response_;
    grpc_closure on_done_;
    std::atomic<bool> on_done_called_{false};
  };

  // A metadata server query to get the zone.
  class ZoneQuery : public MetadataQuery {
   public:
    ZoneQuery(RefCountedPtr<GoogleCloud2ProdResolver> resolver,
              const char* address, grpc_polling_entity* pollent);

   private:
    void OnDone(GoogleCloud2ProdResolver* resolver,
                const grpc_http_response* response,
                grpc_error_handle error) override;
  };

  // A metadata server query to get the IPv6 address.
  class IPv6Query : public MetadataQuery {
   public:
    IPv6Query(RefCountedPtr<GoogleCloud2ProdResolver> resolver,
              const char* address, grpc_polling_entity* pollent);

   private:
    void OnDone(GoogleCloud2ProdResolver* resolver,
                const grpc_http_response* response,
                grpc_error_handle error) override;
  };

  void ZoneQueryDone(absl::StatusOr<std::string> zone);
  void IPv6QueryDone(absl::Status ipv6_supported);
  void StartXdsResolver();

  std::string name_to_resolve_;
  grpc_channel_args* channel_args_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_polling_entity pollent_;
  // non-null iff child_resolver_ has not yet been created
  std::unique_ptr<Resolver::ResultHandler> result_handler_;
  bool using_dns_ = false;
  OrphanablePtr<Resolver> child_resolver_;
  std::string metadata_server_address_;
  bool shutdown_ = false;

  OrphanablePtr<ZoneQuery> zone_query_;
  absl::optional<absl::StatusOr<std::string>> zone_;

  OrphanablePtr<IPv6Query> ipv6_query_;
  absl::optional<absl::Status> supports_ipv6_;
};

//
// GoogleCloud2ProdResolver::MetadataQuery
//

GoogleCloud2ProdResolver::MetadataQuery::MetadataQuery(
    RefCountedPtr<GoogleCloud2ProdResolver> resolver, const char* address,
    const char* path, grpc_polling_entity* pollent)
    : resolver_(std::move(resolver)) {
  // Start HTTP request.
  GRPC_CLOSURE_INIT(&on_done_, OnHttpRequestDone, this, nullptr);
  Ref().release();  // Ref held by callback.
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                             const_cast<char*>("Google")};
  request.host = const_cast<char*>(address);
  request.http.path = const_cast<char*>(path);
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  // TODO(ctiller): share the quota from whomever instantiates this!
  httpcli_ = HttpCli::Get(
      pollent, ResourceQuota::Default(), &request,
      absl::make_unique<HttpCli::PlaintextHttpCliHandshaker::Factory>(),
      ExecCtx::Get()->Now() + 10000,  // 10s timeout
      &on_done_, &response_);
  httpcli_->Start();
}

GoogleCloud2ProdResolver::MetadataQuery::~MetadataQuery() {
  grpc_http_response_destroy(&response_);
}

void GoogleCloud2ProdResolver::MetadataQuery::Orphan() {
  httpcli_.reset();
  Unref();
}

void GoogleCloud2ProdResolver::MetadataQuery::OnHttpRequestDone(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<MetadataQuery*>(arg);
  // Hop back into WorkSerializer to call OnDone().
  // Note: We implicitly pass our ref to the callback here.
  GRPC_ERROR_REF(error);
  self->resolver_->work_serializer_->Run(
      [self, error]() {
        self->OnDone(self->resolver_.get(), &self->response_, error);
        self->Unref();
      },
      DEBUG_LOCATION);
}

GoogleCloud2ProdResolver::ZoneQuery::ZoneQuery(
    RefCountedPtr<GoogleCloud2ProdResolver> resolver, const char* address,
    grpc_polling_entity* pollent)
    : MetadataQuery(std::move(resolver), address,
                    "/computeMetadata/v1/instance/zone", pollent) {}

void GoogleCloud2ProdResolver::ZoneQuery::OnDone(
    GoogleCloud2ProdResolver* resolver, const grpc_http_response* response,
    grpc_error_handle error) {
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "error fetching zone from metadata server: %s",
            grpc_error_std_string(error).c_str());
  }
  absl::StatusOr<std::string> zone;
  if (error != GRPC_ERROR_NONE) {
    zone = grpc_error_to_absl_status(error);
  } else if (response->status != 200) {
    zone = absl::UnknownError(
        absl::StrFormat("response status: %d", response->status));
  } else {
    absl::string_view body(response->body, response->body_length);
    size_t i = body.find_last_of('/');
    if (i == body.npos) {
      zone = absl::UnknownError(
          absl::StrCat("could not parse zone from metadata server: ", body));
      gpr_log(GPR_ERROR, "%s", zone.status().ToString().c_str());
    } else {
      zone = std::string(body.substr(i + 1));
    }
  }
  resolver->ZoneQueryDone(std::move(zone));
  GRPC_ERROR_UNREF(error);
}

//
// GoogleCloud2ProdResolver::IPv6Query
//

GoogleCloud2ProdResolver::IPv6Query::IPv6Query(
    RefCountedPtr<GoogleCloud2ProdResolver> resolver, const char* address,
    grpc_polling_entity* pollent)
    : MetadataQuery(std::move(resolver), address,
                    "/computeMetadata/v1/instance/network-interfaces/0/ipv6s",
                    pollent) {}

void GoogleCloud2ProdResolver::IPv6Query::OnDone(
    GoogleCloud2ProdResolver* resolver, const grpc_http_response* response,
    grpc_error_handle error) {
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "error fetching IPv6 address from metadata server: %s",
            grpc_error_std_string(error).c_str());
  }
  absl::Status status;
  if (error != GRPC_ERROR_NONE) {
    status = grpc_error_to_absl_status(error);
  } else if (response->status != 200) {
    status = absl::UnknownError(
        absl::StrFormat("response status: %d", response->status));
  } else {
    status = absl::OkStatus();
  }
  resolver->IPv6QueryDone(status);
  GRPC_ERROR_UNREF(error);
}

//
// GoogleCloud2ProdResolver
//

GoogleCloud2ProdResolver::GoogleCloud2ProdResolver(ResolverArgs args)
    : channel_args_(grpc_channel_args_copy(args.args)),
      work_serializer_(std::move(args.work_serializer)),
      pollent_(grpc_polling_entity_create_from_pollset_set(args.pollset_set)),
      result_handler_(std::move(args.result_handler)) {
  name_to_resolve_ = std::string(absl::StripPrefix(args.uri.path(), "/"));
  // If we're not running on GCP, we can't use DirectPath, so delegate
  // to the DNS resolver.
  bool test_only_pretend_running_on_gcp = grpc_channel_args_find_bool(
      args.args, "grpc.testing.google_c2p_resolver_pretend_running_on_gcp",
      false);
  bool running_on_gcp =
      test_only_pretend_running_on_gcp || grpc_alts_is_running_on_gcp();
  if (!running_on_gcp ||
      // If the client is already using xDS, we can't use it here, because
      // they may be talking to a completely different xDS server than we
      // want to.
      // TODO(roth): When we implement xDS federation, remove this constraint.
      UniquePtr<char>(gpr_getenv("GRPC_XDS_BOOTSTRAP")) != nullptr ||
      UniquePtr<char>(gpr_getenv("GRPC_XDS_BOOTSTRAP_CONFIG")) != nullptr) {
    using_dns_ = true;
    child_resolver_ = ResolverRegistry::CreateResolver(
        absl::StrCat("dns:", name_to_resolve_).c_str(), channel_args_,
        args.pollset_set, work_serializer_, std::move(result_handler_));
    GPR_ASSERT(child_resolver_ != nullptr);
    return;
  }
  char* test_only_metadata_server_override =
      const_cast<char*>(grpc_channel_args_find_string(
          args.args,
          "grpc.testing.google_c2p_resolver_metadata_server_override"));
  if (test_only_metadata_server_override != nullptr &&
      strlen(test_only_metadata_server_override) > 0) {
    metadata_server_address_ = std::string(test_only_metadata_server_override);
  }
}

void GoogleCloud2ProdResolver::StartLocked() {
  if (using_dns_) {
    child_resolver_->StartLocked();
    return;
  }
  // Using xDS.  Start metadata server queries.
  zone_query_ = MakeOrphanable<ZoneQuery>(
      Ref(), metadata_server_address_.c_str(), &pollent_);
  ipv6_query_ = MakeOrphanable<IPv6Query>(
      Ref(), metadata_server_address_.c_str(), &pollent_);
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

void GoogleCloud2ProdResolver::ZoneQueryDone(absl::StatusOr<std::string> zone) {
  zone_query_.reset();
  zone_ = std::move(zone);
  if (supports_ipv6_.has_value()) StartXdsResolver();
}

void GoogleCloud2ProdResolver::IPv6QueryDone(absl::Status ipv6_supported) {
  ipv6_query_.reset();
  supports_ipv6_ = ipv6_supported;
  if (zone_.has_value()) StartXdsResolver();
}

void GoogleCloud2ProdResolver::StartXdsResolver() {
  if (shutdown_) {
    Result result;
    absl::Status status = absl::CancelledError(absl::StrFormat(
        "C2P resolver shutdown, IPv6 query status: %s, zone query status: %s",
        supports_ipv6_.value().ToString(), zone_.value().status().ToString()));
    result.addresses = status;
    result.service_config = status;
    result_handler_->ReportResult(std::move(result));
    return;
  }
  // Construct bootstrap JSON.
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<uint64_t> dist(1, UINT64_MAX);
  Json::Object node = {
      {"id", absl::StrCat("C2P-", dist(mt))},
  };
  if (!zone_.value().status().ok()) {
    node["locality"] = Json::Object{
        {"zone", zone_.value().value()},
    };
  };
  if (supports_ipv6_.value().ok()) {
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
  Json bootstrap = Json::Object{
      {"xds_servers",
       Json::Array{
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
       }},
      {"node", std::move(node)},
  };
  // Inject bootstrap JSON as fallback config.
  internal::SetXdsFallbackBootstrapConfig(bootstrap.Dump().c_str());
  // Create and start the xds resolver.
  grpc_pollset_set* pollset_set = grpc_polling_entity_pollset_set(&pollent_);
  GPR_ASSERT(pollset_set != nullptr);
  child_resolver_ = ResolverRegistry::CreateResolver(
      absl::StrCat("xds:", name_to_resolve_).c_str(), channel_args_,
      pollset_set, work_serializer_, std::move(result_handler_));
  GPR_ASSERT(child_resolver_ != nullptr);
  child_resolver_->StartLocked();
}

//
// Factory
//

class GoogleCloud2ProdResolverFactory : public ResolverFactory {
 public:
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

  // TODO(roth): Remove experimental suffix once this code is proven stable,
  // and update the scheme in google_c2p_resolver_test.cc when doing so.
  const char* scheme() const override { return "google-c2p-experimental"; }
};

}  // namespace

void GoogleCloud2ProdResolverInit() {
  ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<GoogleCloud2ProdResolverFactory>());
}

void GoogleCloud2ProdResolverShutdown() {}

}  // namespace grpc_core
