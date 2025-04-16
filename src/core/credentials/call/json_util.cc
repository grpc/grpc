//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/credentials/call/json_util.h"

#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include <map>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "src/core/lib/iomgr/error.h"

const char* grpc_json_get_string_property(const grpc_core::Json& json,
                                          const char* prop_name,
                                          grpc_error_handle* error) {
  if (json.type() != grpc_core::Json::Type::kObject) {
    if (error != nullptr) {
      *error = GRPC_ERROR_CREATE("JSON value is not an object");
    }
    return nullptr;
  }
  auto it = json.object().find(prop_name);
  if (it == json.object().end()) {
    if (error != nullptr) {
      *error = GRPC_ERROR_CREATE(
          absl::StrCat("Property ", prop_name, " not found in JSON object."));
    }
    return nullptr;
  }
  if (it->second.type() != grpc_core::Json::Type::kString) {
    if (error != nullptr) {
      *error = GRPC_ERROR_CREATE(absl::StrCat(
          "Property ", prop_name, " n JSON object is not a string."));
    }
    return nullptr;
  }
  return it->second.string().c_str();
}

bool grpc_copy_json_string_property(const grpc_core::Json& json,
                                    const char* prop_name,
                                    char** copied_value) {
  grpc_error_handle error;
  const char* prop_value =
      grpc_json_get_string_property(json, prop_name, &error);
  GRPC_LOG_IF_ERROR("Could not copy JSON property", error);
  if (prop_value == nullptr) return false;
  *copied_value = gpr_strdup(prop_value);
  return true;
}
