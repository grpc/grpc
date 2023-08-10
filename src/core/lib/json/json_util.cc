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

#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"

namespace grpc_core {

bool ParseDurationFromJson(const Json& field, Duration* duration) {
  ValidationErrors errors;
  static_cast<json_detail::LoaderInterface*>(
      NoDestructSingleton<json_detail::AutoLoader<Duration>>::Get())
      ->LoadInto(field, JsonArgs(), duration, &errors);
  return errors.ok();
}

bool ExtractJsonBool(const Json& json, absl::string_view field_name,
                     bool* output, std::vector<grpc_error_handle>* error_list) {
  if (json.type() != Json::Type::kBoolean) {
    error_list->push_back(GRPC_ERROR_CREATE(
        absl::StrCat("field:", field_name, " error:type should be BOOLEAN")));
    return false;
  }
  *output = json.boolean();
  return true;
}

bool ExtractJsonArray(const Json& json, absl::string_view field_name,
                      const Json::Array** output,
                      std::vector<grpc_error_handle>* error_list) {
  if (json.type() != Json::Type::kArray) {
    *output = nullptr;
    error_list->push_back(GRPC_ERROR_CREATE(
        absl::StrCat("field:", field_name, " error:type should be ARRAY")));
    return false;
  }
  *output = &json.array();
  return true;
}

bool ExtractJsonObject(const Json& json, absl::string_view field_name,
                       const Json::Object** output,
                       std::vector<grpc_error_handle>* error_list) {
  if (json.type() != Json::Type::kObject) {
    *output = nullptr;
    error_list->push_back(GRPC_ERROR_CREATE(
        absl::StrCat("field:", field_name, " error:type should be OBJECT")));
    return false;
  }
  *output = &json.object();
  return true;
}

bool ExtractJsonCharPtr(const Json& json, absl::string_view field_name,
                        char** output,
                        std::vector<grpc_error_handle>* error_list) {
  std::string output_string;
  if (!ExtractJsonString(json, field_name, &output_string, error_list))
    return false;
  *output = gpr_strdup(output_string.c_str());
  return true;
}

bool ParseJsonObjectFieldAsDuration(const Json::Object& object,
                                    absl::string_view field_name,
                                    Duration* output,
                                    std::vector<grpc_error_handle>* error_list,
                                    bool required) {
  // TODO(roth): Once we can use C++14 heterogenous lookups, stop
  // creating a std::string here.
  auto it = object.find(std::string(field_name));
  if (it == object.end()) {
    if (required) {
      error_list->push_back(GRPC_ERROR_CREATE(
          absl::StrCat("field:", field_name, " error:does not exist.")));
    }
    return false;
  }
  if (!ParseDurationFromJson(it->second, output)) {
    *output = Duration::NegativeInfinity();
    error_list->push_back(GRPC_ERROR_CREATE(
        absl::StrCat("field:", field_name,
                     " error:type should be STRING of the form given by "
                     "google.proto.Duration.")));
    return false;
  }
  return true;
}

}  // namespace grpc_core
