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

#ifndef GRPC_CORE_LIB_JSON_JSON_UTIL_H
#define GRPC_CORE_LIB_JSON_JSON_UTIL_H

#include <grpc/support/port_platform.h>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

// Parses a JSON field of the form generated for a google.proto.Duration
// proto message, as per:
//   https://developers.google.com/protocol-buffers/docs/proto3#json
// Returns true on success, false otherwise.
bool ParseDurationFromJson(const Json& field, grpc_millis* duration);

//
// Helper functions for extracting types from JSON.
// Return true on success, false otherwise. If an error is encountered during
// parsing, a descriptive error is appended to \a error_list.
//
template <typename NumericType>
bool ExtractJsonNumber(const Json& json, const std::string& field_name,
                       NumericType* output,
                       std::vector<grpc_error_handle>* error_list) {
  static_assert(std::is_integral<NumericType>::value, "Integral required");
  if (json.type() != Json::Type::NUMBER) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("field:", field_name, " error:type should be NUMBER")));
    return false;
  }
  if (!absl::SimpleAtoi(json.string_value(), output)) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("field:", field_name, " error:failed to parse.")));
    return false;
  }
  return true;
}

bool ExtractJsonBool(const Json& json, const std::string& field_name,
                     bool* output, std::vector<grpc_error_handle>* error_list);

// OutputType can be std::string or absl::string_view.
template <typename OutputType>
bool ExtractJsonString(const Json& json, const std::string& field_name,
                       OutputType* output,
                       std::vector<grpc_error_handle>* error_list) {
  if (json.type() != Json::Type::STRING) {
    *output = "";
    error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("field:", field_name, " error:type should be STRING")));
    return false;
  }
  *output = json.string_value();
  return true;
}

bool ExtractJsonArray(const Json& json, const std::string& field_name,
                      const Json::Array** output,
                      std::vector<grpc_error_handle>* error_list);

bool ExtractJsonObject(const Json& json, const std::string& field_name,
                       const Json::Object** output,
                       std::vector<grpc_error_handle>* error_list);

template <typename NumericType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     NumericType* output,
                     std::vector<grpc_error_handle>* error_list) {
  return ExtractJsonNumber(json, field_name, output, error_list);
}

bool ExtractJsonType(const Json& json, const std::string& field_name,
                     bool* output, std::vector<grpc_error_handle>* error_list);

bool ExtractJsonType(const Json& json, const std::string& field_name,
                     std::string* output,
                     std::vector<grpc_error_handle>* error_list);

bool ExtractJsonType(const Json& json, const std::string& field_name,
                     absl::string_view* output,
                     std::vector<grpc_error_handle>* error_list);

bool ExtractJsonType(const Json& json, const std::string& field_name,
                     const Json::Array** output,
                     std::vector<grpc_error_handle>* error_list);

bool ExtractJsonType(const Json& json, const std::string& field_name,
                     const Json::Object** output,
                     std::vector<grpc_error_handle>* error_list);

template <typename T>
bool ParseJsonObjectField(const Json::Object& object,
                          const std::string& field_name, T* output,
                          std::vector<grpc_error_handle>* error_list,
                          bool required = true) {
  auto it = object.find(field_name);
  if (it == object.end()) {
    if (required) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrCat("field:", field_name, " error:does not exist.")));
    }
    return false;
  }
  auto& child_object_json = it->second;
  return ExtractJsonType(child_object_json, field_name, output, error_list);
}

bool ParseJsonObjectFieldAsDuration(const Json::Object& object,
                                    const std::string& field_name,
                                    grpc_millis* output,
                                    std::vector<grpc_error_handle>* error_list,
                                    bool required = true);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_JSON_JSON_UTIL_H
