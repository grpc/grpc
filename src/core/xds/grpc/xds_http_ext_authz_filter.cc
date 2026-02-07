//
// Copyright 2026 gRPC authors.
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

#include "src/core/xds/grpc/xds_http_ext_authz_filter.h"

#include <grpc/grpc.h>

#include <variant>

#include "envoy/config/core/v3/base.upb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.upb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.upbdefs.h"
#include "envoy/type/v3/http_status.upb.h"
#include "envoy/type/v3/percent.upb.h"
#include "src/core/ext/filters/ext_authz/ext_authz_filter.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"

namespace grpc_core {

absl::string_view XdsExtAuthzFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_authz.v3.ExtAuthz";
}

absl::string_view XdsExtAuthzFilter::OverrideConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute";
}

void XdsExtAuthzFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_getmsgdef(symtab);
}

std::optional<XdsHttpFilterImpl::FilterConfig>
XdsExtAuthzFilter::GenerateFilterConfig(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) const {
  absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse ext_authz filter config");
    return std::nullopt;
  }
  auto* ext_authz = envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_parse(
      serialized_filter_config->data(), serialized_filter_config->size(),
      context.arena);
  if (ext_authz == nullptr) {
    errors->AddError("could not parse ext_authz filter config");
    return std::nullopt;
  }

  Json::Object config = {
      {"filter_instance_name", Json::FromString(std::string(instance_name))}};

  Json::Object ext_authz_config = {};

  // XdsGrpcService
  {
    const auto* grpc_service_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_grpc_service(
            ext_authz);
    if (grpc_service_proto == nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".ext_authz_config.grpc_service");
      errors->AddError("grpc_service field must be present");
    } else {
      auto grpc_service =
          ParseXdsGrpcService(context, grpc_service_proto, errors);
      auto target_json = JsonParse(grpc_service.ToJsonString());
      if (target_json.ok()) {
        ext_authz_config["xds_grpc_service"] = *target_json;
      }
    }
  }
  // FilterEnabled
  {
    const auto* filter_enabled_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_filter_enabled(
            ext_authz);
    if (filter_enabled_proto == nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".ext_authz_config.filter_enabled");
      errors->AddError("filter_enabled field is not present");
    } else {
      auto default_value =
          envoy_config_core_v3_RuntimeFractionalPercent_default_value(
              filter_enabled_proto);
      if (default_value == nullptr) {
        ValidationErrors::ScopedField field(
            errors, ".ext_authz_config.filter_enabled.default_value");
        errors->AddError(
            "default_value field must be present inside filter_enabled");
      } else {
        auto numerator =
            envoy_type_v3_FractionalPercent_numerator(default_value);
        auto denominator =
            envoy_type_v3_FractionalPercent_denominator(default_value);
        switch (denominator) {
          case envoy_type_v3_FractionalPercent_HUNDRED:
            denominator = 100;
            break;
          case envoy_type_v3_FractionalPercent_TEN_THOUSAND:
            denominator = 10000;
            break;
          case envoy_type_v3_FractionalPercent_MILLION:
            denominator = 1000000;
            break;
          default:
            denominator = 100;
            break;
        }
        ext_authz_config["filter_enabled"] =
            Json::FromObject({{"numerator", Json::FromNumber(numerator)},
                              {"denominator", Json::FromNumber(denominator)}});
      }
    }
    // deny_at_disable
    {
      const auto* deny_at_disable_proto =
          envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_deny_at_disable(
              ext_authz);
      if (deny_at_disable_proto == nullptr) {
        ValidationErrors::ScopedField field(
            errors, ".ext_authz_config.deny_at_disable");
        errors->AddError("deny_at_disable field is not present");
      } else {
        auto* default_value =
            envoy_config_core_v3_RuntimeFeatureFlag_default_value(
                deny_at_disable_proto);
        ext_authz_config["deny_at_disable"] =
            Json::FromBool(ParseBoolValue(default_value));
      }
    }
    // failure_mode_allow
    {
      bool failure_mode_allow =
          envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_failure_mode_allow(
              ext_authz);
      ext_authz_config["failure_mode_allow"] =
          Json::FromBool(failure_mode_allow);
    }
    // failure_mode_allow_header_add
    {
      bool failure_mode_allow_header_add =
          envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_failure_mode_allow_header_add(
              ext_authz);
      ext_authz_config["failure_mode_allow_header_add"] =
          Json::FromBool(failure_mode_allow_header_add);
    }
    // status_on_error
    {
      const auto* status_on_error_proto =
          envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_status_on_error(
              ext_authz);
      if (status_on_error_proto == nullptr) {
        ValidationErrors::ScopedField field(
            errors, ".ext_authz_config.status_on_error");
        errors->AddError("status_on_error field is not present");
      } else {
        ext_authz_config["status_on_error"] = Json::FromNumber(
            envoy_type_v3_HttpStatus_code(status_on_error_proto));
      }
    }
    // include_peer_certificate
    {
      bool include_peer_certificate =
          envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_include_peer_certificate(
              ext_authz);
      ext_authz_config["include_peer_certificate"] =
          Json::FromBool(include_peer_certificate);
    }
  }

  config["ext_authz"] = Json::FromObject(ext_authz_config);
  return FilterConfig{ConfigProtoName(), Json::FromObject(config)};
}

std::optional<XdsHttpFilterImpl::FilterConfig>
XdsExtAuthzFilter::GenerateFilterConfigOverride(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) const {
  return std::nullopt;
}

void XdsExtAuthzFilter::AddFilter(FilterChainBuilder& builder) const {
  builder.AddFilter<ExtAuthzFilter>(nullptr);
}

const grpc_channel_filter* XdsExtAuthzFilter::channel_filter() const {
  return &ExtAuthzFilter::kFilterVtable;
}

ChannelArgs XdsExtAuthzFilter::ModifyChannelArgs(
    const ChannelArgs& args) const {
  // TODO(rishesh): use it or remove it
  return args;
}

absl::StatusOr<XdsHttpFilterImpl::ServiceConfigJsonEntry>
XdsExtAuthzFilter::GenerateMethodConfig(
    const FilterConfig& hcm_filter_config,
    const FilterConfig* filter_config_override) const {
  const Json& config = filter_config_override != nullptr
                           ? filter_config_override->config
                           : hcm_filter_config.config;
  return ServiceConfigJsonEntry{"ext_authz", JsonDump(config)};
}

absl::StatusOr<XdsHttpFilterImpl::ServiceConfigJsonEntry>
XdsExtAuthzFilter::GenerateServiceConfig(
    const FilterConfig& hcm_filter_config) const {
  return ServiceConfigJsonEntry{"ext_authz",
                                JsonDump(hcm_filter_config.config)};
}

}  // namespace grpc_core
