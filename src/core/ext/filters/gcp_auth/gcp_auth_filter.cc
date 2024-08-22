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

#include "src/core/ext/filters/gcp_auth/gcp_auth_filter.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/gcp_auth/gcp_auth_service_config_parser.h"
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
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"

namespace grpc_core {

using XdsConfig = XdsDependencyManager::XdsConfig;

const NoInterceptor GcpAuthenticationFilter::Call::OnClientToServerMessage;
const NoInterceptor GcpAuthenticationFilter::Call::OnClientToServerHalfClose;
const NoInterceptor GcpAuthenticationFilter::Call::OnServerInitialMetadata;
const NoInterceptor GcpAuthenticationFilter::Call::OnServerToClientMessage;
const NoInterceptor GcpAuthenticationFilter::Call::OnServerTrailingMetadata;
const NoInterceptor GcpAuthenticationFilter::Call::OnFinalize;

namespace {

struct Audience {
  std::string url;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs& args) {
    static const auto* loader = JsonObjectLoader<Audience>()
        .Field("a", &Audience::url)
        .Finish();
    return loader;
  }
};

}  // namespace

absl::Status GcpAuthenticationFilter::Call::OnClientInitialMetadata(
    ClientMetadata& /*md*/, GcpAuthenticationFilter* filter) {
  // Get the cluster name chosen for this RPC.
  auto* service_config_call_data = GetContext<ServiceConfigCallData>();
  CHECK_NE(service_config_call_data, nullptr);
  auto cluster_attribute =
      service_config_call_data->GetCallAttribute<XdsClusterAttribute>();
  CHECK_NE(cluster_attribute, nullptr);
  absl::string_view cluster_name = cluster_attribute->cluster();
  // Look up the CDS resource for the cluster.
  auto it = filter->xds_config_->clusters.find(cluster_name);
  CHECK(it != filter->xds_config_->clusters.end());
  if (!it->second.ok()) return absl::OkStatus();  // Will fail later.
  CHECK(it->second->cluster != nullptr);
  auto& metadata_map = it->second->cluster->metadata;
  auto md_it = metadata_map.find(filter->filter_config_->filter_instance_name);
  // If no audience in the cluster, then no need to add call creds.
  if (md_it == metadata_map.end()) return absl::OkStatus();
  // If the entry is present but the wrong type, fail the RPC.
  if (md_it->second.type != kXdsAudienceClusterMetadataType) {
    return absl::UnavailableError(absl::StrCat(
        "audience metadata in wrong format for cluster ", cluster_name));
  }
// FIXME: store metadata in parsed form so we don't need to validate
// JSON on a per-call basis
  auto audience = LoadFromJson<Audience>(md_it->second.json);
  if (!audience.ok()) {
    return absl::UnavailableError(absl::StrCat(
        "audience configuration invalid for cluster ", cluster_name, ": ",
        audience.status().message()));
  }
  // Get the call creds instance.
  auto creds = filter->GetCallCredentials(audience->url);
  CHECK(creds != nullptr);
  // Add the call creds instance to the call.
  auto* arena = GetContext<Arena>();
  auto* security_ctx = DownCast<grpc_client_security_context*>(
      arena->GetContext<SecurityContext>());
  if (security_ctx == nullptr) {
    security_ctx = arena->New<grpc_client_security_context>(std::move(creds));
    arena->SetContext<grpc_core::SecurityContext>(security_ctx);
  } else {
    security_ctx->creds = std::move(creds);
  }
  return absl::OkStatus();
}

const grpc_channel_filter GcpAuthenticationFilter::kFilter =
    MakePromiseBasedFilter<GcpAuthenticationFilter, FilterEndpoint::kClient,
                           0>();

absl::StatusOr<std::unique_ptr<GcpAuthenticationFilter>>
GcpAuthenticationFilter::Create(const ChannelArgs& args,
                                ChannelFilter::Args filter_args) {
  auto* service_config = args.GetObject<ServiceConfig>();
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
  auto xds_config = args.GetObjectRef<XdsConfig>();
  if (xds_config == nullptr) {
    return absl::InvalidArgumentError(
        "gcp_auth: xds config not found in channel args");
  }
  return std::make_unique<GcpAuthenticationFilter>(
      filter_config, std::move(xds_config));
}

GcpAuthenticationFilter::GcpAuthenticationFilter(
    const GcpAuthenticationParsedConfig::Config* filter_config,
    RefCountedPtr<const XdsConfig> xds_config)
    : filter_config_(filter_config),
      xds_config_(std::move(xds_config)),
      cache_(filter_config->cache_size) {}

RefCountedPtr<grpc_call_credentials>
GcpAuthenticationFilter::GetCallCredentials(const std::string& audience) {
  MutexLock lock(&mu_);
  return cache_.GetOrInsert(
      audience,
      [](const std::string& audience) {
        return MakeRefCounted<GcpServiceAccountIdentityCallCredentials>(
            audience);
      });
}

void GcpAuthenticationFilterRegister(CoreConfiguration::Builder* builder) {
  GcpAuthenticationServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
