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

#include "src/core/xds/grpc/xds_common_types.h"

#include <grpc/support/json.h>
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <utility>

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "upb/mem/arena.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/util/upb_utils.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

HeaderValueOption::AppendAction
UpbHeaderAppendActionToHeaderValueOptionAppendAction(
    int32_t header_value_option_append_action) {
  switch (header_value_option_append_action) {
    case envoy_config_core_v3_HeaderValueOption_APPEND_IF_EXISTS_OR_ADD:
      return HeaderValueOption::AppendAction::kAppendIfExistsOrAdd;

    case envoy_config_core_v3_HeaderValueOption_ADD_IF_ABSENT:
      return HeaderValueOption::AppendAction::kAddIfAbsent;

    case envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS_OR_ADD:
      return HeaderValueOption::AppendAction::kOverwriteIfExistsOrAdd;

    case envoy_config_core_v3_HeaderValueOption_OVERWRITE_IF_EXISTS:
      return HeaderValueOption::AppendAction::kOverwriteIfExists;

    default:
      return HeaderValueOption::AppendAction::kDefault;
  }
}

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

HeaderValueOption ParseHeaderValueOption(
    const envoy_config_core_v3_HeaderValueOption* header_value_option_config,
    ValidationErrors* errors) {
  if (header_value_option_config == nullptr) {
    errors->AddError("field not set");
    return {};
  }
  HeaderValueOption header_value_option;
  // parse header
  if (auto* header = envoy_config_core_v3_HeaderValueOption_header(
          header_value_option_config);
      header != nullptr) {
    auto [key, value] = ParseHeader(header, errors);
    header_value_option.header = {
        std::move(key),
        std::move(value),
    };
  }
  // parse header_append_action
  int32_t header_append_action =
      envoy_config_core_v3_HeaderValueOption_append_action(
          header_value_option_config);
  header_value_option.append_action =
      UpbHeaderAppendActionToHeaderValueOptionAppendAction(
          header_append_action);
  // parse keep_empty_value
  auto keep_empty_value =
      envoy_config_core_v3_HeaderValueOption_keep_empty_value(
          header_value_option_config);
  header_value_option.keep_empty_value = keep_empty_value;

  return header_value_option;
}

}  // namespace grpc_core