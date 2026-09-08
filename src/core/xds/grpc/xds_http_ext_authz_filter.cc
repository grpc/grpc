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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.upb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.upb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.upbdefs.h"
#include "envoy/type/v3/http_status.upb.h"
#include "upb/reflection/def.h"

#include "absl/strings/string_view.h"

#include "src/core/ext/filters/ext_authz/ext_authz_filter.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_grpc_service_parser.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_resource_type.h"

namespace grpc_core {

absl::string_view XdsHttpExtAuthzFilterFactory::ConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_authz.v3.ExtAuthz";
}

absl::string_view XdsHttpExtAuthzFilterFactory::OverrideConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute";
}

void XdsHttpExtAuthzFilterFactory::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_getmsgdef(symtab);
  envoy_extensions_filters_http_ext_authz_v3_ExtAuthzPerRoute_getmsgdef(symtab);
}

const grpc_channel_filter* XdsHttpExtAuthzFilterFactory::channel_filter() const {
  return &ExtAuthzFilter::kFilterVtable;
}

void XdsHttpExtAuthzFilterFactory::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  builder.AddFilter<ExtAuthzFilter>(std::move(config));
}

RefCountedPtr<const FilterConfig>
XdsHttpExtAuthzFilterFactory::ParseTopLevelConfig(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse ext_authz filter config");
    return nullptr;
  }
  auto* ext_authz = envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_parse(
      serialized_filter_config->data(), serialized_filter_config->size(),
      context.arena);
  if (ext_authz == nullptr) {
    errors->AddError("could not parse ext_authz filter config");
    return nullptr;
  }
  auto ext_authz_obj = MakeRefCounted<ExtAuthz>();
  // grpc_service
  {
    ValidationErrors::ScopedField field(errors, ".grpc_service");
    ext_authz_obj->server_target = ParseXdsGrpcService(
        context,
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_grpc_service(
            ext_authz),
        errors);
    ext_authz_obj->server_uri = ext_authz_obj->server_target->server_uri();
  }
  // filter_enabled
  {
    const auto* filter_enabled_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_filter_enabled(
            ext_authz);
    if (filter_enabled_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".filter_enabled");
      auto default_value =
          envoy_config_core_v3_RuntimeFractionalPercent_default_value(
              filter_enabled_proto);
      if (default_value == nullptr) {
        ValidationErrors::ScopedField field(errors, ".default_value");
        errors->AddError("field not set");
      } else {
        ext_authz_obj->filter_enabled = ParseFractionalPercent(default_value);
      }
    }
  }
  // deny_at_disable
  {
    const auto* deny_at_disable_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_deny_at_disable(
            ext_authz);
    if (deny_at_disable_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".deny_at_disable");
      const auto* default_value =
          envoy_config_core_v3_RuntimeFeatureFlag_default_value(
              deny_at_disable_proto);
      if (default_value == nullptr) {
        ValidationErrors::ScopedField field(errors, ".default_value");
        errors->AddError("field not set");
      } else {
        ext_authz_obj->deny_at_disable = ParseBoolValue(default_value);
      }
    }
  }
  // failure_mode_allow
  ext_authz_obj->failure_mode_allow =
      envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_failure_mode_allow(
          ext_authz);
  // failure_mode_allow_header_add
  ext_authz_obj->failure_mode_allow_header_add =
      envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_failure_mode_allow_header_add(
          ext_authz);
  // status_on_error
  {
    const auto* status_on_error_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_status_on_error(
            ext_authz);
    if (status_on_error_proto != nullptr) {
      ext_authz_obj->status_on_error = grpc_http2_status_to_grpc_status(
          envoy_type_v3_HttpStatus_code(status_on_error_proto));
    }
  }
  // include_peer_certificate
  ext_authz_obj->include_peer_certificate =
      envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_include_peer_certificate(
          ext_authz);
  // allowed_headers
  {
    const auto* allowed_headers_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_allowed_headers(
            ext_authz);
    if (allowed_headers_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".allowed_headers");
      ext_authz_obj->allowed_headers =
          XdsListStringMatcherParse(context, allowed_headers_proto, errors);
    }
  }
  // disallowed_headers
  {
    const auto* disallowed_headers_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_disallowed_headers(
            ext_authz);
    if (disallowed_headers_proto != nullptr) {
      ValidationErrors::ScopedField field(errors, ".disallowed_headers");
      ext_authz_obj->disallowed_headers =
          XdsListStringMatcherParse(context, disallowed_headers_proto, errors);
    }
  }
  // HeaderMutationRules
  {
    const auto* header_mutation_rules_proto =
        envoy_extensions_filters_http_ext_authz_v3_ExtAuthz_decoder_header_mutation_rules(
            ext_authz);
    if (header_mutation_rules_proto != nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".decoder_header_mutation_rules");
      ext_authz_obj->decoder_header_mutation_rules =
          ParseHeaderMutationRules(header_mutation_rules_proto, errors);
    }
  }
  auto config = MakeRefCounted<ExtAuthzFilter::Config>();
  config->instance_name = std::string(instance_name);
  config->ext_authz = std::move(ext_authz_obj);
  return config;
}

