//
// Copyright 2024 gRPC authors.
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

#include "src/core/ext/filters/gcp_authentication/gcp_authentication_filter.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "src/core/ext/filters/gcp_authentication/gcp_authentication_service_config_parser.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/gcp_service_account_identity/gcp_service_account_identity_credentials.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/xds/xds_resolver_attributes.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_call_data.h"

namespace grpc_core {

//
// GcpAuthenticationFilter::Call
//

const NoInterceptor GcpAuthenticationFilter::Call::OnClientToServerMessage;
const NoInterceptor GcpAuthenticationFilter::Call::OnClientToServerHalfClose;
const NoInterceptor GcpAuthenticationFilter::Call::OnServerInitialMetadata;
const NoInterceptor GcpAuthenticationFilter::Call::OnServerToClientMessage;
const NoInterceptor GcpAuthenticationFilter::Call::OnServerTrailingMetadata;
const NoInterceptor GcpAuthenticationFilter::Call::OnFinalize;

absl::Status GcpAuthenticationFilter::Call::OnClientInitialMetadata(
    ClientMetadata& /*md*/, GcpAuthenticationFilter* filter) {
  // Get the cluster name chosen for this RPC.
  auto* service_config_call_data = GetContext<ServiceConfigCallData>();
  auto cluster_attribute =
      service_config_call_data->GetCallAttribute<XdsClusterAttribute>();
  if (cluster_attribute == nullptr) {
    // Can't happen, but be defensive.
    return absl::InternalError(
        "GCP authentication filter: call has no xDS cluster attribute");
  }
  absl::string_view cluster_name = cluster_attribute->cluster();
  if (!absl::ConsumePrefix(&cluster_name, "cluster:")) {
    return absl::OkStatus();  // Cluster specifier plugin.
  }
  // Look up the CDS resource for the cluster.
  auto it = filter->xds_config_->clusters.find(cluster_name);
  if (it == filter->xds_config_->clusters.end()) {
    // Can't happen, but be defensive.
    return absl::InternalError(
        absl::StrCat("GCP authentication filter: xDS cluster ", cluster_name,
                     " not found in XdsConfig"));
  }
  if (!it->second.ok()) {
    // Cluster resource had an error, so fail the call.
    // Note: For wait_for_ready calls, this does the wrong thing by
    // failing the call instead of queuing it, but there's no easy
    // way to queue the call here until we get a valid CDS resource,
    // because once that happens, a new instance of this filter will be
    // swapped in for subsequent calls, but *this* call is already tied
    // to this filter instance, which will never see the update.
    return absl::UnavailableError(
        absl::StrCat("GCP authentication filter: CDS resource unavailable for ",
                     cluster_name));
  }
  if (it->second->cluster == nullptr) {
    // Can't happen, but be defensive.
    return absl::InternalError(absl::StrCat(
        "GCP authentication filter: CDS resource not present for cluster ",
        cluster_name));
  }
  auto& metadata_map = it->second->cluster->metadata;
  const XdsMetadataValue* metadata_value =
      metadata_map.Find(filter->filter_config_->filter_instance_name);
  // If no audience in the cluster, then no need to add call creds.
  if (metadata_value == nullptr) return absl::OkStatus();
  // If the entry is present but the wrong type, fail the RPC.
  if (metadata_value->type() != XdsGcpAuthnAudienceMetadataValue::Type()) {
    return absl::UnavailableError(absl::StrCat(
        "GCP authentication filter: audience metadata in wrong format for "
        "cluster ",
        cluster_name));
  }
  // Get the call creds instance.
  auto creds = filter->cache_->Get(
      DownCast<const XdsGcpAuthnAudienceMetadataValue*>(metadata_value)->url());
  // Add the call creds instance to the call.
  auto* arena = GetContext<Arena>();
  auto* security_ctx = DownCast<grpc_client_security_context*>(
      arena->GetContext<SecurityContext>());
  if (security_ctx == nullptr) {
    security_ctx = arena->New<grpc_client_security_context>(std::move(creds));
    arena->SetContext<SecurityContext>(security_ctx);
  } else {
    security_ctx->creds = std::move(creds);
  }
  return absl::OkStatus();
}

//
// GcpAuthenticationFilter::CallCredentialsCache
//

UniqueTypeName GcpAuthenticationFilter::CallCredentialsCache::Type() {
  static UniqueTypeName::Factory factory("gcp_auth_call_creds_cache");
  return factory.Create();
}

void GcpAuthenticationFilter::CallCredentialsCache::SetMaxSize(
    size_t max_size) {
  MutexLock lock(&mu_);
  cache_.SetMaxSize(max_size);
}

RefCountedPtr<grpc_call_credentials>
GcpAuthenticationFilter::CallCredentialsCache::Get(
    const std::string& audience) {
  MutexLock lock(&mu_);
  return cache_.GetOrInsert(audience, [](const std::string& audience) {
    return MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(audience);
  });
}

//
// GcpAuthenticationFilter
//

const grpc_channel_filter GcpAuthenticationFilter::kFilter =
    MakePromiseBasedFilter<GcpAuthenticationFilter, FilterEndpoint::kClient,
                           0>();

absl::StatusOr<std::unique_ptr<GcpAuthenticationFilter>>
GcpAuthenticationFilter::Create(const ChannelArgs& args,
                                ChannelFilter::Args filter_args) {
  // Get filter config.
  auto service_config = args.GetObjectRef<ServiceConfig>();
  if (service_config == nullptr) {
    return absl::InvalidArgumentError(
        "gcp_auth: no service config in channel args");
  }
  auto* config = static_cast<const GcpAuthenticationParsedConfig*>(
      service_config->GetGlobalParsedConfig(
          GcpAuthenticationServiceConfigParser::ParserIndex()));
  if (config == nullptr) {
    return absl::InvalidArgumentError("gcp_auth: parsed config not found");
  }
  auto* filter_config = config->GetConfig(filter_args.instance_id());
  if (filter_config == nullptr) {
    return absl::InvalidArgumentError(
        "gcp_auth: filter instance ID not found in filter config");
  }
  // Get XdsConfig so that we can look up CDS resources.
  auto xds_config = args.GetObjectRef<XdsConfig>();
  if (xds_config == nullptr) {
    return absl::InvalidArgumentError(
        "gcp_auth: xds config not found in channel args");
  }
  // Get existing cache or create new one.
  auto cache = filter_args.GetOrCreateState<CallCredentialsCache>(
      filter_config->filter_instance_name, [&]() {
        return MakeRefCounted<CallCredentialsCache>(filter_config->cache_size);
      });
  // Make sure size is updated, in case we're reusing a pre-existing
  // cache but it has the wrong size.
  cache->SetMaxSize(filter_config->cache_size);
  // Instantiate filter.
  return std::unique_ptr<GcpAuthenticationFilter>(
      new GcpAuthenticationFilter(std::move(service_config), filter_config,
                                  std::move(xds_config), std::move(cache)));
}

GcpAuthenticationFilter::GcpAuthenticationFilter(
    RefCountedPtr<ServiceConfig> service_config,
    const GcpAuthenticationParsedConfig::Config* filter_config,
    RefCountedPtr<const XdsConfig> xds_config,
    RefCountedPtr<CallCredentialsCache> cache)
    : service_config_(std::move(service_config)),
      filter_config_(filter_config),
      xds_config_(std::move(xds_config)),
      cache_(std::move(cache)) {}

void GcpAuthenticationFilterRegister(CoreConfiguration::Builder* builder) {
  GcpAuthenticationServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
