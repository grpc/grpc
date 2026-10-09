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

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/config/common/mutation_rules/v3/mutation_rules.upb.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/v3/range.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/struct.upb.h"
#include "google/protobuf/struct.upbdefs.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "upb/base/status.hpp"
#include "upb/json/encode.h"
#include "upb/mem/arena.h"
#include "xds/type/matcher/v3/regex.upb.h"
#include "xds/type/v3/typed_struct.upb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

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
// ParseFractionalPercent()
//

uint32_t ParseFractionalPercent(
    const envoy_type_v3_FractionalPercent* fractional_percent) {
  if (fractional_percent == nullptr) return 1000000;
  uint32_t numerator =
      envoy_type_v3_FractionalPercent_numerator(fractional_percent);
  const auto denominator =
      static_cast<envoy_type_v3_FractionalPercent_DenominatorType>(
          envoy_type_v3_FractionalPercent_denominator(fractional_percent));
  switch (denominator) {
    case envoy_type_v3_FractionalPercent_MILLION:
      break;
    case envoy_type_v3_FractionalPercent_TEN_THOUSAND:
      numerator *= 100;
      break;
    case envoy_type_v3_FractionalPercent_HUNDRED:
    default:
      numerator *= 10000;
  }
  return std::min(numerator, 1000000u);
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

std::vector<StringMatcher> XdsListStringMatcherParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_type_matcher_v3_ListStringMatcher* list_matcher,
    ValidationErrors* errors) {
  if (list_matcher == nullptr) return {};
  std::vector<StringMatcher> matchers;
  size_t patterns_size = 0;
  const envoy_type_matcher_v3_StringMatcher* const* patterns =
      envoy_type_matcher_v3_ListStringMatcher_patterns(list_matcher,
                                                       &patterns_size);
  matchers.reserve(patterns_size);
  for (size_t i = 0; i < patterns_size; ++i) {
    ValidationErrors::ScopedField field(errors,
                                        absl::StrCat(".patterns[", i, "]"));
    matchers.push_back(StringMatcherParse(context, patterns[i], errors));
  }
  return matchers;
}

//
// ParseXdsHeaderMatcher()
//