RefCountedPtr<const FilterConfig>
XdsHttpExtAuthzFilterFactory::ParseOverrideConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse ext_authz filter override config");
    return nullptr;
  }
  auto* ext_authz_per_route =
      envoy_extensions_filters_http_ext_authz_v3_ExtAuthzPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (ext_authz_per_route == nullptr) {
    errors->AddError("could not parse ext_authz filter override config");
    return nullptr;
  }
  if (envoy_extensions_filters_http_ext_authz_v3_ExtAuthzPerRoute_disabled(
          ext_authz_per_route)) {
    auto config = MakeRefCounted<ExtAuthzFilter::Config>();
    config->disabled = true;
    return config;
  }
  return MakeRefCounted<ExtAuthzFilter::Config>();
}

RefCountedPtr<const FilterConfig> XdsHttpExtAuthzFilterFactory::MergeConfigs(
    RefCountedPtr<const FilterConfig> top_level_config,
    RefCountedPtr<const FilterConfig> virtual_host_override_config,
    RefCountedPtr<const FilterConfig> route_override_config,
    RefCountedPtr<const FilterConfig> cluster_weight_override_config,
    XdsTransportFactory& transport_factory, Blackboard& blackboard) const {
  // Find the most specific override config.
  const FilterConfig* override_config = nullptr;
  if (cluster_weight_override_config != nullptr) {
    override_config = cluster_weight_override_config.get();
  } else if (route_override_config != nullptr) {
    override_config = route_override_config.get();
  } else if (virtual_host_override_config != nullptr) {
    override_config = virtual_host_override_config.get();
  }
  if (override_config != nullptr) {
    GRPC_CHECK_EQ(override_config->type(), ExtAuthzFilter::Config::Type());
    const auto& o = DownCast<const ExtAuthzFilter::Config&>(*override_config);
    if (o.disabled || o.ext_authz == nullptr) {
      // Filter is disabled by route override.
      return nullptr;
    }
  }
  const auto& top_config =
      DownCast<const ExtAuthzFilter::Config&>(*top_level_config);
  auto config = MakeRefCounted<ExtAuthzFilter::Config>();
  config->instance_name = top_config.instance_name;
  config->ext_authz = top_config.ext_authz;
  // Blackboard handling
  if (config->ext_authz != nullptr &&
      config->ext_authz->server_target.has_value()) {
    std::string key = config->ext_authz->server_target->Key();
    config->channel_cache =
        blackboard.GetOrSet<ExtAuthzFilter::ChannelCache>(key, [&]() {
          std::shared_ptr<const XdsBootstrap::XdsServerTarget> target_shared =
              std::make_shared<GrpcXdsServerTarget>(
                  *config->ext_authz->server_target);
          return MakeRefCounted<ExtAuthzFilter::ChannelCache>(
              std::move(target_shared), transport_factory.Ref());
        });
  }
  return config;
}

}  // namespace grpc_core