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

#include "envoy/config/common/mutation_rules/v3/mutation_rules.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.upbdefs.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.upbdefs.h"
#include "src/core/filter/ext_proc/ext_proc_filter.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::string_view XdsHttpExtProcFilter::ConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_proc.v3.ExternalProcessor";
}

absl::string_view XdsHttpExtProcFilter::OverrideConfigProtoName() const {
  return "envoy.extensions.filters.http.ext_proc.v3.ExtProcPerRoute";
}

void XdsHttpExtProcFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_getmsgdef(symtab);
  envoy_extensions_filters_http_ext_proc_v3_ExtProcPerRoute_getmsgdef(symtab);
  envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_getmsgdef(symtab);
}

void XdsHttpExtProcFilter::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  builder.AddFilter<ExtProcFilter>(std::move(config));
}

const grpc_channel_filter* XdsHttpExtProcFilter::channel_filter() const {
  return &ExtProcFilter::kFilterVtable;
}

namespace {

std::optional<bool> ParseHeaderProcessingMode(int32_t value,
                                              ValidationErrors* errors) {
  switch (value) {
    case envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_SEND:
      return true;
    case envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_SKIP:
      return false;
    default:
      errors->AddError(
          absl::StrCat("unsupported header processing mode value: ", value));
      [[fallthrough]];
    case envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_DEFAULT:
      return std::nullopt;
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

ProcessingMode ParseProcessingMode(
    const envoy_extensions_filters_http_ext_proc_v3_ProcessingMode* proto,
    ValidationErrors* errors) {
  ProcessingMode processing_mode;
  if (proto == nullptr) {
    errors->AddError("field not set");
    return processing_mode;
  }
  {
    ValidationErrors::ScopedField field(errors, ".request_header_mode");
    processing_mode.request_header_mode = ParseHeaderProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_request_header_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".response_header_mode");
    processing_mode.response_header_mode = ParseHeaderProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_response_header_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".response_trailer_mode");
    processing_mode.response_trailer_mode = ParseHeaderProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_response_trailer_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".request_body_mode");
    processing_mode.request_body_mode = ParseBodyProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_request_body_mode(
            proto),
        errors);
  }
  {
    ValidationErrors::ScopedField field(errors, ".response_body_mode");
    processing_mode.response_body_mode = ParseBodyProcessingMode(
        envoy_extensions_filters_http_ext_proc_v3_ProcessingMode_response_body_mode(
            proto),
        errors);
  }
  return processing_mode;
}

}  // namespace

RefCountedPtr<const FilterConfig> XdsHttpExtProcFilter::ParseTopLevelConfig(
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
  if (extension_with_matcher == nullptr) {
    errors->AddError("could not parse ext_proc filter config");
    return nullptr;
  }
  auto config = MakeRefCounted<ExtProcFilter::Config>();
  // grpc_service
  {
    ValidationErrors::ScopedField field(errors, ".grpc_service");
    config->grpc_service = std::make_shared<XdsGrpcService>(ParseXdsGrpcService(
        context,
        envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_grpc_service(
            ext_proc),
        errors));
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
  // allow_mode_override
  config->allow_mode_override =
     envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_allow_mode_override(
         ext_proc);
  size_t size;
  const auto* const* allowed_override_modes =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_allowed_override_modes(
          ext_proc, &size);
  for (size_t i = 0; i < size; ++i) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".allowed_override_modes[", i, "]"));
    config->allowed_override_modes.push_back(
        ParseProcessingMode(allowed_override_modes[i], errors));
  }
  // request_attributes
  const auto* const* request_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_request_attributes(
          ext_proc, &size);
  for (size_t i = 0; i < size; ++i) {
    config->request_attributes.push_back(
        UpbStringToStdString(request_attributes[i]));
  }
  // response_attributes
  const auto* const* response_attributes =
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
          envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_forwarding_rules(
              ext_proc);
      forwarding_rules != nullptr) {
    const auto* allowed_headers =
        envoy_extensions_filters_http_ext_proc_v3_HeaderForwardingRules_allowed_headers(
            forwarding_rules);
    if (allowed_headers != nullptr) {
      ValidationErrors::ScopedField field(
          errors, ".forwarding_rules.allowed_headers");
      config->forwarding_allowed_headers = ListStringMatcherParse(
          context, allowed_headers, errors);
    }
    const auto* disallowed_headers =
        envoy_extensions_filters_http_ext_proc_v3_HeaderForwardingRules_disallowed_headers(
            forwarding_rules);
    if (disallowed_headers != nullptr) {
      ValidationErrors::ScopedField field(
          errors, ".forwarding_rules.disallowed_headers");
      config->forwarding_disallowed_headers = ListStringMatcherParse(
          context, disallowed_headers, errors);
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
  }
  return config;
}

