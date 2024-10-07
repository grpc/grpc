//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"

#include <grpc/support/port_platform.h>
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
#include "src/core/ext/filters/stateful_session/stateful_session_service_config_parser.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/xds/xds_resolver_attributes.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/util/crash.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/time.h"

namespace grpc_core {

const NoInterceptor StatefulSessionFilter::Call::OnClientToServerMessage;
const NoInterceptor StatefulSessionFilter::Call::OnClientToServerHalfClose;
const NoInterceptor StatefulSessionFilter::Call::OnServerToClientMessage;
const NoInterceptor StatefulSessionFilter::Call::OnFinalize;

UniqueTypeName XdsOverrideHostAttribute::TypeName() {
  static UniqueTypeName::Factory kFactory("xds_override_host");
  return kFactory.Create();
}

const grpc_channel_filter StatefulSessionFilter::kFilter =
    MakePromiseBasedFilter<StatefulSessionFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata>();

absl::StatusOr<std::unique_ptr<StatefulSessionFilter>>
StatefulSessionFilter::Create(const ChannelArgs&,
                              ChannelFilter::Args filter_args) {
  return std::make_unique<StatefulSessionFilter>(filter_args);
}

StatefulSessionFilter::StatefulSessionFilter(ChannelFilter::Args filter_args)
    : index_(filter_args.instance_id()),
      service_config_parser_index_(
          StatefulSessionServiceConfigParser::ParserIndex()) {}

namespace {

absl::string_view AllocateStringOnArena(
    absl::string_view src1, absl::string_view src2 = absl::string_view()) {
  if (src1.empty() && src2.empty()) {
    return absl::string_view();
  }
  char* arena_allocated_value =
      static_cast<char*>(GetContext<Arena>()->Alloc(src1.size() + src2.size()));
  memcpy(arena_allocated_value, src1.data(), src1.size());
  if (!src2.empty()) {
    memcpy(arena_allocated_value + src1.size(), src2.data(), src2.size());
  }
  return absl::string_view(arena_allocated_value, src1.size() + src2.size());
}

// Adds the set-cookie header to the server initial metadata if needed.
void MaybeUpdateServerInitialMetadata(
    const StatefulSessionMethodParsedConfig::CookieConfig* cookie_config,
    bool cluster_changed, absl::string_view actual_cluster,
    absl::string_view cookie_address_list,
    XdsOverrideHostAttribute* override_host_attribute,
    ServerMetadata& server_initial_metadata) {
  // If cookie doesn't need to change, do nothing.
  if (cookie_address_list == override_host_attribute->actual_address_list() &&
      !cluster_changed) {
    return;
  }
  // Construct new cookie value.
  std::string new_value = absl::StrCat(
      override_host_attribute->actual_address_list(), ";", actual_cluster);
  std::vector<std::string> parts = {absl::StrCat(
      *cookie_config->name, "=", absl::Base64Escape(new_value), "; HttpOnly")};
  if (!cookie_config->path.empty()) {
    parts.emplace_back(absl::StrCat("Path=", cookie_config->path));
  }
  if (cookie_config->ttl > Duration::Zero()) {
    parts.emplace_back(
        absl::StrCat("Max-Age=", cookie_config->ttl.as_timespec().tv_sec));
  }
  server_initial_metadata.Append(
      "set-cookie", Slice::FromCopiedString(absl::StrJoin(parts, "; ")),
      [](absl::string_view error, const Slice&) {
        Crash(absl::StrCat("ERROR ADDING set-cookie METADATA: ", error));
      });
}

// Returns an arena-allocated string containing the cluster name
// to use for this RPC, which will live long enough to use when modifying
// the server's initial metadata. If cluster_from_cookie is non-empty and
// points to a cluster present in the selected route, uses that; otherwise,
// uses the cluster selected by the XdsConfigSelector.
// Returns the empty string if cluster override cannot be used (i.e., the route
// uses a cluster specifier plugin).
absl::string_view GetClusterToUse(
    absl::string_view cluster_from_cookie,
    ServiceConfigCallData* service_config_call_data) {
  // Get cluster assigned by the XdsConfigSelector.
  auto cluster_attribute =
      service_config_call_data->GetCallAttribute<XdsClusterAttribute>();
  CHECK_NE(cluster_attribute, nullptr);
  auto current_cluster = cluster_attribute->cluster();
  static constexpr absl::string_view kClusterPrefix = "cluster:";
  // If prefix is not "cluster:", then we can't use cluster override.
  if (!absl::ConsumePrefix(&current_cluster, kClusterPrefix)) {
    return absl::string_view();
  }
  // No cluster in cookie, use the cluster from the attribute
  if (cluster_from_cookie.empty()) {
    return AllocateStringOnArena(current_cluster);
  }
  // Use cluster from the cookie if it is configured for the route.
  auto route_data =
      service_config_call_data->GetCallAttribute<XdsRouteStateAttribute>();
  CHECK_NE(route_data, nullptr);
  // Cookie cluster was not configured for route - use the one from the
  // attribute
  if (!route_data->HasClusterForRoute(cluster_from_cookie)) {
    return AllocateStringOnArena(current_cluster);
  }
  auto arena_allocated_cluster =
      AllocateStringOnArena(kClusterPrefix, cluster_from_cookie);
  // Update the cluster name attribute with an arena allocated value.
  cluster_attribute->set_cluster(arena_allocated_cluster);
  return absl::StripPrefix(arena_allocated_cluster, kClusterPrefix);
}

std::string GetCookieValue(const ClientMetadata& client_initial_metadata,
                           absl::string_view cookie_name) {
  // Check to see if the cookie header is present.
  std::string buffer;
  auto header_value = client_initial_metadata.GetStringValue("cookie", &buffer);
  if (!header_value.has_value()) return "";
  // Parse cookie header.
  std::vector<absl::string_view> values;
  for (absl::string_view cookie : absl::StrSplit(*header_value, "; ")) {
    std::pair<absl::string_view, absl::string_view> kv =
        absl::StrSplit(cookie, absl::MaxSplits('=', 1));
    if (kv.first == cookie_name) values.push_back(kv.second);
  }
  if (values.empty()) return "";
  // TODO(roth): Figure out the right behavior for multiple cookies.
  // For now, just choose the first value.
  std::string decoded;
  if (absl::Base64Unescape(values.front(), &decoded)) {
    return decoded;
  }
  return "";
}

bool IsConfiguredPath(absl::string_view configured_path,
                      const ClientMetadata& client_initial_metadata) {
  // No path configured meaning all paths match
  if (configured_path.empty()) {
    return true;
  }
  // Check to see if the configured path matches the request path.
  const Slice* path_slice =
      client_initial_metadata.get_pointer(HttpPathMetadata());
  CHECK_NE(path_slice, nullptr);
  absl::string_view path = path_slice->as_string_view();
  // Matching criteria from
  // https://www.rfc-editor.org/rfc/rfc6265#section-5.1.4.
  // The cookie-path is a prefix of the request-path (and)
  if (!absl::StartsWith(path, configured_path)) {
    return false;
  }
  // One of
  // 1. The cookie-path and the request-path are identical.
  // 2. The last character of the cookie-path is %x2F ("/").
  // 3. The first character of the request-path that is not included
  //    in the cookie-path is a %x2F ("/") character.
  return path.length() == configured_path.length() ||
         configured_path.back() == '/' || path[configured_path.length()] == '/';
}
}  // namespace

void StatefulSessionFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, StatefulSessionFilter* filter) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "StatefulSessionFilter::Call::OnClientInitialMetadata");
  // Get config.
  auto* service_config_call_data = GetContext<ServiceConfigCallData>();
  CHECK_NE(service_config_call_data, nullptr);
  auto* method_params = static_cast<StatefulSessionMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          filter->service_config_parser_index_));
  CHECK_NE(method_params, nullptr);
  cookie_config_ = method_params->GetConfig(filter->index_);
  CHECK_NE(cookie_config_, nullptr);
  if (!cookie_config_->name.has_value() ||
      !IsConfiguredPath(cookie_config_->path, md)) {
    return;
  }
  // Base64-decode cookie value.
  std::string cookie_value = GetCookieValue(md, *cookie_config_->name);
  // Cookie format is "host;cluster"
  std::pair<absl::string_view, absl::string_view> host_cluster =
      absl::StrSplit(cookie_value, absl::MaxSplits(';', 1));
  // Allocate the string on the arena, so that it has the right lifetime.
  if (!host_cluster.first.empty()) {
    cookie_address_list_ = AllocateStringOnArena(host_cluster.first);
  }
  // Set override host attribute.
  override_host_attribute_ =
      GetContext<Arena>()->ManagedNew<XdsOverrideHostAttribute>(
          cookie_address_list_);
  service_config_call_data->SetCallAttribute(override_host_attribute_);
  // Check if the cluster override is valid, and apply it if necessary.
  // Note that cluster_name will point to an arena-allocated string
  // that will still be alive when we see the server initial metadata.
  // If the cluster name is empty, that means we cannot use a
  // cluster override (i.e., the route uses a cluster specifier plugin).
  cluster_name_ =
      GetClusterToUse(host_cluster.second, service_config_call_data);
  cluster_changed_ = cluster_name_ != host_cluster.second;
  perform_filtering_ = true;
}

void StatefulSessionFilter::Call::OnServerInitialMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "StatefulSessionFilter::Call::OnServerInitialMetadata");
  if (!perform_filtering_) return;
  // Add cookie to server initial metadata if needed.
  MaybeUpdateServerInitialMetadata(cookie_config_, cluster_changed_,
                                   cluster_name_, cookie_address_list_,
                                   override_host_attribute_, md);
}

void StatefulSessionFilter::Call::OnServerTrailingMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "StatefulSessionFilter::Call::OnServerTrailingMetadata");
  if (!perform_filtering_) return;
  // If we got a Trailers-Only response, then add the
  // cookie to the trailing metadata instead of the
  // initial metadata.
  if (md.get(GrpcTrailersOnly()).value_or(false)) {
    MaybeUpdateServerInitialMetadata(cookie_config_, cluster_changed_,
                                     cluster_name_, cookie_address_list_,
                                     override_host_attribute_, md);
  }
}

void StatefulSessionFilterRegister(CoreConfiguration::Builder* builder) {
  StatefulSessionServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
