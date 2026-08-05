//
// Copyright 2025 gRPC authors.
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

#include "src/core/xds/grpc/xds_http_ext_proc_filter.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.upbdefs.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upbdefs.h"
#include "re2/re2.h"
#include "src/core/filter/ext_proc/ext_proc_filter.h"
#include "src/core/filter/filter_args.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_grpc_service_parser.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::string_view XdsHttpExtProcFilterFactory::ConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_proc.v3.ExternalProcessor";
}

absl::string_view XdsHttpExtProcFilterFactory::OverrideConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_proc.v3.ExtProcPerRoute";
}

void XdsHttpExtProcFilterFactory::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_getmsgdef(symtab);
  envoy_extensions_filters_http_ext_proc_v3_ExtProcPerRoute_getmsgdef(symtab);
  envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_getmsgdef(symtab);
}

void XdsHttpExtProcFilterFactory::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  builder.AddFilter<ExtProcFilter>(std::move(config));
}

const grpc_channel_filter* XdsHttpExtProcFilterFactory::channel_filter() const {
  return &ExtProcFilter::kFilterVtable;
}

namespace {

bool ParseHeaderProcessingMode(int32_t value, ValidationErrors* errors) {
  switch (value) {
    case envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_SEND:
      return true;
    case envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_SKIP:
      return false;
    default:
      errors->AddError(
          absl::StrCat("unsupported header processing mode value: ", value));
      return false;
  }
}

bool ParseBodyProcessingMode(int32_t value, ValidationErrors* errors) {
  switch (value) {
    case envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_GRPC:
      return true;
    default:
      errors->AddError(
          absl::StrCat("unsupported body processing mode value: ", value));
      [[fallthrough]];
    case envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_NONE:
      return false;
  }
}

ExtProcFilter::ProcessingMode ParseProcessingMode(
    const envoy_extensions_filters_http_ext_proc_v3_ProcessingMode* proto,
    ValidationErrors* errors) {
  ExtProcFilter::ProcessingMode processing_mode;
  if (proto == nullptr) {
    errors->AddError("field not set");
    return processing_mode;
  }
  {
    ValidationErrors::ScopedField field(errors, ".request_header_mode");
    processing_mode.send_request_headers = ParseHeaderProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_request_header_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".response_header_mode");
    processing_mode.send_response_headers = ParseHeaderProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_response_header_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".response_trailer_mode");
    processing_mode.send_response_trailers = ParseHeaderProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_response_trailer_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".request_body_mode");
    processing_mode.send_request_body = ParseBodyProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_request_body_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".response_body_mode");
    processing_mode.send_response_body = ParseBodyProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_response_body_mode(
            proto),
        errors);
  }
  if (processing_mode.send_response_body &&
      !processing_mode.send_response_trailers) {
    ValidationErrors::ScopedField field(errors, ".response_trailer_mode");
    errors->AddError(
        "must be set to SEND if response_body_mode is set to GRPC");
  }
  return processing_mode;
}

}  // namespace

