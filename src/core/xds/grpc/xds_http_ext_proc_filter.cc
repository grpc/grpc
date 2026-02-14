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
#include "envoy/config/common/mutation_rules/v3/mutation_rules.upbdefs.h"
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
  {
    ValidationErrors::ScopedField field(errors, ".grpc_service");
    config->grpc_service = ParseXdsGrpcService(
        context,
        envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_grpc_service(
            ext_proc),
        errors);
  }
  config->failure_mode_allow =
     envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_failure_mode_allow(
         ext_proc);
  {
    ValidationErrors::ScopedField field(errors, ".processing_mode");
    config->processing_mode = ParseProcessingMode(
       envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_processing_mode(
           ext_proc),
       errors);
  }
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
  const auto* const* request_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_request_attributes(
          ext_proc, &size);
  for (size_t i = 0; i < size; ++i) {
    config->request_attributes.push_back(
        UpbStringToStdString(request_attributes[i]));
  }
  const auto* const* response_attributes =
      envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_response_attributes(
          ext_proc, &size);
  for (size_t i = 0; i < size; ++i) {
    config->response_attributes.push_back(
        UpbStringToStdString(response_attributes[i]));
  }
// FIXME: parse mutation_rules
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
  config->disable_immediate_response =
     envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_disable_immediate_response(
         ext_proc);
  config->observability_mode =
     envoy_extensions_filters_http_ext_proc_v3_ExternalProcessor_observability_mode(
         ext_proc);
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
  auto* extension_with_matcher =
      envoy_extensions_filters_http_ext_proc_v3_ExtProcPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (extension_with_matcher == nullptr) {
    errors->AddError("could not parse ext_proc filter override config");
    return nullptr;
  }
  auto config = MakeRefCounted<ExtProcFilter::Config>();
// FIXME:
  return config;
}

}  // namespace grpc_core
