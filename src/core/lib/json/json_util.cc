//
//
// Copyright 2020 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/json/json_util.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"

namespace grpc_core {

bool ParseDurationFromJson(const Json& field, grpc_millis* duration) {
  if (field.type() != Json::Type::STRING) return false;
  size_t len = field.string_value().size();
  if (field.string_value()[len - 1] != 's') return false;
  grpc_core::UniquePtr<char> buf(gpr_strdup(field.string_value().c_str()));
  *(buf.get() + len - 1) = '\0';  // Remove trailing 's'.
  char* decimal_point = strchr(buf.get(), '.');
  int nanos = 0;
  if (decimal_point != nullptr) {
    *decimal_point = '\0';
    nanos = gpr_parse_nonnegative_int(decimal_point + 1);
    if (nanos == -1) {
      return false;
    }
    int num_digits = static_cast<int>(strlen(decimal_point + 1));
    if (num_digits > 9) {  // We don't accept greater precision than nanos.
      return false;
    }
    for (int i = 0; i < (9 - num_digits); ++i) {
      nanos *= 10;
    }
  }
  int seconds =
      decimal_point == buf.get() ? 0 : gpr_parse_nonnegative_int(buf.get());
  if (seconds == -1) return false;
  *duration = seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
  return true;
}

bool ExtractJsonBool(const Json& json, absl::string_view field_name,
                     bool* output, std::vector<grpc_error_handle>* error_list) {
  switch (json.type()) {
    case Json::Type::JSON_TRUE:
      *output = true;
      return true;
    case Json::Type::JSON_FALSE:
      *output = false;
      return true;
    default:
      AddFieldError(field_name, "type should be BOOLEAN", error_list);
      return false;
  }
}

bool ExtractJsonArray(const Json& json, absl::string_view field_name,
                      const Json::Array** output,
                      std::vector<grpc_error_handle>* error_list) {
  if (json.type() != Json::Type::ARRAY) {
    *output = nullptr;
    AddFieldError(field_name, "type should be ARRAY", error_list);
    error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("field:", field_name, " error:type should be ARRAY")));
    return false;
  }
  *output = &json.array_value();
  return true;
}

bool ExtractJsonObject(const Json& json, absl::string_view field_name,
                       const Json::Object** output,
                       std::vector<grpc_error_handle>* error_list) {
  if (json.type() != Json::Type::OBJECT) {
    *output = nullptr;
    AddFieldError(field_name, "type should be OBJECT", error_list);
    return false;
  }
  *output = &json.object_value();
  return true;
}

bool ParseJsonObjectFieldAsDuration(const Json::Object& object,
                                    absl::string_view field_name,
                                    grpc_millis* output,
                                    std::vector<grpc_error_handle>* error_list,
                                    bool required) {
  const Json* child_object_json =
      GetJsonField(object, field_name, error_list, required);
  if (child_object_json == nullptr) return false;
  if (!ParseDurationFromJson(*child_object_json, output)) {
    *output = GRPC_MILLIS_INF_PAST;
    AddFieldError(
        field_name,
        "type should be STRING of the form given by google.proto.Duration.",
        error_list);
    return false;
  }
  return true;
}

const Json* GetJsonField(const Json::Object& object,
                         absl::string_view field_name,
                         std::vector<grpc_error_handle>* error_list,
                         bool required) {
  auto it = object.find(std::string(field_name));
  if (it == object.end()) {
    if (required) AddFieldError(field_name, "does not exist.", error_list);
    return nullptr;
  }
  return &it->second;
}

}  // namespace grpc_core