RefCountedPtr<const FilterConfig>
XdsHttpExtProcFilterFactory::ParseTopLevelConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse ext_proc filter config");
    return nullptr;
  }
  auto* ext_proc =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (ext_proc == nullptr) {
    errors->AddError("could not parse ext_proc filter config");
    return nullptr;
  }
  auto config = MakeRefCounted<ExtProcFilter::Config>();
  // grpc_service
  {
    ValidationErrors::ScopedField field(errors, ".grpc_service");
    config->channel_info = ParseXdsGrpcService(
        context,
        envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_grpc_service(
            ext_proc),
        errors);
  }
  // failure_mode_allow
  config->failure_mode_allow =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_failure_mode_allow(
          ext_proc);
  // processing_mode
  {
    ValidationErrors::ScopedField field(errors, ".processing_mode");
    config->processing_mode = ParseProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_processing_mode(
            ext_proc),
        errors);
  }
  size_t size;
  // TODO(rishesh): Validate that request_attributes and response_attributes are
  // actually valid.
  // request_attributes
  const auto* request_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_request_attributes(
          ext_proc, &size);
  for (size_t i = 0; i < size; ++i) {
    config->request_attributes.push_back(
        UpbStringToStdString(request_attributes[i]));
  }
  // response_attributes
  const auto* response_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_response_attributes(
          ext_proc, &size);
  for (size_t i = 0; i < size; ++i) {
    config->response_attributes.push_back(
        UpbStringToStdString(response_attributes[i]));
  }
  // mutation_rules
  if (const auto* mutation_rules =
          envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_mutation_rules(
              ext_proc);
      mutation_rules != nullptr) {
    ValidationErrors::ScopedField field(errors, ".mutation_rules");
    config->mutation_rules = ParseHeaderMutationRules(mutation_rules, errors);
  }
  // forwarding_rules
  if (const auto* forwarding_rules =
          envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_forward_rules(
              ext_proc);
      forwarding_rules != nullptr) {
    const auto* allowed_headers =
        envoy_extensions_filters_http_ext_proc_v3_HeaderForwardingRules_allowed_headers(
            forwarding_rules);
    if (allowed_headers != nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".forwarding_rules.allowed_headers");
      config->forwarding_allowed_headers =
          XdsListStringMatcherParse(context, allowed_headers, errors);
    }
    const auto* disallowed_headers =
        envoy_extensions_filters_http_ext_proc_v3_HeaderForwardingRules_disallowed_headers(
            forwarding_rules);
    if (disallowed_headers != nullptr) {
      ValidationErrors::ScopedField field(
          errors, ".forwarding_rules.disallowed_headers");
      config->forwarding_disallowed_headers =
          XdsListStringMatcherParse(context, disallowed_headers, errors);
    }
  }
  // disable_immediate_response
  config->disable_immediate_response =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_disable_immediate_response(
          ext_proc);
  // observability_mode
  config->observability_mode =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_observability_mode(
          ext_proc);
  // deferred_close_timeout
  const auto* deferred_close_timeout =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_deferred_close_timeout(
          ext_proc);
  if (deferred_close_timeout == nullptr) {
    config->deferred_close_timeout = Duration::Seconds(5);
  } else {
    ValidationErrors::ScopedField field(errors, ".deferred_close_timeout");
    config->deferred_close_timeout =
        ParseDuration(deferred_close_timeout, errors);
    if (config->deferred_close_timeout <= Duration::Zero()) {
      errors->AddError("duration must be positive");
    }
  }
  return config;
}

RefCountedPtr<const FilterConfig>
XdsHttpExtProcFilterFactory::ParseOverrideConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse ext_proc filter override config");
    return nullptr;
  }
  auto* ext_proc_per_route =
      envoy_extensions_filters_http_ext_proc_v3_ExtProcPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (ext_proc_per_route == nullptr) {
    errors->AddError("could not parse ext_proc filter override config");
    return nullptr;
  }
  auto config = MakeRefCounted<ExtProcFilter::Config>();
  auto* overrides =
      envoy_extensions_filters_http_ext_proc_v3_ExtProcPerRoute_overrides(
          ext_proc_per_route);
  if (overrides == nullptr) return nullptr;
  ValidationErrors::ScopedField field(errors, ".overrides");
  // processing_mode
  if (auto* processing_mode =
          envoy_extensions_filters_http_ext_proc_v3_ExtProcOverrides_processing_mode(
              overrides);
      processing_mode != nullptr) {
    ValidationErrors::ScopedField field(errors, ".processing_mode");
    config->processing_mode = ParseProcessingMode(processing_mode, errors);
  }
  // grpc_service
  if (auto* grpc_service =
          envoy_extensions_filters_http_ext_proc_v3_ExtProcOverrides_grpc_service(
              overrides);
      grpc_service != nullptr) {
    ValidationErrors::ScopedField field(errors, ".grpc_service");
    config->channel_info = ParseXdsGrpcService(context, grpc_service, errors);
  }
  // TODO(rishesh): Validate that request_attributes and response_attributes are
  // actually valid.
  // request_attributes
  size_t size;
  const auto* request_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExtProcOverrides_request_attributes(
          overrides, &size);
  for (size_t i = 0; i < size; ++i) {
    config->request_attributes.push_back(
        UpbStringToStdString(request_attributes[i]));
  }
  // response_attributes
  const auto* response_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExtProcOverrides_response_attributes(
          overrides, &size);
  for (size_t i = 0; i < size; ++i) {
    config->response_attributes.push_back(
        UpbStringToStdString(response_attributes[i]));
  }
  // failure_mode_allow
  if (auto* failure_mode_allow =
          envoy_extensions_filters_http_ext_proc_v3_ExtProcOverrides_failure_mode_allow(
              overrides);
      failure_mode_allow != nullptr) {
    config->failure_mode_allow = ParseBoolValue(failure_mode_allow);
  }
  return config;
}

