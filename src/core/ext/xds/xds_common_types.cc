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

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/struct.upbdefs.h"
#include "google/protobuf/wrappers.upb.h"
#include "upb/base/status.h"
#include "upb/json/encode.h"
#include "upb/mem/arena.h"
#include "upb/upb.hpp"
#include "xds/type/v3/typed_struct.upb.h"

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/json/json_reader.h"

namespace grpc_core {

//
// ParseDuration()
//

Duration ParseDuration(const google_protobuf_Duration* proto_duration,
                       ValidationErrors* errors) {
  int64_t seconds = google_protobuf_Duration_seconds(proto_duration);
  if (seconds < 0 || seconds > 315576000000) {
    ValidationErrors::ScopedField field(errors, ".seconds");
    errors->AddError("value must be in the range [0, 315576000000]");
  }
  int32_t nanos = google_protobuf_Duration_nanos(proto_duration);
  if (nanos < 0 || nanos > 999999999) {
    ValidationErrors::ScopedField field(errors, ".nanos");
    errors->AddError("value must be in the range [0, 999999999]");
  }
  return Duration::FromSecondsAndNanoseconds(seconds, nanos);
}

//
// CommonTlsContext::CertificateValidationContext
//

std::string CommonTlsContext::CertificateValidationContext::ToString() const {
  std::vector<std::string> contents;
  contents.reserve(match_subject_alt_names.size());
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
  std::vector<std::string> contents;
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
  std::vector<std::string> contents;
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
CommonTlsContext::CertificateProviderPluginInstance
CertificateProviderInstanceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance*
        certificate_provider_instance_proto,
    ValidationErrors* errors) {
  CommonTlsContext::CertificateProviderPluginInstance cert_provider;
  cert_provider.instance_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_instance_name(
          certificate_provider_instance_proto));
  const auto& bootstrap =
      static_cast<const GrpcXdsBootstrap&>(context.client->bootstrap());
  if (bootstrap.certificate_providers().find(cert_provider.instance_name) ==
      bootstrap.certificate_providers().end()) {
    ValidationErrors::ScopedField field(errors, ".instance_name");
    errors->AddError(
        absl::StrCat("unrecognized certificate provider instance name: ",
                     cert_provider.instance_name));
  }
  cert_provider.certificate_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_certificate_name(
          certificate_provider_instance_proto));
  return cert_provider;
}

CommonTlsContext::CertificateProviderPluginInstance
CertificateProviderPluginInstanceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance*
        certificate_provider_plugin_instance_proto,
    ValidationErrors* errors) {
  CommonTlsContext::CertificateProviderPluginInstance cert_provider;
  cert_provider.instance_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_instance_name(
          certificate_provider_plugin_instance_proto));
  const auto& bootstrap =
      static_cast<const GrpcXdsBootstrap&>(context.client->bootstrap());
  if (bootstrap.certificate_providers().find(cert_provider.instance_name) ==
      bootstrap.certificate_providers().end()) {
    ValidationErrors::ScopedField field(errors, ".instance_name");
    errors->AddError(
        absl::StrCat("unrecognized certificate provider instance name: ",
                     cert_provider.instance_name));
  }
  cert_provider.certificate_name = UpbStringToStdString(
      envoy_extensions_transport_sockets_tls_v3_CertificateProviderPluginInstance_certificate_name(
          certificate_provider_plugin_instance_proto));
  return cert_provider;
}

