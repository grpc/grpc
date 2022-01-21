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

#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"

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

    // Calls OnDone() if not already called.  Releases a ref.
    void MaybeCallOnDone(grpc_error_handle error);

    // If error is not GRPC_ERROR_NONE, then it's not safe to look at response.
    virtual void OnDone(GoogleCloud2ProdResolver* resolver,
                        const grpc_http_response* response,
                        grpc_error_handle error) = 0;

    RefCountedPtr<GoogleCloud2ProdResolver> resolver_;
    grpc_httpcli_response response_;
    grpc_closure on_done_;
    std::atomic<bool> on_done_called_{false};
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

  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_polling_entity pollent_;
  bool using_dns_ = false;
  OrphanablePtr<Resolver> child_resolver_;

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
  grpc_httpcli_request request;
  memset(&request, 0, sizeof(grpc_httpcli_request));
  grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                             const_cast<char*>("Google")};
  request.host = const_cast<char*>("metadata.google.internal");
  request.http.path = const_cast<char*>(path);
  request.http.hdr_count = 1;
  request.http.hdrs = &header;
  // TODO(ctiller): share the quota from whomever instantiates this!
  grpc_httpcli_get(pollent, ResourceQuota::Default(), &request,
                   ExecCtx::Get()->Now() + 10000,  // 10s timeout
                   &on_done_, &response_);
}

GoogleCloud2ProdResolver::MetadataQuery::~MetadataQuery() {
  grpc_http_response_destroy(&response_);
}

void GoogleCloud2ProdResolver::MetadataQuery::Orphan() {
  // TODO(roth): Once the HTTP client library supports cancellation,
  // use that here.
  MaybeCallOnDone(GRPC_ERROR_CANCELLED);
}

void GoogleCloud2ProdResolver::MetadataQuery::OnHttpRequestDone(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<MetadataQuery*>(arg);
  self->MaybeCallOnDone(GRPC_ERROR_REF(error));
}

void GoogleCloud2ProdResolver::MetadataQuery::MaybeCallOnDone(
    grpc_error_handle error) {
  bool expected = false;
  if (!on_done_called_.compare_exchange_strong(expected, true,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed)) {
    // We've already called OnDone(), so just clean up.
    GRPC_ERROR_UNREF(error);
    Unref();
    return;
  }
  // Hop back into WorkSerializer to call OnDone().
  // Note: We implicitly pass our ref to the callback here.
  resolver_->work_serializer_->Run(
      [this, error]() {
        OnDone(resolver_.get(), &response_, error);
        Unref();
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
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "error fetching zone from metadata server: %s",
            grpc_error_std_string(error).c_str());
  }
  std::string zone;
  if (error == GRPC_ERROR_NONE && response->status == 200) {
    absl::string_view body(response->body, response->body_length);
    size_t i = body.find_last_of('/');
    if (i == body.npos) {
      gpr_log(GPR_ERROR, "could not parse zone from metadata server: %s",
              std::string(body).c_str());
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
    RefCountedPtr<GoogleCloud2ProdResolver> resolver,
    grpc_polling_entity* pollent)
    : MetadataQuery(std::move(resolver),
                    "/computeMetadata/v1/instance/network-interfaces/0/ipv6s",
                    pollent) {}

void GoogleCloud2ProdResolver::IPv6Query::OnDone(
    GoogleCloud2ProdResolver* resolver, const grpc_http_response* response,
    grpc_error_handle error) {
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "error fetching IPv6 address from metadata server: %s",
            grpc_error_std_string(error).c_str());
  }
  resolver->IPv6QueryDone(error == GRPC_ERROR_NONE && response->status == 200);
  GRPC_ERROR_UNREF(error);
}

//
// GoogleCloud2ProdResolver
//

GoogleCloud2ProdResolver::GoogleCloud2ProdResolver(ResolverArgs args)
    : work_serializer_(std::move(args.work_serializer)),
      pollent_(grpc_polling_entity_create_from_pollset_set(args.pollset_set)) {
  absl::string_view name_to_resolve = absl::StripPrefix(args.uri.path(), "/");
  // If we're not running on GCP, we can't use DirectPath, so delegate
  // to the DNS resolver.
  if (!grpc_alts_is_running_on_gcp() ||
      // If the client is already using xDS, we can't use it here, because
      // they may be talking to a completely different xDS server than we
      // want to.
      // TODO(roth): When we implement xDS federation, remove this constraint.
      UniquePtr<char>(gpr_getenv("GRPC_XDS_BOOTSTRAP")) != nullptr ||
      UniquePtr<char>(gpr_getenv("GRPC_XDS_BOOTSTRAP_CONFIG")) != nullptr) {
    using_dns_ = true;
    child_resolver_ = ResolverRegistry::CreateResolver(
        absl::StrCat("dns:", name_to_resolve).c_str(), args.args,
        args.pollset_set, work_serializer_, std::move(args.result_handler));
    GPR_ASSERT(child_resolver_ != nullptr);
    return;
  }
  // Create xds resolver.
  child_resolver_ = ResolverRegistry::CreateResolver(
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
  // Now start xDS resolver.
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

  // TODO(roth): Remove experimental suffix once this code is proven stable.
  const char* scheme() const override { return "google-c2p-experimental"; }
};

}  // namespace

void GoogleCloud2ProdResolverInit() {
  ResolverRegistry::Builder::RegisterResolverFactory(
      absl::make_unique<GoogleCloud2ProdResolverFactory>());
}

void GoogleCloud2ProdResolverShutdown() {}

}  // namespace grpc_core