HeaderMatcher ParseXdsHeaderMatcher(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_route_v3_HeaderMatcher* matcher,
    ValidationErrors* errors) {
  absl::string_view name =
      UpbStringToAbsl(envoy_config_route_v3_HeaderMatcher_name(matcher));
  const bool invert_match =
      envoy_config_route_v3_HeaderMatcher_invert_match(matcher);
  if (envoy_config_route_v3_HeaderMatcher_has_string_match(matcher)) {
    ValidationErrors::ScopedField field(errors, ".string_match");
    auto string_matcher = StringMatcherParse(
        context, envoy_config_route_v3_HeaderMatcher_string_match(matcher),
        errors);
    return HeaderMatcher::CreateFromStringMatcher(
        name, std::move(string_matcher), invert_match);
  }
  HeaderMatcher::Type type;
  absl::string_view match_string;
  int64_t range_start = 0;
  int64_t range_end = 0;
  bool present_match = false;
  bool case_sensitive = true;
  if (envoy_config_route_v3_HeaderMatcher_has_exact_match(matcher)) {
    type = HeaderMatcher::Type::kExact;
    match_string = UpbStringToAbsl(
        envoy_config_route_v3_HeaderMatcher_exact_match(matcher));
  } else if (envoy_config_route_v3_HeaderMatcher_has_prefix_match(matcher)) {
    type = HeaderMatcher::Type::kPrefix;
    match_string = UpbStringToAbsl(
        envoy_config_route_v3_HeaderMatcher_prefix_match(matcher));
  } else if (envoy_config_route_v3_HeaderMatcher_has_suffix_match(matcher)) {
    type = HeaderMatcher::Type::kSuffix;
    match_string = UpbStringToAbsl(
        envoy_config_route_v3_HeaderMatcher_suffix_match(matcher));
  } else if (envoy_config_route_v3_HeaderMatcher_has_contains_match(matcher)) {
    type = HeaderMatcher::Type::kContains;
    match_string = UpbStringToAbsl(
        envoy_config_route_v3_HeaderMatcher_contains_match(matcher));
  } else if (envoy_config_route_v3_HeaderMatcher_has_safe_regex_match(
                 matcher)) {
    const envoy_type_matcher_v3_RegexMatcher* regex_matcher =
        envoy_config_route_v3_HeaderMatcher_safe_regex_match(matcher);
    GRPC_CHECK_NE(regex_matcher, nullptr);
    type = HeaderMatcher::Type::kSafeRegex;
    match_string = UpbStringToAbsl(
        envoy_type_matcher_v3_RegexMatcher_regex(regex_matcher));
  } else if (envoy_config_route_v3_HeaderMatcher_has_range_match(matcher)) {
    type = HeaderMatcher::Type::kRange;
    const envoy_type_v3_Int64Range* range_matcher =
        envoy_config_route_v3_HeaderMatcher_range_match(matcher);
    GRPC_CHECK_NE(range_matcher, nullptr);
    range_start = envoy_type_v3_Int64Range_start(range_matcher);
    range_end = envoy_type_v3_Int64Range_end(range_matcher);
  } else if (envoy_config_route_v3_HeaderMatcher_has_present_match(matcher)) {
    type = HeaderMatcher::Type::kPresent;
    present_match = envoy_config_route_v3_HeaderMatcher_present_match(matcher);
  } else {
    errors->AddError("invalid header matcher");
    return HeaderMatcher();
  }
  absl::StatusOr<HeaderMatcher> header_matcher =
      HeaderMatcher::Create(name, type, match_string, range_start, range_end,
                            present_match, invert_match, case_sensitive);
  if (!header_matcher.ok()) {
    errors->AddError(absl::StrCat("cannot create header matcher: ",
                                  header_matcher.status().message()));
    return HeaderMatcher();
  }
  return std::move(*header_matcher);
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
// ParseXdsHeader()
//

namespace {

std::optional<std::string> GetHeaderValue(upb_StringView upb_value,
                                          bool is_binary,
                                          absl::string_view field_name,
                                          ValidationErrors* errors) {
  absl::string_view value = UpbStringToAbsl(upb_value);
  if (value.empty()) return std::nullopt;
  ValidationErrors::ScopedField field(errors, field_name);
  if (value.size() > 16384) errors->AddError("longer than 16384 bytes");
  if (is_binary) {
    std::string decoded_value;
    if (!absl::Base64Unescape(value, &decoded_value)) {
      errors->AddError("invalid base64");
    }
    return decoded_value;
  }
  ValidateMetadataResult result = ValidateNonBinaryHeaderValueIsLegal(value);
  if (result != ValidateMetadataResult::kOk) {
    errors->AddError(ValidateMetadataResultToString(result));
  }
  return std::string(value);
}

}  // namespace

std::pair<std::string, std::string> ParseXdsHeader(
    const envoy_config_core_v3_HeaderValue* header_value,
    ValidationErrors* errors) {
  // key
  absl::string_view key =
      UpbStringToAbsl(envoy_config_core_v3_HeaderValue_key(header_value));
  {
    ValidationErrors::ScopedField field(errors, ".key");
    if (key.size() > 16384) errors->AddError("longer than 16384 bytes");
    if (absl::StartsWith(key, ":") || absl::StartsWith(key, "grpc-") ||
        key == "host") {
      errors->AddError(absl::StrCat("header \"", key, "\" not allowed"));
    } else {
      ValidateMetadataResult result = ValidateHeaderKeyIsLegal(key);
      if (result != ValidateMetadataResult::kOk) {
        errors->AddError(ValidateMetadataResultToString(result));
      }
    }
  }
  // Per gRFC A102, when reading HeaderValue protos, we prioritize reading
  // the raw_value field for both binary and non-binary headers across xDS and
  // side-streams. If raw_value is unset, we fall back to using the value field
  // for backward compatibility.
  bool is_binary = absl::EndsWith(key, "-bin");
  std::optional<std::string> value =
      GetHeaderValue(envoy_config_core_v3_HeaderValue_raw_value(header_value),
                     is_binary, ".raw_value", errors);
  if (!value.has_value()) {
    value = GetHeaderValue(envoy_config_core_v3_HeaderValue_value(header_value),
                           is_binary, ".value", errors);
    if (!value.has_value()) {
      errors->AddError("either value or raw_value must be set");
    }
  }
  return {std::string(key), value.has_value() ? std::move(*value) : ""};
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
  header_mutation_rules_config.disallow_all = ParseBoolValue(
      envoy_config_common_mutation_rules_v3_HeaderMutationRules_disallow_all(
          header_mutation_rules));
  const google_protobuf_BoolValue* disallow_is_error_proto =
      envoy_config_common_mutation_rules_v3_HeaderMutationRules_disallow_is_error(
          header_mutation_rules);
  header_mutation_rules_config.disallow_is_error =
      ParseBoolValue(disallow_is_error_proto);
  const envoy_type_matcher_v3_RegexMatcher* disallow_expression_proto =
      envoy_config_common_mutation_rules_v3_HeaderMutationRules_disallow_expression(
          header_mutation_rules);
  if (disallow_expression_proto != nullptr) {
    ValidationErrors::ScopedField field(
        errors, ".header_mutation_rules.disallow_expression");
    header_mutation_rules_config.disallow_expression =
        ParseRegexMatcher(disallow_expression_proto, errors);
  }
  const envoy_type_matcher_v3_RegexMatcher* allow_expression_proto =
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

//
// ParseXdsHeaderValueOption()
//

namespace {

XdsHeaderValueOption::AppendAction ParseXdsHeaderValueOptionAppendAction(
    int32_t header_value_option_append_action, ValidationErrors* errors) {
  switch (header_value_option_append_action) {
    case envoy_config_core_v3_HeaderValueOption_APPEND_IF_EXISTS_OR_ADD:
      return XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd;
    case envoy_config_core_v3_HeaderValueOption_ADD_IF_ABSENT:
      return XdsHeaderValueOption::AppendAction::kAddIfAbsent;
    case envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS_OR_ADD:
      return XdsHeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd;
    case envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS:
      return XdsHeaderValueOption::AppendAction::kOverwriteIfExists;
    default:
      errors->AddError("unsupported append action");
      return XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd;
  }
}

}  // namespace

XdsHeaderValueOption ParseXdsHeaderValueOption(
    const envoy_config_core_v3_HeaderValueOption* header_value_option_config,
    ValidationErrors* errors) {
  if (header_value_option_config == nullptr) {
    errors->AddError("field is not present");
    return {};
  }
  XdsHeaderValueOption header_value_option;
  // parse header
  {
    ValidationErrors::ScopedField field(errors, ".header");
    if (const auto* header = envoy_config_core_v3_HeaderValueOption_header(
            header_value_option_config);
        header != nullptr) {
      header_value_option.header = ParseXdsHeader(header, errors);
    } else {
      errors->AddError("field not set");
    }
  }
  // parse header_append_action
  {
    ValidationErrors::ScopedField field(errors, ".append_action");
    int32_t header_append_action =
        envoy_config_core_v3_HeaderValueOption_append_action(
            header_value_option_config);
    header_value_option.append_action =
        ParseXdsHeaderValueOptionAppendAction(header_append_action, errors);
  }
  return header_value_option;
}

}  // namespace grpc_core
