//
// Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_common_types.h"

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "xds/type/v3/typed_struct.upb.h"

namespace grpc_core {

//
// CommonTlsContext::CertificateValidationContext
//

std::string CommonTlsContext::CertificateValidationContext::ToString() const {
  std::vector<std::string> contents;
  for (const auto& match : match_subject_alt_names) {
    contents.push_back(match.ToString());
  }
  return absl::StrFormat("{match_subject_alt_names=[%s]}",
                         absl::StrJoin(contents, ", "));
}

bool CommonTlsContext::CertificateValidationContext::Empty() const {
  return match_subject_alt_names.empty();
}

//
// CommonTlsContext::CertificateProviderPluginInstance
//

std::string CommonTlsContext::CertificateProviderPluginInstance::ToString()
    const {
  absl::InlinedVector<std::string, 2> contents;
  if (!instance_name.empty()) {
    contents.push_back(absl::StrFormat("instance_name=%s", instance_name));
  }
  if (!certificate_name.empty()) {
    contents.push_back(
        absl::StrFormat("certificate_name=%s", certificate_name));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool CommonTlsContext::CertificateProviderPluginInstance::Empty() const {
  return instance_name.empty() && certificate_name.empty();
}

//
// CommonTlsContext
//

std::string CommonTlsContext::ToString() const {
  absl::InlinedVector<std::string, 2> contents;
  if (!tls_certificate_provider_instance.Empty()) {
    contents.push_back(
        absl::StrFormat("tls_certificate_provider_instance=%s",
                        tls_certificate_provider_instance.ToString()));
  }
  if (!certificate_validation_context.Empty()) {
    contents.push_back(
        absl::StrFormat("certificate_validation_context=%s",
                        certificate_validation_context.ToString()));
  }
  return absl::StrCat("{", absl::StrJoin(contents, ", "), "}");
}

bool CommonTlsContext::Empty() const {
  return tls_certificate_provider_instance.Empty() &&
         certificate_validation_context.Empty();
}

namespace {

// CertificateProviderInstance is deprecated but we are still supporting it for
// backward compatibility reasons. Note that we still parse the data into the
// same CertificateProviderPluginInstance struct since the fields are the same.
// TODO(yashykt): Remove this once we stop supporting the old way of fetching
// certificate provider instances.
grpc_error_handle CertificateProviderInstanceParse(
    const XdsEncodingContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance*
        certificate_provider_instance_proto,
    CommonTlsContext::CertificateProviderPluginInstance*
        certificate_provider_plugin_instance) {
  *certificate_provider_plugin_instance = {
      UpbStringToStdString(
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_instance_name(
              certificate_provider_instance_proto)),
      UpbStringToStdString(
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_certificate_name(
              certificate_provider_instance_proto))};
  if (context.certificate_provider_definition_map->find(
          certificate_provider_plugin_instance->instance_name) ==
      context.certificate_provider_definition_map->end()) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("Unrecognized certificate provider instance name: ",
                     certificate_provider_plugin_instance->instance_name));
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle CertificateProviderPluginInstanceParse(
    const XdsEncodingContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance*
        certificate_provider_plugin_instance_proto,
    CommonTlsContext::CertificateProviderPluginInstance*
        certificate_provider_plugin_instance) {
  *certificate_provider_plugin_instance = {
      UpbStringToStdString(
          envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_instance_name(
              certificate_provider_plugin_instance_proto)),
      UpbStringToStdString(
          envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_certificate_name(
              certificate_provider_plugin_instance_proto))};
  if (context.certificate_provider_definition_map->find(
          certificate_provider_plugin_instance->instance_name) ==
      context.certificate_provider_definition_map->end()) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("Unrecognized certificate provider instance name: ",
                     certificate_provider_plugin_instance->instance_name));
  }
  return GRPC_ERROR_NONE;
}

grpc_error_handle CertificateValidationContextParse(
    const XdsEncodingContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext*
        certificate_validation_context_proto,
    CommonTlsContext::CertificateValidationContext*
        certificate_validation_context) {
  std::vector<grpc_error_handle> errors;
  size_t len = 0;
  auto* subject_alt_names_matchers =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_match_subject_alt_names(
          certificate_validation_context_proto, &len);
  for (size_t i = 0; i < len; ++i) {
    StringMatcher::Type type;
    std::string matcher;
    if (envoy_type_matcher_v3_StringMatcher_has_exact(
            subject_alt_names_matchers[i])) {
      type = StringMatcher::Type::kExact;
      matcher = UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_exact(
          subject_alt_names_matchers[i]));
    } else if (envoy_type_matcher_v3_StringMatcher_has_prefix(
                   subject_alt_names_matchers[i])) {
      type = StringMatcher::Type::kPrefix;
      matcher = UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_prefix(
          subject_alt_names_matchers[i]));
    } else if (envoy_type_matcher_v3_StringMatcher_has_suffix(
                   subject_alt_names_matchers[i])) {
      type = StringMatcher::Type::kSuffix;
      matcher = UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_suffix(
          subject_alt_names_matchers[i]));
    } else if (envoy_type_matcher_v3_StringMatcher_has_contains(
                   subject_alt_names_matchers[i])) {
      type = StringMatcher::Type::kContains;
      matcher =
          UpbStringToStdString(envoy_type_matcher_v3_StringMatcher_contains(
              subject_alt_names_matchers[i]));
    } else if (envoy_type_matcher_v3_StringMatcher_has_safe_regex(
                   subject_alt_names_matchers[i])) {
      type = StringMatcher::Type::kSafeRegex;
      auto* regex_matcher = envoy_type_matcher_v3_StringMatcher_safe_regex(
          subject_alt_names_matchers[i]);
      matcher = UpbStringToStdString(
          envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
    } else {
      errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Invalid StringMatcher specified"));
      continue;
    }
    bool ignore_case = envoy_type_matcher_v3_StringMatcher_ignore_case(
        subject_alt_names_matchers[i]);
    absl::StatusOr<StringMatcher> string_matcher =
        StringMatcher::Create(type, matcher,
                              /*case_sensitive=*/!ignore_case);
    if (!string_matcher.ok()) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrCat("string matcher: ", string_matcher.status().message())));
      continue;
    }
    if (type == StringMatcher::Type::kSafeRegex && ignore_case) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "StringMatcher: ignore_case has no effect for SAFE_REGEX."));
      continue;
    }
    certificate_validation_context->match_subject_alt_names.push_back(
        std::move(string_matcher.value()));
  }
  auto* ca_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_ca_certificate_provider_instance(
          certificate_validation_context_proto);
  if (ca_certificate_provider_instance != nullptr) {
    grpc_error_handle error = CertificateProviderPluginInstanceParse(
        context, ca_certificate_provider_instance,
        &certificate_validation_context->ca_certificate_provider_instance);
    if (error != GRPC_ERROR_NONE) errors.push_back(error);
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_verify_certificate_spki(
          certificate_validation_context_proto, nullptr) != nullptr) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "CertificateValidationContext: verify_certificate_spki "
        "unsupported"));
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_verify_certificate_hash(
          certificate_validation_context_proto, nullptr) != nullptr) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "CertificateValidationContext: verify_certificate_hash "
        "unsupported"));
  }
  auto* require_signed_certificate_timestamp =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_require_signed_certificate_timestamp(
          certificate_validation_context_proto);
  if (require_signed_certificate_timestamp != nullptr &&
      google_protobuf_BoolValue_value(require_signed_certificate_timestamp)) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "CertificateValidationContext: "
        "require_signed_certificate_timestamp unsupported"));
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_has_crl(
          certificate_validation_context_proto)) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "CertificateValidationContext: crl unsupported"));
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_has_custom_validator_config(
          certificate_validation_context_proto)) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "CertificateValidationContext: custom_validator_config "
        "unsupported"));
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR(
      "Error parsing CertificateValidationContext", &errors);
}

}  // namespace

