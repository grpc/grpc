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

#include <sstream>

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
// Helper functions for extracting types from JSON
//
template <typename NumericType, typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     NumericType* output, ErrorVectorType* error_list) {
  static_assert(std::is_integral<NumericType>::value, "Integral required");
  if (json.type() != Json::Type::NUMBER) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:type should be NUMBER")
            .c_str()));
    return false;
  }
  std::istringstream ss(json.string_value());
  ss >> *output;
  // The JSON parsing API should have dealt with parsing errors, but check
  // anyway
  if (GPR_UNLIKELY(ss.bad())) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:failed to parse.").c_str()));
    return false;
  }
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     bool* output, ErrorVectorType* error_list) {
  switch (json.type()) {
    case Json::Type::JSON_TRUE:
      *output = true;
      return true;
    case Json::Type::JSON_FALSE:
      *output = false;
      return true;
    default:
      error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field:", field_name, " error:type should be BOOLEAN")
              .c_str()));
      return false;
  }
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     std::string* output, ErrorVectorType* error_list) {
  if (json.type() != Json::Type::STRING) {
    *output = "";
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:type should be STRING")
            .c_str()));
    return false;
  }
  *output = json.string_value();
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     const Json::Array** output, ErrorVectorType* error_list) {
  if (json.type() != Json::Type::ARRAY) {
    *output = nullptr;
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:type should be ARRAY")
            .c_str()));
    return false;
  }
  *output = &json.array_value();
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     const Json::Object** output, ErrorVectorType* error_list) {
  if (json.type() != Json::Type::OBJECT) {
    *output = nullptr;
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name, " error:type should be OBJECT")
            .c_str()));
    return false;
  }
  *output = &json.object_value();
  return true;
}

template <typename ErrorVectorType>
bool ExtractJsonType(const Json& json, const std::string& field_name,
                     grpc_millis* output, ErrorVectorType* error_list) {
  if (!ParseDurationFromJson(json, output)) {
    *output = GRPC_MILLIS_INF_PAST;
    error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("field:", field_name,
                     " error:type should be STRING of the form given by "
                     "google.proto.Duration.")
            .c_str()));
    return false;
  }
  return true;
}

template <typename T, typename ErrorVectorType>
bool ParseJsonObjectField(const Json::Object& object,
                          const std::string& field_name, T* output,
                          ErrorVectorType* error_list, bool optional = false) {
  auto it = object.find(field_name);
  if (it == object.end()) {
    if (!optional) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("field:", field_name, " error:does not exist.")
              .c_str()));
    }
    return false;
  }
  auto& child_object_json = it->second;
  return ExtractJsonType(child_object_json, field_name, output, error_list);
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_JSON_JSON_UTIL_H