namespace {

struct OverrideConfig final : public FilterConfig {
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("ext_proc_override_config");
  }
  UniqueTypeName type() const override { return Type(); }

  bool Equals(const FilterConfig& other) const override {
    const auto& o = DownCast<const OverrideConfig&>(other);
    return processing_mode == o.processing_mode &&
           grpc_service == o.grpc_service &&
           request_attributes == o.request_attributes &&
           response_attributes == o.response_attributes &&
           failure_mode_allow == o.failure_mode_allow;
  }

  std::string ToString() const override {
    std::vector<std::string> parts;
    if (processing_mode.has_value()) {
      parts.push_back(
          absl::StrCat("processing_mode=", processing_mode->ToString()));
    }
    if (grpc_service.has_value()) {
      parts.push_back(absl::StrCat("grpc_service=", grpc_service->ToString());
    }
    if (!request_attributes.empty()) {
      parts.push_back(absl::StrCat("request_attributes=[",
                                   absl::StrJoin(request_attributes, ", "),
                                   "]"));
    }
    if (!response.empty()) {
      parts.push_back(absl::StrCat("response_attributes=[",
                                   absl::StrJoin(response_attributes, ", "),
                                   "]"));
    }
    if (failure_mode_allow.has_value()) {
      parts.push_back(absl::StrCat("failure_mode_allow=",
                                   failure_mode_allow ? "true" : "false"));
    }
    return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
  }

  std::optional<ProcessingMode> processing_mode;
  std::shared_ptr<XdsGrpcService> grpc_service;
  std::vector<std::string> request_attributes;
  std::vector<std::string> response_attributes;
  std::optional<bool> failure_mode_allow;
};

}  // namespace

RefCountedPtr<const FilterConfig> XdsHttpExtProcFilter::ParseOverrideConfig(
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
  if (extension_with_matcher == nullptr) {
    errors->AddError("could not parse ext_proc filter override config");
    return nullptr;
  }
  auto* overrides =
      envoy_extensions_filters_http_ext_proc_v3_ExtProcPerRoute_overrides(
          ext_proc_per_route);
  if (overrides == nullptr) return nullptr;
  ValidationErrors::ScopedField field(errors, ".overrides");
  auto config = MakeRefCounted<OverrideConfig>();
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
    config->grpc_service = std::make_shared<XdsGrpcService>(
        ParseXdsGrpcService(context, grpc_service, errors));
  }
  // request_attributes
  size_t size;
  const auto* const* request_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExtProcOverrides_request_attributes(
          overrides, &size);
  for (size_t i = 0; i < size; ++i) {
    config->request_attributes.push_back(
        UpbStringToStdString(request_attributes[i]));
  }
  // response_attributes
  const auto* const* response_attributes =
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

RefCountedPtr<const FilterConfig> XdsHttpExtProcFilter::MergeConfigs(
    RefCountedPtr<const FilterConfig> top_level_config,
    RefCountedPtr<const FilterConfig> virtual_host_override_config,
    RefCountedPtr<const FilterConfig> route_override_config,
    RefCountedPtr<const FilterConfig> cluster_weight_override_config) const {
  // Find the most specific override config.
  const FilterConfig* override_config = nullptr;
  if (cluster_weight_override_config != nullptr) {
    override_config = cluster_weight_override_config.get();
  } else if (route_override_config != nullptr) {
    override_config = route_override_config.get();
  } else if (virtual_host_override_config != nullptr) {
    override_config = virtual_host_override_config.get();
  }
  if (override_config == nullptr) return top_level_config;
  GRPC_CHECK_EQ(override_config->type(), OverrideConfig::Type());
  const OverrideConfig& o = DownCast<const OverrideConfig&>(*override_config);
  // Construct a merged config.
  auto config = MakeRefCounted<ExtProcFilter::Config>();
  *config = DownCast<const ExtProcFilter::Config&>(*top_level_config);
  if (o.processing_mode.has_value()) {
    config->processing_mode = *o.processing_mode;
  }
  if (o.grpc_service != nullptr) config->grpc_service = o.grpc_service;
  if (!o.request_attributes.empty()) {
    config->request_attributes = o.request_attributes;
  }
  if (!o.response_attributes.empty()) {
    config->response_attributes = o.response_attributes;
  }
  if (o.failure_mode_allow.has_value()) {
    config->failure_mode_allow = *o.failure_mode_allow;
  }
  return config;
}

}  // namespace grpc_core