CommonTlsContext::CertificateValidationContext
CertificateValidationContextParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext*
        certificate_validation_context_proto,
    ValidationErrors* errors) {
  CommonTlsContext::CertificateValidationContext certificate_validation_context;
  size_t len = 0;
  auto* subject_alt_names_matchers =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_match_subject_alt_names(
          certificate_validation_context_proto, &len);
  for (size_t i = 0; i < len; ++i) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".match_subject_alt_names[", i, "]"));
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
      errors->AddError("invalid StringMatcher specified");
      continue;
    }
    bool ignore_case = envoy_type_matcher_v3_StringMatcher_ignore_case(
        subject_alt_names_matchers[i]);
    absl::StatusOr<StringMatcher> string_matcher =
        StringMatcher::Create(type, matcher,
                              /*case_sensitive=*/!ignore_case);
    if (!string_matcher.ok()) {
      errors->AddError(string_matcher.status().message());
      continue;
    }
    if (type == StringMatcher::Type::kSafeRegex && ignore_case) {
      ValidationErrors::ScopedField field(errors, ".ignore_case");
      errors->AddError("not supported for regex matcher");
      continue;
    }
    certificate_validation_context.match_subject_alt_names.push_back(
        std::move(string_matcher.value()));
  }
  auto* ca_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_ca_certificate_provider_instance(
          certificate_validation_context_proto);
  if (ca_certificate_provider_instance != nullptr) {
    ValidationErrors::ScopedField field(errors,
                                        ".ca_certificate_provider_instance");
    certificate_validation_context.ca_certificate_provider_instance =
        CertificateProviderPluginInstanceParse(
            context, ca_certificate_provider_instance, errors);
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_verify_certificate_spki(
          certificate_validation_context_proto, nullptr) != nullptr) {
    ValidationErrors::ScopedField field(errors, ".verify_certificate_spki");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_verify_certificate_hash(
          certificate_validation_context_proto, nullptr) != nullptr) {
    ValidationErrors::ScopedField field(errors, ".verify_certificate_hash");
    errors->AddError("feature unsupported");
  }
  auto* require_signed_certificate_timestamp =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_require_signed_certificate_timestamp(
          certificate_validation_context_proto);
  if (require_signed_certificate_timestamp != nullptr &&
      google_protobuf_BoolValue_value(require_signed_certificate_timestamp)) {
    ValidationErrors::ScopedField field(
        errors, ".require_signed_certificate_timestamp");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_has_crl(
          certificate_validation_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".crl");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_has_custom_validator_config(
          certificate_validation_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".custom_validator_config");
    errors->AddError("feature unsupported");
  }
  return certificate_validation_context;
}

}  // namespace

CommonTlsContext CommonTlsContext::Parse(
    const XdsResourceType::DecodeContext& context,
    const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
        common_tls_context_proto,
    ValidationErrors* errors) {
  CommonTlsContext common_tls_context;
  // The validation context is derived from the oneof in
  // 'validation_context_type'. 'validation_context_sds_secret_config' is not
  // supported.
  auto* combined_validation_context =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_combined_validation_context(
          common_tls_context_proto);
  if (combined_validation_context != nullptr) {
    ValidationErrors::ScopedField field(errors, ".combined_validation_context");
    auto* default_validation_context =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_default_validation_context(
            combined_validation_context);
    if (default_validation_context != nullptr) {
      ValidationErrors::ScopedField field(errors,
                                          ".default_validation_context");
      common_tls_context.certificate_validation_context =
          CertificateValidationContextParse(context, default_validation_context,
                                            errors);
    }
    // If after parsing default_validation_context,
    // common_tls_context->certificate_validation_context.ca_certificate_provider_instance
    // is empty, fall back onto
    // 'validation_context_certificate_provider_instance' inside
    // 'combined_validation_context'. Note that this way of fetching root
    // certificates is deprecated and will be removed in the future.
    // TODO(yashykt): Remove this once it's no longer needed.
    const auto* validation_context_certificate_provider_instance =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_validation_context_certificate_provider_instance(
            combined_validation_context);
    if (common_tls_context.certificate_validation_context
            .ca_certificate_provider_instance.Empty() &&
        validation_context_certificate_provider_instance != nullptr) {
      ValidationErrors::ScopedField field(
          errors, ".validation_context_certificate_provider_instance");
      common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance = CertificateProviderInstanceParse(
          context, validation_context_certificate_provider_instance, errors);
    }
  } else {
    auto* validation_context =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_validation_context(
            common_tls_context_proto);
    if (validation_context != nullptr) {
      ValidationErrors::ScopedField field(errors, ".validation_context");
      common_tls_context.certificate_validation_context =
          CertificateValidationContextParse(context, validation_context,
                                            errors);
    } else if (
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_validation_context_sds_secret_config(
            common_tls_context_proto)) {
      ValidationErrors::ScopedField field(
          errors, ".validation_context_sds_secret_config");
      errors->AddError("feature unsupported");
    }
  }
  auto* tls_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_provider_instance(
          common_tls_context_proto);
  if (tls_certificate_provider_instance != nullptr) {
    ValidationErrors::ScopedField field(errors,
                                        ".tls_certificate_provider_instance");
    common_tls_context.tls_certificate_provider_instance =
        CertificateProviderPluginInstanceParse(
            context, tls_certificate_provider_instance, errors);
  } else {
    // Fall back onto 'tls_certificate_certificate_provider_instance'. Note that
    // this way of fetching identity certificates is deprecated and will be
    // removed in the future.
    // TODO(yashykt): Remove this once it's no longer needed.
    auto* tls_certificate_certificate_provider_instance =
        envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_certificate_provider_instance(
            common_tls_context_proto);
    if (tls_certificate_certificate_provider_instance != nullptr) {
      ValidationErrors::ScopedField field(
          errors, ".tls_certificate_certificate_provider_instance");
      common_tls_context.tls_certificate_provider_instance =
          CertificateProviderInstanceParse(
              context, tls_certificate_certificate_provider_instance, errors);
    } else {
      if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_tls_certificates(
              common_tls_context_proto)) {
        ValidationErrors::ScopedField field(errors, ".tls_certificates");
        errors->AddError("feature unsupported");
      }
      if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_tls_certificate_sds_secret_configs(
              common_tls_context_proto)) {
        ValidationErrors::ScopedField field(
            errors, ".tls_certificate_sds_secret_configs");
        errors->AddError("feature unsupported");
      }
    }
  }
  if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_tls_params(
          common_tls_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".tls_params");
    errors->AddError("feature unsupported");
  }
  if (envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_custom_handshaker(
          common_tls_context_proto)) {
    ValidationErrors::ScopedField field(errors, ".custom_handshaker");
    errors->AddError("feature unsupported");
  }
  return common_tls_context;
}

