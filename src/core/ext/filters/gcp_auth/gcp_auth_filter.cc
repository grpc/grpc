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

#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/gcp_auth/gcp_auth_service_config_parser.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/xds/xds_resolver_attributes.h"
#include "src/core/service_config/service_config_call_data.h"

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

// FIXME: this needs to move to channel init time
  // Get filter config.
  auto* service_config_call_data = GetContext<ServiceConfigCallData>();
  CHECK_NE(service_config_call_data, nullptr);
  ServiceConfig* service_config = service_config_call_data->service_config();
  CHECK_NE(service_config, nullptr);
  auto* filter_config = static_cast<GcpAuthenticationParsedConfig*>(
      service_config->GetGlobalParsedConfig(
          filter->service_config_parser_index_));
  CHECK_NE(filter_config, nullptr);
  auto* filter_instance_config = filter_config->GetConfig(filter->index_);
  CHECK_NE(filter_instance_config, nullptr);

  // Get the cluster name chosen for this RPC.
  auto cluster_attribute =
      service_config_call_data->GetCallAttribute<XdsClusterAttribute>();
  CHECK_NE(cluster_attribute, nullptr);
  absl::string_view cluster_name = cluster_attribute->cluster();
  // Look up the CDS resource for the cluster.
  auto it = filter->xds_config_.find(cluster_name);
  CHECK(it != filter->xds_config_.end());
  CHECK(it->second.cluster != nullptr);
  auto md_it = it->second.cluster->metadata.find(
      filter_instance_config->filter_instance_name);
  // If no audience in the cluster, then no need to add call creds.
  if (md_it == it->second.cluster->metadata.end()) return absl::OkStatus();
  // If the entry is present but the wrong type, fail the RPC.
  if (md_it->second.type != "extensions.filters.http.gcp_authn.v3.Audience") {
    return absl::UnavailableError(absl::StrCat(
        "audience configuration in wrong format for cluster ", cluster_name));
  }
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
    ctx = arena->New<grpc_client_security_context>(std::move(creds));
    arena->SetContext<grpc_core::SecurityContext>(ctx);
  } else {
    ctx->creds = std::move(creds);
  }
  return absl::OkStatus();
}

const grpc_channel_filter GcpAuthenticationFilter::kFilter =
    MakePromiseBasedFilter<GcpAuthenticationFilter, FilterEndpoint::kClient,
                           0>();

absl::StatusOr<std::unique_ptr<GcpAuthenticationFilter>>
GcpAuthenticationFilter::Create(const ChannelArgs& args,
                                ChannelFilter::Args filter_args) {
  return std::make_unique<GcpAuthenticationFilter>(args, filter_args);
}

GcpAuthenticationFilter::GcpAuthenticationFilter(
    const ChannelArgs& args, ChannelFilter::Args filter_args)
    : index_(filter_args.instance_id()),
      service_config_parser_index_(
          GcpAuthenticationServiceConfigParser::ParserIndex()),
      xds_config_(args.GetObjectRef<XdsConfig>()),
      // FIXME: need to configure cache max size from service config
      cache_(10) {}

RefCountedPtr<grpc_call_credentials>
GcpAuthenticationFilter::GetCallCredentials(const std::string& audience) {
  MutexLock lock(&mu_);
  cache_.GetOrInsert(
      audience,
      [](const std::string& audience) {
        return MakeRefCounted<GcpServiceAccountIdentityCredentials>(audience);
      });
}

void GcpAuthenticationFilterRegister(CoreConfiguration::Builder* builder) {
  GcpAuthenticationServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