grpc_error_handle CommonTlsContext::Parse(
    const XdsEncodingContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    CommonTlsContext* common_tls_context) {
  std::vector<grpc_error_handle> errors;
  // The validation context is derived from the oneof in
  // 'validation_context_type'. 'validation_context_sds_secret_config' is not
  // supported.
  auto* combined_validation_context =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_combined_validation_context(
          common_tls_context_proto);
  if (combined_validation_context != nullptr) {
    auto* default_validation_context =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_default_validation_context(
            combined_validation_context);
    if (default_validation_context != nullptr) {
      grpc_error_handle error = CertificateValidationContextParse(
          context, default_validation_context,
          &common_tls_context->certificate_validation_context);
      if (error != GRPC_ERROR_NONE) errors.push_back(error);
    }
    // If after parsing default_validation_context,
    // common_tls_context->certificate_validation_context.ca_certificate_provider_instance
    // is empty, fall back onto
    // 'validation_context_certificate_provider_instance' inside
    // 'combined_validation_context'. Note that this way of fetching root
    // certificates is deprecated and will be removed in the future.
    // TODO(yashykt): Remove this once it's no longer needed.
    auto* validation_context_certificate_provider_instance =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_validation_context_certificate_provider_instance(
            combined_validation_context);
    if (common_tls_context->certificate_validation_context
            .ca_certificate_provider_instance.Empty() &&
        validation_context_certificate_provider_instance != nullptr) {
      grpc_error_handle error = CertificateProviderInstanceParse(
          context, validation_context_certificate_provider_instance,
          &common_tls_context->certificate_validation_context
               .ca_certificate_provider_instance);
      if (error != GRPC_ERROR_NONE) errors.push_back(error);
    }
  } else {
    auto* validation_context =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_validation_context(
            common_tls_context_proto);
    if (validation_context != nullptr) {
      grpc_error_handle error = CertificateValidationContextParse(
          context, validation_context,
          &common_tls_context->certificate_validation_context);
      if (error != GRPC_ERROR_NONE) errors.push_back(error);
    } else if (
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_validation_context_sds_secret_config(
            common_tls_context_proto)) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "validation_context_sds_secret_config unsupported"));
    }
  }
  auto* tls_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_provider_instance(
          common_tls_context_proto);
  if (tls_certificate_provider_instance != nullptr) {
    grpc_error_handle error = CertificateProviderPluginInstanceParse(
        context, tls_certificate_provider_instance,
        &common_tls_context->tls_certificate_provider_instance);
    if (error != GRPC_ERROR_NONE) errors.push_back(error);
  } else {
    // Fall back onto 'tls_certificate_certificate_provider_instance'. Note that
    // this way of fetching identity certificates is deprecated and will be
    // removed in the future.
    // TODO(yashykt): Remove this once it's no longer needed.
    auto* tls_certificate_certificate_provider_instance =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_certificate_provider_instance(
            common_tls_context_proto);
    if (tls_certificate_certificate_provider_instance != nullptr) {
      grpc_error_handle error = CertificateProviderInstanceParse(
          context, tls_certificate_certificate_provider_instance,
          &common_tls_context->tls_certificate_provider_instance);
      if (error != GRPC_ERROR_NONE) errors.push_back(error);
    } else {
      if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_tls_certificates(
              common_tls_context_proto)) {
        errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "tls_certificates unsupported"));
      }
      if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_tls_certificate_sds_secret_configs(
              common_tls_context_proto)) {
        errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "tls_certificate_sds_secret_configs unsupported"));
      }
    }
  }
  if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_tls_params(
          common_tls_context_proto)) {
    errors.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("tls_params unsupported"));
  }
  if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_custom_handshaker(
          common_tls_context_proto)) {
    errors.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("custom_handshaker unsupported"));
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("Error parsing CommonTlsContext",
                                       &errors);
}

grpc_error_handle ExtractHttpFilterTypeName(const XdsEncodingContext& context,
                                            const google_protobuf_Any* any,
                                            absl::string_view* filter_type) {
  *filter_type = UpbStringToAbsl(google_protobuf_Any_type_url(any));
  if (*filter_type == "type.googleapis.com/xds.type.v3.TypedStruct" ||
      *filter_type == "type.googleapis.com/udpa.type.v1.TypedStruct") {
    upb_strview any_value = google_protobuf_Any_value(any);
    const auto* typed_struct = xds_type_v3_TypedStruct_parse(
        any_value.data, any_value.size, context.arena);
    if (typed_struct == nullptr) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "could not parse TypedStruct from filter config");
    }
    *filter_type =
        UpbStringToAbsl(xds_type_v3_TypedStruct_type_url(typed_struct));
  }
  *filter_type = absl::StripPrefix(*filter_type, "type.googleapis.com/");
  return GRPC_ERROR_NONE;
}

}  // namespace grpc_core