//
// ExtractXdsExtension
//

namespace {

absl::StatusOr<Json> ParseProtobufStructToJson(
    const XdsResourceType::DecodeContext& context,
    const google_protobuf_Struct* resource) {
  upb::Status status;
  const auto* msg_def = google_protobuf_Struct_getmsgdef(context.symtab);
  size_t json_size = upb_JsonEncode(resource, msg_def, context.symtab, 0,
                                    nullptr, 0, status.ptr());
  if (json_size == static_cast<size_t>(-1)) {
    return absl::InvalidArgumentError(
        absl::StrCat("error encoding google::Protobuf::Struct as JSON: ",
                     upb_Status_ErrorMessage(status.ptr())));
  }
  void* buf = upb_Arena_Malloc(context.arena, json_size + 1);
  upb_JsonEncode(resource, msg_def, context.symtab, 0,
                 reinterpret_cast<char*>(buf), json_size + 1, status.ptr());
  auto json = JsonParse(reinterpret_cast<char*>(buf));
  if (!json.ok()) {
    // This should never happen.
    return absl::InternalError(
        absl::StrCat("error parsing JSON form of google::Protobuf::Struct "
                     "produced by upb library: ",
                     json.status().ToString()));
  }
  return std::move(*json);
}

}  // namespace

absl::optional<XdsExtension> ExtractXdsExtension(
    const XdsResourceType::DecodeContext& context,
    const google_protobuf_Any* any, ValidationErrors* errors) {
  if (any == nullptr) {
    errors->AddError("field not present");
    return absl::nullopt;
  }
  XdsExtension extension;
  auto strip_type_prefix = [&]() {
    ValidationErrors::ScopedField field(errors, ".type_url");
    if (extension.type.empty()) {
      errors->AddError("field not present");
      return false;
    }
    size_t pos = extension.type.rfind('/');
    if (pos == absl::string_view::npos || pos == extension.type.size() - 1) {
      errors->AddError(absl::StrCat("invalid value \"", extension.type, "\""));
    } else {
      extension.type = extension.type.substr(pos + 1);
    }
    return true;
  };
  extension.type = UpbStringToAbsl(google_protobuf_Any_type_url(any));
  if (!strip_type_prefix()) return absl::nullopt;
  extension.validation_fields.emplace_back(
      errors, absl::StrCat(".value[", extension.type, "]"));
  absl::string_view any_value = UpbStringToAbsl(google_protobuf_Any_value(any));
  if (extension.type == "xds.type.v3.TypedStruct" ||
      extension.type == "udpa.type.v1.TypedStruct") {
    const auto* typed_struct = xds_type_v3_TypedStruct_parse(
        any_value.data(), any_value.size(), context.arena);
    if (typed_struct == nullptr) {
      errors->AddError("could not parse");
      return absl::nullopt;
    }
    extension.type =
        UpbStringToAbsl(xds_type_v3_TypedStruct_type_url(typed_struct));
    if (!strip_type_prefix()) return absl::nullopt;
    extension.validation_fields.emplace_back(
        errors, absl::StrCat(".value[", extension.type, "]"));
    auto* protobuf_struct = xds_type_v3_TypedStruct_value(typed_struct);
    if (protobuf_struct == nullptr) {
      extension.value = Json::FromObject({});  // Default to empty object.
    } else {
      auto json = ParseProtobufStructToJson(context, protobuf_struct);
      if (!json.ok()) {
        errors->AddError(json.status().message());
        return absl::nullopt;
      }
      extension.value = std::move(*json);
    }
  } else {
    extension.value = any_value;
  }
  return std::move(extension);
}

}  // namespace grpc_core
