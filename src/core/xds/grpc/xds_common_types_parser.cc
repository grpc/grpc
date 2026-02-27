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

#include "src/core/xds/grpc/xds_common_types_parser.h"

#include <grpc/support/json.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <map>
#include <utility>

#include "envoy/config/core/v3/address.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/struct.upbdefs.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/env.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "upb/base/status.hpp"
#include "upb/json/encode.h"
#include "upb/mem/arena.h"
#include "xds/type/matcher/v3/regex.upb.h"
#include "xds/type/v3/typed_struct.upb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

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
// ParseXdsAddress()
//

std::optional<grpc_resolved_address> ParseXdsAddress(
    const envoy_config_core_v3_Address* address, ValidationErrors* errors) {
  if (address == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  ValidationErrors::ScopedField field(errors, ".socket_address");
  const envoy_config_core_v3_SocketAddress* socket_address =
      envoy_config_core_v3_Address_socket_address(address);
  if (socket_address == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  std::string address_str = UpbStringToStdString(
      envoy_config_core_v3_SocketAddress_address(socket_address));
  uint32_t port;
  {
    ValidationErrors::ScopedField field(errors, ".port_value");
    port = envoy_config_core_v3_SocketAddress_port_value(socket_address);
    if (GPR_UNLIKELY(port >> 16) != 0) {
      errors->AddError("invalid port");
      return std::nullopt;
    }
  }
  auto addr = StringToSockaddr(address_str, port);
  if (!addr.ok()) {
    errors->AddError(addr.status().message());
    return std::nullopt;
  }
  return *addr;
}

//
// StringMatcherParse()
//

namespace {

class StringMatcherProtoAccessor {
 public:
  virtual ~StringMatcherProtoAccessor() = default;

  virtual bool IsPresent() const = 0;
  virtual bool HasExact() const = 0;
  virtual upb_StringView GetExact() const = 0;
  virtual bool HasPrefix() const = 0;
  virtual upb_StringView GetPrefix() const = 0;
  virtual bool HasSuffix() const = 0;
  virtual upb_StringView GetSuffix() const = 0;
  virtual bool HasContains() const = 0;
  virtual upb_StringView GetContains() const = 0;
  virtual bool HasSafeRegex() const = 0;
  virtual upb_StringView GetSafeRegex() const = 0;
  virtual bool IgnoreCase() const = 0;
};

#define GRPC_STRING_MATCHER_PROTO_ACCESSOR_CLASS(prefix)                    \
  class ProtoAccessor final : public StringMatcherProtoAccessor {           \
   public:                                                                  \
    explicit ProtoAccessor(                                                 \
        const prefix##_type_matcher_v3_StringMatcher* proto)                \
        : proto_(proto) {}                                                  \
                                                                            \
    bool IsPresent() const override { return proto_ != nullptr; }           \
    bool HasExact() const override {                                        \
      return prefix##_type_matcher_v3_StringMatcher_has_exact(proto_);      \
    }                                                                       \
    upb_StringView GetExact() const override {                              \
      return prefix##_type_matcher_v3_StringMatcher_exact(proto_);          \
    }                                                                       \
    bool HasPrefix() const override {                                       \
      return prefix##_type_matcher_v3_StringMatcher_has_prefix(proto_);     \
    }                                                                       \
    upb_StringView GetPrefix() const override {                             \
      return prefix##_type_matcher_v3_StringMatcher_prefix(proto_);         \
    }                                                                       \
    bool HasSuffix() const override {                                       \
      return prefix##_type_matcher_v3_StringMatcher_has_suffix(proto_);     \
    }                                                                       \
    upb_StringView GetSuffix() const override {                             \
      return prefix##_type_matcher_v3_StringMatcher_suffix(proto_);         \
    }                                                                       \
    bool HasContains() const override {                                     \
      return prefix##_type_matcher_v3_StringMatcher_has_contains(proto_);   \
    }                                                                       \
    upb_StringView GetContains() const override {                           \
      return prefix##_type_matcher_v3_StringMatcher_contains(proto_);       \
    }                                                                       \
    bool HasSafeRegex() const override {                                    \
      return prefix##_type_matcher_v3_StringMatcher_has_safe_regex(proto_); \
    }                                                                       \
    upb_StringView GetSafeRegex() const override {                          \
      auto* regex_matcher =                                                 \
          prefix##_type_matcher_v3_StringMatcher_safe_regex(proto_);        \
      return prefix##_type_matcher_v3_RegexMatcher_regex(regex_matcher);    \
    }                                                                       \
    bool IgnoreCase() const override {                                      \
      return prefix##_type_matcher_v3_StringMatcher_ignore_case(proto_);    \
    }                                                                       \
                                                                            \
   private:                                                                 \
    const prefix##_type_matcher_v3_StringMatcher* proto_;                   \
  };

StringMatcher StringMatcherParseInternal(
    const StringMatcherProtoAccessor& proto, ValidationErrors* errors) {
  if (!proto.IsPresent()) {
    errors->AddError("field not present");
    return StringMatcher();
  }
  StringMatcher::Type type;
  std::string matcher;
  if (proto.HasExact()) {
    type = StringMatcher::Type::kExact;
    matcher = UpbStringToStdString(proto.GetExact());
  } else if (proto.HasPrefix()) {
    type = StringMatcher::Type::kPrefix;
    matcher = UpbStringToStdString(proto.GetPrefix());
  } else if (proto.HasSuffix()) {
    type = StringMatcher::Type::kSuffix;
    matcher = UpbStringToStdString(proto.GetSuffix());
  } else if (proto.HasContains()) {
    type = StringMatcher::Type::kContains;
    matcher = UpbStringToStdString(proto.GetContains());
  } else if (proto.HasSafeRegex()) {
    type = StringMatcher::Type::kSafeRegex;
    matcher = UpbStringToStdString(proto.GetSafeRegex());
  } else {
    errors->AddError("invalid string matcher");
    return StringMatcher();
  }
  const bool ignore_case = proto.IgnoreCase();
  absl::StatusOr<StringMatcher> string_matcher =
      StringMatcher::Create(type, matcher,
                            /*case_sensitive=*/!ignore_case);
  if (!string_matcher.ok()) {
    errors->AddError(string_matcher.status().message());
    return StringMatcher();
  }
  if (type == StringMatcher::Type::kSafeRegex && ignore_case) {
    ValidationErrors::ScopedField field(errors, ".ignore_case");
    errors->AddError("not supported for regex matcher");
  }
  return std::move(*string_matcher);
}

}  // namespace

StringMatcher StringMatcherParse(
    const XdsResourceType::DecodeContext& /*context*/,
    const envoy_type_matcher_v3_StringMatcher* matcher_proto,
    ValidationErrors* errors) {
  GRPC_STRING_MATCHER_PROTO_ACCESSOR_CLASS(envoy);
  ProtoAccessor proto_accessor(matcher_proto);
  return StringMatcherParseInternal(proto_accessor, errors);
}

StringMatcher StringMatcherParse(
    const XdsResourceType::DecodeContext& /*context*/,
    const xds_type_matcher_v3_StringMatcher* matcher_proto,
    ValidationErrors* errors) {
  GRPC_STRING_MATCHER_PROTO_ACCESSOR_CLASS(xds);
  ProtoAccessor proto_accessor(matcher_proto);
  return StringMatcherParseInternal(proto_accessor, errors);
}

//
// CommonTlsContextParse()
//

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
      DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap());
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
      DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap());
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
    auto string_matcher =
        StringMatcherParse(context, subject_alt_names_matchers[i], errors);
    certificate_validation_context.match_subject_alt_names.push_back(
        std::move(string_matcher));
  }
  auto* ca_certificate_provider_instance =
      envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_ca_certificate_provider_instance(
          certificate_validation_context_proto);
  if (ca_certificate_provider_instance != nullptr) {
    ValidationErrors::ScopedField field(errors,
                                        ".ca_certificate_provider_instance");
    certificate_validation_context.ca_certs =
        CertificateProviderPluginInstanceParse(
            context, ca_certificate_provider_instance, errors);
  } else {
    auto* system_root_certs =
        envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_system_root_certs(
            certificate_validation_context_proto);
    if (system_root_certs != nullptr) {
      certificate_validation_context.ca_certs =
          CommonTlsContext::CertificateValidationContext::SystemRootCerts();
    }
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
  if (ParseBoolValue(
          envoy_extensions_transport_sockets_tls_v3_CertificateValidationContext_require_signed_certificate_timestamp(
              certificate_validation_context_proto))) {
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

CommonTlsContext CommonTlsContextParse(
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
    // common_tls_context->certificate_validation_context.ca_certs does not
    // contain a cert provider, fall back onto
    // 'validation_context_certificate_provider_instance' inside
    // 'combined_validation_context'. Note that this way of fetching root
    // certificates is deprecated and will be removed in the future.
    // TODO(yashykt): Remove this once it's no longer needed.
    if (!std::holds_alternative<
            CommonTlsContext::CertificateProviderPluginInstance>(
            common_tls_context.certificate_validation_context.ca_certs)) {
      const auto* validation_context_certificate_provider_instance =
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CombinedCertificateValidationContext_validation_context_certificate_provider_instance(
              combined_validation_context);
      if (validation_context_certificate_provider_instance != nullptr) {
        ValidationErrors::ScopedField field(
            errors, ".validation_context_certificate_provider_instance");
        common_tls_context.certificate_validation_context.ca_certs =
            CertificateProviderInstanceParse(
                context, validation_context_certificate_provider_instance,
                errors);
      }
    }
  } else if (
      auto* validation_context =
          envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_validation_context(
              common_tls_context_proto);
      validation_context != nullptr) {
    ValidationErrors::ScopedField field(errors, ".validation_context");
    common_tls_context.certificate_validation_context =
        CertificateValidationContextParse(context, validation_context, errors);
  } else if (
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_has_validation_context_sds_secret_config(
          common_tls_context_proto)) {
    ValidationErrors::ScopedField field(
        errors, ".validation_context_sds_secret_config");
    errors->AddError("feature unsupported");
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
      size_t size;
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificates(
          common_tls_context_proto, &size);
      if (size != 0) {
        ValidationErrors::ScopedField field(errors, ".tls_certificates");
        errors->AddError("feature unsupported");
      }
      envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_tls_certificate_sds_secret_configs(
          common_tls_context_proto, &size);
      if (size != 0) {
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
// ParseProtobufStructToJson()
//

absl::StatusOr<Json> ParseProtobufStructToJson(
    const XdsResourceType::DecodeContext& context,
    const google_protobuf_Struct* resource) {
  upb::Status status;
  const auto* msg_def = google_protobuf_Struct_getmsgdef(context.symtab);
  size_t json_size =
      upb_JsonEncode(reinterpret_cast<const upb_Message*>(resource), msg_def,
                     context.symtab, 0, nullptr, 0, status.ptr());
  if (json_size == static_cast<size_t>(-1)) {
    return absl::InvalidArgumentError(
        absl::StrCat("error encoding google::Protobuf::Struct as JSON: ",
                     upb_Status_ErrorMessage(status.ptr())));
  }
  void* buf = upb_Arena_Malloc(context.arena, json_size + 1);
  upb_JsonEncode(reinterpret_cast<const upb_Message*>(resource), msg_def,
                 context.symtab, 0, reinterpret_cast<char*>(buf), json_size + 1,
                 status.ptr());
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

//
// ExtractXdsExtension()
//

namespace {

bool StripTypePrefix(absl::string_view& type, ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".type_url");
  if (type.empty()) {
    errors->AddError("field not present");
    return false;
  }
  size_t pos = type.rfind('/');
  if (pos == absl::string_view::npos || pos == type.size() - 1) {
    errors->AddError(absl::StrCat("invalid value \"", type, "\""));
  } else {
    type = type.substr(pos + 1);
  }
  return true;
}

}  // namespace

std::optional<XdsExtension> ExtractXdsExtension(
    const XdsResourceType::DecodeContext& context,
    const google_protobuf_Any* any, ValidationErrors* errors) {
  if (any == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  XdsExtension extension;
  extension.type = UpbStringToAbsl(google_protobuf_Any_type_url(any));
  if (!StripTypePrefix(extension.type, errors)) return std::nullopt;
  extension.validation_fields.emplace_back(
      errors, absl::StrCat(".value[", extension.type, "]"));
  absl::string_view any_value = UpbStringToAbsl(google_protobuf_Any_value(any));
  if (extension.type == "xds.type.v3.TypedStruct" ||
      extension.type == "udpa.type.v1.TypedStruct") {
    const auto* typed_struct = xds_type_v3_TypedStruct_parse(
        any_value.data(), any_value.size(), context.arena);
    if (typed_struct == nullptr) {
      errors->AddError("could not parse");
      return std::nullopt;
    }
    extension.type =
        UpbStringToAbsl(xds_type_v3_TypedStruct_type_url(typed_struct));
    if (!StripTypePrefix(extension.type, errors)) return std::nullopt;
    extension.validation_fields.emplace_back(
        errors, absl::StrCat(".value[", extension.type, "]"));
    auto* protobuf_struct = xds_type_v3_TypedStruct_value(typed_struct);
    if (protobuf_struct == nullptr) {
      extension.value = Json::FromObject({});  // Default to empty object.
    } else {
      auto json = ParseProtobufStructToJson(context, protobuf_struct);
      if (!json.ok()) {
        errors->AddError(json.status().message());
        return std::nullopt;
      }
      extension.value = std::move(*json);
    }
  } else {
    extension.value = any_value;
  }
  return std::move(extension);
}

//
// ParseXdsGrpcService()
//

namespace {

absl::string_view GetHeaderValue(upb_StringView upb_value,
                                 absl::string_view field_name, bool validate,
                                 ValidationErrors* errors) {
  absl::string_view value = UpbStringToAbsl(upb_value);
  if (!value.empty()) {
    ValidationErrors::ScopedField field(errors, field_name);
    if (value.size() > 16384) errors->AddError("longer than 16384 bytes");
    if (validate) {
      ValidateMetadataResult result =
          ValidateNonBinaryHeaderValueIsLegal(value);
      if (result != ValidateMetadataResult::kOk) {
        errors->AddError(ValidateMetadataResultToString(result));
      }
    }
  }
  return value;
}

std::pair<std::string, std::string> ParseHeader(
    const envoy_config_core_v3_HeaderValue* header_value,
    ValidationErrors* errors) {
  // key
  absl::string_view key =
      UpbStringToAbsl(envoy_config_core_v3_HeaderValue_key(header_value));
  {
    ValidationErrors::ScopedField field(errors, ".key");
    if (key.size() > 16384) errors->AddError("longer than 16384 bytes");
    ValidateMetadataResult result = ValidateHeaderKeyIsLegal(key);
    if (result != ValidateMetadataResult::kOk) {
      errors->AddError(ValidateMetadataResultToString(result));
    }
  }
  // value or raw_value
  absl::string_view value;
  if (absl::EndsWith(key, "-bin")) {
    value =
        GetHeaderValue(envoy_config_core_v3_HeaderValue_raw_value(header_value),
                       ".raw_value", /*validate=*/false, errors);
    if (value.empty()) {
      value =
          GetHeaderValue(envoy_config_core_v3_HeaderValue_value(header_value),
                         ".value", /*validate=*/true, errors);
      if (value.empty()) {
        errors->AddError("either value or raw_value must be set");
      }
    }
  } else {
    // Key does not end in "-bin".
    value = GetHeaderValue(envoy_config_core_v3_HeaderValue_value(header_value),
                           ".value", /*validate=*/true, errors);
    if (value.empty()) {
      ValidationErrors::ScopedField field(errors, ".value");
      errors->AddError("field not set");
    }
  }
  return {std::string(key), std::string(value)};
}

}  // namespace

XdsGrpcService ParseXdsGrpcService(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_GrpcService* grpc_service,
    ValidationErrors* errors) {
  if (grpc_service == nullptr) {
    errors->AddError("field not set");
    return {};
  }
  XdsGrpcService xds_grpc_service;
  // timeout
  if (auto* timeout = envoy_config_core_v3_GrpcService_timeout(grpc_service);
      timeout != nullptr) {
    ValidationErrors::ScopedField field(errors, ".timeout");
    xds_grpc_service.timeout = ParseDuration(timeout, errors);
    if (xds_grpc_service.timeout <= Duration::Zero()) {
      errors->AddError("duration must be positive");
    }
  }
  // initial_metadata
  size_t initial_metadata_size;
  auto* initial_metadata = envoy_config_core_v3_GrpcService_initial_metadata(
      grpc_service, &initial_metadata_size);
  for (size_t i = 0; i < initial_metadata_size; ++i) {
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".initial_metadata[", i, "]"));
    xds_grpc_service.initial_metadata.push_back(
        ParseHeader(initial_metadata[i], errors));
  }
  // google_grpc
  ValidationErrors::ScopedField field(errors, ".google_grpc");
  auto* google_grpc =
      envoy_config_core_v3_GrpcService_google_grpc(grpc_service);
  if (google_grpc == nullptr) {
    errors->AddError("field not set");
  } else {
    // target_uri
    std::string target_uri = UpbStringToStdString(
        envoy_config_core_v3_GrpcService_GoogleGrpc_target_uri(google_grpc));
    if (!CoreConfiguration::Get().resolver_registry().IsValidTarget(
            target_uri)) {
      ValidationErrors::ScopedField field(errors, ".target_uri");
      errors->AddError("invalid target URI");
    }
    // credentials
    RefCountedPtr<const ChannelCredsConfig> channel_creds_config;
    std::vector<RefCountedPtr<const CallCredsConfig>> call_creds_configs;
    if (DownCast<const GrpcXdsServer&>(context.server).TrustedXdsServer()) {
      // Trusted xDS server.  Use credentials from the GoogleGrpc proto.
      // First, look at channel creds.
      {
        ValidationErrors::ScopedField field(errors,
                                            ".channel_credentials_plugin");
        size_t size;
        const auto* const* channel_creds_plugin =
            envoy_config_core_v3_GrpcService_GoogleGrpc_channel_credentials_plugin(
                google_grpc, &size);
        if (size == 0) {
          errors->AddError("field not set");
        } else {
          const auto& registry =
              CoreConfiguration::Get().channel_creds_registry();
          const auto& certificate_providers =
              DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap())
                  .certificate_providers();
          for (size_t i = 0; i < size; ++i) {
            ValidationErrors::ScopedField field(errors,
                                                absl::StrCat("[", i, "]"));
            absl::string_view type = UpbStringToAbsl(
                google_protobuf_Any_type_url(channel_creds_plugin[i]));
            if (!StripTypePrefix(type, errors)) continue;
            if (!registry.IsProtoSupported(type)) continue;
            ValidationErrors::ScopedField field2(errors, ".value");
            absl::string_view serialized_config = UpbStringToAbsl(
                google_protobuf_Any_value(channel_creds_plugin[i]));
            channel_creds_config = registry.ParseProto(
                type, serialized_config, certificate_providers, errors);
            break;
          }
          if (channel_creds_config == nullptr) {
            errors->AddError("no supported channel credentials type found");
          }
        }
      }
      // Now look at call creds.
      {
        ValidationErrors::ScopedField field(errors, ".call_credentials_plugin");
        size_t size;
        const auto* const* call_creds_plugin =
            envoy_config_core_v3_GrpcService_GoogleGrpc_call_credentials_plugin(
                google_grpc, &size);
        const auto& registry = CoreConfiguration::Get().call_creds_registry();
        for (size_t i = 0; i < size; ++i) {
          ValidationErrors::ScopedField field(errors,
                                              absl::StrCat("[", i, "]"));
          absl::string_view type = UpbStringToAbsl(
              google_protobuf_Any_type_url(call_creds_plugin[i]));
          if (!StripTypePrefix(type, errors)) continue;
          if (!registry.IsProtoSupported(type)) continue;
          ValidationErrors::ScopedField field2(errors, ".value");
          absl::string_view serialized_config =
              UpbStringToAbsl(google_protobuf_Any_value(call_creds_plugin[i]));
          call_creds_configs.push_back(
              registry.ParseProto(type, serialized_config, errors));
        }
      }
    } else {
      // Not a trusted xDS server.  Do lookup in bootstrap.
      const auto& bootstrap =
          DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap());
      auto& allowed_grpc_services = bootstrap.allowed_grpc_services();
      auto it = allowed_grpc_services.find(target_uri);
      if (it == allowed_grpc_services.end()) {
        ValidationErrors::ScopedField field(errors, ".target_uri");
        errors->AddError(
            "service not present in \"allowed_grpc_services\" "
            "in bootstrap config");
      } else {
        channel_creds_config = it->second.channel_creds_config;
        call_creds_configs = it->second.call_creds_configs;
      }
    }
    xds_grpc_service.server_target = std::make_unique<GrpcXdsServerTarget>(
        target_uri, std::move(channel_creds_config),
        std::move(call_creds_configs));
  }
  return xds_grpc_service;
}

//
// ParseHeaderMutationRules()
//
namespace {

std::unique_ptr<RE2> ParseRegexMatcher(
    const envoy_type_matcher_v3_RegexMatcher* regex_matcher,
    ValidationErrors* errors) {
  auto matcher = UpbStringToStdString(
      envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
  auto regex = std::make_unique<RE2>(matcher);
  if (!regex->ok()) {
    errors->AddError(absl::StrCat("Invalid regex string specified in matcher: ",
                                  regex->error()));
    return nullptr;
  }
  return regex;
}

}  // namespace

HeaderMutationRules ParseHeaderMutationRules(
    const envoy_config_common_mutation_rules_v3_HeaderMutationRules*
        header_mutation_rules,
    ValidationErrors* errors) {
  if (header_mutation_rules == nullptr) {
    errors->AddError("field is not present");
    return {};
  }
  HeaderMutationRules header_mutation_rules_config;
  header_mutation_rules_config.disallow_all =
      envoy_config_common_mutation_rules_v3_HeaderMutationRules_disallow_all(
          header_mutation_rules);
  header_mutation_rules_config.disallow_is_error =
      envoy_config_common_mutation_rules_v3_HeaderMutationRules_disallow_is_error(
          header_mutation_rules);
  const auto* disallow_expression_proto =
      envoy_config_common_mutation_rules_v3_HeaderMutationRules_disallow_expression(
          header_mutation_rules);
  if (disallow_expression_proto != nullptr) {
    ValidationErrors::ScopedField field(
        errors, ".header_mutation_rules.disallow_expression");
    header_mutation_rules_config.disallow_expression =
        ParseRegexMatcher(disallow_expression_proto, errors);
  }
  const auto* allow_expression_proto =
      envoy_config_common_mutation_rules_v3_HeaderMutationRules_allow_expression(
          header_mutation_rules);
  if (allow_expression_proto != nullptr) {
    ValidationErrors::ScopedField field(
        errors, ".header_mutation_rules.allow_expression");
    header_mutation_rules_config.allow_expression =
        ParseRegexMatcher(allow_expression_proto, errors);
  }
  return header_mutation_rules_config;
}

}  // namespace grpc_core
