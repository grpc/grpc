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

#include "src/core/xds/grpc/xds_http_stateful_session_filter.h"

#include <string>
#include <utility>
#include <variant>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/filters/http/stateful_session/v3/stateful_session.upb.h"
#include "envoy/extensions/filters/http/stateful_session/v3/stateful_session.upbdefs.h"
#include "envoy/extensions/http/stateful_session/cookie/v3/cookie.upb.h"
#include "envoy/extensions/http/stateful_session/cookie/v3/cookie.upbdefs.h"
#include "envoy/type/http/v3/cookie.upb.h"
#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_http_filter.h"

namespace grpc_core {

absl::string_view XdsHttpStatefulSessionFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.stateful_session.v3.StatefulSession";
}

absl::string_view XdsHttpStatefulSessionFilter::OverrideConfigProtoName()
    const {
  return "envoy.extensions.filters.http.stateful_session.v3"
         ".StatefulSessionPerRoute";
}

void XdsHttpStatefulSessionFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_stateful_session_v3_StatefulSession_getmsgdef(
      symtab);
  envoy_extensions_filters_http_stateful_session_v3_StatefulSessionPerRoute_getmsgdef(
      symtab);
  envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_getmsgdef(
      symtab);
}

void XdsHttpStatefulSessionFilter::AddFilter(
    InterceptionChainBuilder& builder) const {
  builder.Add<StatefulSessionFilter>();
}

void XdsHttpStatefulSessionFilter::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  builder.AddFilter<StatefulSessionFilter>(std::move(config));
}

const grpc_channel_filter* XdsHttpStatefulSessionFilter::channel_filter()
    const {
  return &StatefulSessionFilter::kFilterVtable;
}

namespace {

RefCountedPtr<StatefulSessionFilter::Config> ParseStatefulSession(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_filters_http_stateful_session_v3_StatefulSession*
        stateful_session,
    ValidationErrors* errors) {
  auto config = MakeRefCounted<StatefulSessionFilter::Config>();
  ValidationErrors::ScopedField field(errors, ".session_state");
  const auto* session_state =
      envoy_extensions_filters_http_stateful_session_v3_StatefulSession_session_state(
          stateful_session);
  if (session_state == nullptr) return config;
  ValidationErrors::ScopedField field2(errors, ".typed_config");
  const auto* typed_config =
      envoy_config_core_v3_TypedExtensionConfig_typed_config(session_state);
  auto extension = ExtractXdsExtension(context, typed_config, errors);
  if (!extension.has_value()) return config;
  if (extension->type !=
      "envoy.extensions.http.stateful_session.cookie.v3"
      ".CookieBasedSessionState") {
    errors->AddError("unsupported session state type");
    return config;
  }
  const absl::string_view* serialized_session_state =
      std::get_if<absl::string_view>(&extension->value);
  if (serialized_session_state == nullptr) {
    errors->AddError("could not parse session state config");
    return config;
  }
  auto* cookie_state =
      envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_parse(
          serialized_session_state->data(), serialized_session_state->size(),
          context.arena);
  if (cookie_state == nullptr) {
    errors->AddError("could not parse session state config");
    return config;
  }
  ValidationErrors::ScopedField field3(errors, ".cookie");
  const auto* cookie =
      envoy_extensions_http_stateful_session_cookie_v3_CookieBasedSessionState_cookie(
          cookie_state);
  if (cookie == nullptr) {
    errors->AddError("field not present");
    return config;
  }
  // name
  config->cookie_name =
      UpbStringToStdString(envoy_type_http_v3_Cookie_name(cookie));
  if (config->cookie_name.empty()) {
    ValidationErrors::ScopedField field(errors, ".name");
    errors->AddError("field not present");
  }
  // ttl
  if (const auto* duration = envoy_type_http_v3_Cookie_ttl(cookie);
      duration != nullptr) {
    ValidationErrors::ScopedField field(errors, ".ttl");
    config->ttl = ParseDuration(duration, errors);
  }
  // path
  config->path = UpbStringToStdString(envoy_type_http_v3_Cookie_path(cookie));
  return config;
}

}  // namespace

RefCountedPtr<const FilterConfig>
XdsHttpStatefulSessionFilter::ParseTopLevelConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse stateful session filter config");
    return nullptr;
  }
  auto* stateful_session =
      envoy_extensions_filters_http_stateful_session_v3_StatefulSession_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (stateful_session == nullptr) {
    errors->AddError("could not parse stateful session filter config");
    return nullptr;
  }
  return ParseStatefulSession(context, stateful_session, errors);
}

RefCountedPtr<const FilterConfig>
XdsHttpStatefulSessionFilter::ParseOverrideConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse stateful session filter override config");
    return nullptr;
  }
  auto* stateful_session_per_route =
      envoy_extensions_filters_http_stateful_session_v3_StatefulSessionPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (stateful_session_per_route == nullptr) {
    errors->AddError("could not parse stateful session filter override config");
    return nullptr;
  }
  if (!envoy_extensions_filters_http_stateful_session_v3_StatefulSessionPerRoute_disabled(
          stateful_session_per_route)) {
    ValidationErrors::ScopedField field(errors, ".stateful_session");
    const auto* stateful_session =
        envoy_extensions_filters_http_stateful_session_v3_StatefulSessionPerRoute_stateful_session(
            stateful_session_per_route);
    if (stateful_session != nullptr) {
      return ParseStatefulSession(context, stateful_session, errors);
    }
  }
  // Return an empty config.  This is used to disable the filter.
  return MakeRefCounted<StatefulSessionFilter::Config>();
}

RefCountedPtr<const FilterConfig> XdsHttpStatefulSessionFilter::MergeConfigs(
    RefCountedPtr<const FilterConfig> top_level_config,
    RefCountedPtr<const FilterConfig> virtual_host_override_config,
    RefCountedPtr<const FilterConfig> route_override_config,
    RefCountedPtr<const FilterConfig> cluster_weight_override_config) const {
  // No merging here, we just use the most specific config.
  if (cluster_weight_override_config != nullptr) {
    return cluster_weight_override_config;
  }
  if (route_override_config != nullptr) {
    return route_override_config;
  }
  if (virtual_host_override_config != nullptr) {
    return virtual_host_override_config;
  }
  return top_level_config;
}

}  // namespace grpc_core