RefCountedPtr<const FilterConfig> XdsHttpExtProcFilterFactory::MergeConfigs(
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
  const auto& top_config =
      DownCast<const ExtProcFilter::Config&>(*top_level_config);
  auto config = MakeRefCounted<ExtProcFilter::Config>();
  config->channel_info = top_config.channel_info;
  config->failure_mode_allow = top_config.failure_mode_allow;
  config->processing_mode = top_config.processing_mode;
  config->request_attributes = top_config.request_attributes;
  config->response_attributes = top_config.response_attributes;
  if (top_config.mutation_rules.has_value()) {
    HeaderMutationRules rules;
    rules.disallow_all = top_config.mutation_rules->disallow_all;
    rules.disallow_is_error = top_config.mutation_rules->disallow_is_error;
    if (top_config.mutation_rules->allow_expression != nullptr) {
      rules.allow_expression = std::make_unique<RE2>(
          top_config.mutation_rules->allow_expression->pattern());
    }
    if (top_config.mutation_rules->disallow_expression != nullptr) {
      rules.disallow_expression = std::make_unique<RE2>(
          top_config.mutation_rules->disallow_expression->pattern());
    }
    config->mutation_rules = std::move(rules);
  }
  config->forwarding_allowed_headers = top_config.forwarding_allowed_headers;
  config->forwarding_disallowed_headers =
      top_config.forwarding_disallowed_headers;
  config->disable_immediate_response = top_config.disable_immediate_response;
  config->observability_mode = top_config.observability_mode;
  config->deferred_close_timeout = top_config.deferred_close_timeout;
  if (override_config != nullptr) {
    GRPC_CHECK_EQ(override_config->type(), ExtProcFilter::Config::Type());
    const auto& o = DownCast<const ExtProcFilter::Config&>(*override_config);
    if (o.processing_mode.has_value()) {
      config->processing_mode = o.processing_mode;
    }
    if (std::holds_alternative<GrpcXdsServerTarget>(o.channel_info)) {
      config->channel_info = o.channel_info;
    }
    if (!o.request_attributes.empty()) {
      config->request_attributes = o.request_attributes;
    }
    if (!o.response_attributes.empty()) {
      config->response_attributes = o.response_attributes;
    }
    if (o.failure_mode_allow.has_value()) {
      config->failure_mode_allow = o.failure_mode_allow;
    }
  }
  // Blackboard handling
  if (const auto* target =
          std::get_if<GrpcXdsServerTarget>(&config->channel_info);
      target != nullptr) {
    std::string key = target->Key();
    config->channel_info =
        blackboard.GetOrSet<ExtProcFilter::ExtProcChannel>(key, [&]() {
          std::shared_ptr<const XdsBootstrap::XdsServerTarget> target_shared =
              std::make_shared<GrpcXdsServerTarget>(*target);
          return MakeRefCounted<ExtProcFilter::ExtProcChannel>(
              std::move(target_shared), transport_factory.Ref());
        });
  }
  return config;
}

}  // namespace grpc_core