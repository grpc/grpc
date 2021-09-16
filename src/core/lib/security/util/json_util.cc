/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/util/json_util.h"

#include <string.h>

#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/error.h"

const char* grpc_json_get_string_property(const grpc_core::Json& json,
                                          const char* prop_name,
                                          grpc_error_handle* error) {
  if (json.type() != grpc_core::Json::Type::OBJECT) {
    if (error != nullptr) {
      *error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON value is not an object");
    }
    return nullptr;
  }
  auto it = json.object_value().find(prop_name);
  if (it == json.object_value().end()) {
    if (error != nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrCat("Property ", prop_name, " not found in JSON object."));
    }
    return nullptr;
  }
  if (it->second.type() != grpc_core::Json::Type::STRING) {
    if (error != nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
          "Property ", prop_name, " n JSON object is not a string."));
    }
    return nullptr;
  }
  return it->second.string_value().c_str();
}

bool grpc_copy_json_string_property(const grpc_core::Json& json,
                                    const char* prop_name,
                                    char** copied_value) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  const char* prop_value =
      grpc_json_get_string_property(json, prop_name, &error);
  GRPC_LOG_IF_ERROR("Could not copy JSON property", error);
  if (prop_value == nullptr) return false;
  *copied_value = gpr_strdup(prop_value);
  return true;
}
