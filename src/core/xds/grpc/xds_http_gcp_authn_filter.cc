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

#include "src/core/xds/grpc/xds_http_gcp_authn_filter.h"

#include <grpc/support/json.h>

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upbdefs.h"
#include "src/core/ext/filters/gcp_authentication/gcp_authentication_filter.h"
#include "src/core/ext/filters/gcp_authentication/gcp_authentication_service_config_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_http_filter.h"

namespace grpc_core {

absl::string_view XdsHttpGcpAuthnFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig";
}

absl::string_view XdsHttpGcpAuthnFilter::OverrideConfigProtoName() const {
  return "";
}

void XdsHttpGcpAuthnFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_gcp_authn_v3_GcpAuthnFilterConfig_getmsgdef(
      symtab);
}

namespace {

Json::Object ValidateFilterConfig(
    absl::string_view instance_name,
    const envoy_extensions_filters_http_gcp_authn_v3_GcpAuthnFilterConfig*
        gcp_auth,
    ValidationErrors* errors) {
  Json::Object config = {
      {"filter_instance_name", Json::FromString(std::string(instance_name))}};
  const auto* cache_config =
      envoy_extensions_filters_http_gcp_authn_v3_GcpAuthnFilterConfig_cache_config(
          gcp_auth);
  if (cache_config == nullptr) return config;
  uint64_t cache_size =
      ParseUInt64Value(
          envoy_extensions_filters_http_gcp_authn_v3_TokenCacheConfig_cache_size(
              cache_config))
          .value_or(10);
  if (cache_size == 0) {
    ValidationErrors::ScopedField field(errors, ".cache_config.cache_size");
    errors->AddError("must be greater than 0");
  }
  config["cache_size"] = Json::FromNumber(cache_size);
  return config;
}

}  // namespace

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpGcpAuthnFilter::GenerateFilterConfig(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) const {
  absl::string_view* serialized_filter_config =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse GCP auth filter config");
    return absl::nullopt;
  }
  auto* gcp_auth =
      envoy_extensions_filters_http_gcp_authn_v3_GcpAuthnFilterConfig_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (gcp_auth == nullptr) {
    errors->AddError("could not parse GCP auth filter config");
    return absl::nullopt;
  }
  return FilterConfig{ConfigProtoName(), Json::FromObject(ValidateFilterConfig(
                                             instance_name, gcp_auth, errors))};
}

absl::optional<XdsHttpFilterImpl::FilterConfig>
XdsHttpGcpAuthnFilter::GenerateFilterConfigOverride(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& /*context*/,
    XdsExtension /*extension*/, ValidationErrors* errors) const {
  errors->AddError("GCP auth filter does not support config override");
  return absl::nullopt;
}

void XdsHttpGcpAuthnFilter::AddFilter(InterceptionChainBuilder& builder) const {
  builder.Add<GcpAuthenticationFilter>();
}

const grpc_channel_filter* XdsHttpGcpAuthnFilter::channel_filter() const {
  return &GcpAuthenticationFilter::kFilter;
}

ChannelArgs XdsHttpGcpAuthnFilter::ModifyChannelArgs(
    const ChannelArgs& args) const {
  return args.Set(GRPC_ARG_PARSE_GCP_AUTHENTICATION_METHOD_CONFIG, 1);
}

absl::StatusOr<XdsHttpFilterImpl::ServiceConfigJsonEntry>
XdsHttpGcpAuthnFilter::GenerateMethodConfig(
    const FilterConfig& /*hcm_filter_config*/,
    const FilterConfig* /*filter_config_override*/) const {
  return ServiceConfigJsonEntry{"", ""};
}

absl::StatusOr<XdsHttpFilterImpl::ServiceConfigJsonEntry>
XdsHttpGcpAuthnFilter::GenerateServiceConfig(
    const FilterConfig& hcm_filter_config) const {
  return ServiceConfigJsonEntry{"gcp_authentication",
                                JsonDump(hcm_filter_config.config)};
}

}  // namespace grpc_core
