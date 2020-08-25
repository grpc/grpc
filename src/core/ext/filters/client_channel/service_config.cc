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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/service_config.h"

#include <string>

#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/service_config_parser.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

RefCountedPtr<ServiceConfig> ServiceConfig::Create(
    absl::string_view json_string, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr);
  Json json = Json::Parse(json_string, error);
  if (*error != GRPC_ERROR_NONE) return nullptr;
  return MakeRefCounted<ServiceConfig>(
      std::string(json_string.data(), json_string.size()), std::move(json),
      error);
}

ServiceConfig::ServiceConfig(std::string json_string, Json json,
                             grpc_error** error)
    : json_string_(std::move(json_string)), json_(std::move(json)) {
  GPR_DEBUG_ASSERT(error != nullptr);
  if (json_.type() != Json::Type::OBJECT) {
    *error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON value is not an object");
    return;
  }
  std::vector<grpc_error*> error_list;
  grpc_error* global_error = GRPC_ERROR_NONE;
  parsed_global_configs_ =
      ServiceConfigParser::ParseGlobalParameters(json_, &global_error);
  if (global_error != GRPC_ERROR_NONE) error_list.push_back(global_error);
  grpc_error* local_error = ParsePerMethodParams();
  if (local_error != GRPC_ERROR_NONE) error_list.push_back(local_error);
  if (!error_list.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_VECTOR("Service config parsing error",
                                           &error_list);
  }
}

ServiceConfig::~ServiceConfig() {
  for (auto& p : parsed_method_configs_map_) {
    grpc_slice_unref_internal(p.first);
  }
}

grpc_error* ServiceConfig::ParseJsonMethodConfig(const Json& json) {
  std::vector<grpc_error*> error_list;
  // Parse method config with each registered parser.
  auto parsed_configs =
      absl::make_unique<ServiceConfigParser::ParsedConfigVector>();
  grpc_error* parser_error = GRPC_ERROR_NONE;
  *parsed_configs =
      ServiceConfigParser::ParsePerMethodParameters(json, &parser_error);
  if (parser_error != GRPC_ERROR_NONE) {
    error_list.push_back(parser_error);
  }
  parsed_method_config_vectors_storage_.push_back(std::move(parsed_configs));
  const auto* vector_ptr = parsed_method_config_vectors_storage_.back().get();
  // Add an entry for each path.
  bool found_name = false;
  auto it = json.object_value().find("name");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:name error:not of type Array"));
      return GRPC_ERROR_CREATE_FROM_VECTOR("methodConfig", &error_list);
    }
    const Json::Array& name_array = it->second.array_value();
    for (const Json& name : name_array) {
      grpc_error* parse_error = GRPC_ERROR_NONE;
      std::string path = ParseJsonMethodName(name, &parse_error);
      if (parse_error != GRPC_ERROR_NONE) {
        error_list.push_back(parse_error);
      } else {
        found_name = true;
        if (path.empty()) {
          if (default_method_config_vector_ != nullptr) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "field:name error:multiple default method configs"));
          }
          default_method_config_vector_ = vector_ptr;
        } else {
          grpc_slice key = grpc_slice_from_copied_string(path.c_str());
          // If the key is not already present in the map, this will
          // store a ref to the key in the map.
          auto& value = parsed_method_configs_map_[key];
          if (value != nullptr) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "field:name error:multiple method configs with same name"));
            // The map entry already existed, so we need to unref the
            // key we just created.
            grpc_slice_unref_internal(key);
          } else {
            value = vector_ptr;
          }
        }
      }
    }
  }
  if (!found_name) {
    parsed_method_config_vectors_storage_.pop_back();
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("methodConfig", &error_list);
}

grpc_error* ServiceConfig::ParsePerMethodParams() {
  std::vector<grpc_error*> error_list;
  auto it = json_.object_value().find("methodConfig");
  if (it != json_.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:methodConfig error:not of type Array"));
    }
    for (const Json& method_config : it->second.array_value()) {
      if (method_config.type() != Json::Type::OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:methodConfig error:not of type Object"));
        continue;
      }
      grpc_error* error = ParseJsonMethodConfig(method_config);
      if (error != GRPC_ERROR_NONE) {
        error_list.push_back(error);
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("Method Params", &error_list);
}

std::string ServiceConfig::ParseJsonMethodName(const Json& json,
                                               grpc_error** error) {
  if (json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:name error:type is not object");
    return "";
  }
  // Find service name.
  const std::string* service_name = nullptr;
  auto it = json.object_value().find("service");
  if (it != json.object_value().end() &&
      it->second.type() != Json::Type::JSON_NULL) {
    if (it->second.type() != Json::Type::STRING) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:name error: field:service error:not of type string");
      return "";
    }
    if (!it->second.string_value().empty()) {
      service_name = &it->second.string_value();
    }
  }
  const std::string* method_name = nullptr;
  // Find method name.
  it = json.object_value().find("method");
  if (it != json.object_value().end() &&
      it->second.type() != Json::Type::JSON_NULL) {
    if (it->second.type() != Json::Type::STRING) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:name error: field:method error:not of type string");
      return "";
    }
    if (!it->second.string_value().empty()) {
      method_name = &it->second.string_value();
    }
  }
  // If neither service nor method are specified, it's the default.
  // Method name may not be specified without service name.
  if (service_name == nullptr) {
    if (method_name != nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:name error:method name populated without service name");
    }
    return "";
  }
  // Construct path.
  return absl::StrCat("/", *service_name, "/",
                      method_name == nullptr ? "" : *method_name);
}

const ServiceConfigParser::ParsedConfigVector*
ServiceConfig::GetMethodParsedConfigVector(const grpc_slice& path) const {
  // Try looking up the full path in the map.
  auto it = parsed_method_configs_map_.find(path);
  if (it != parsed_method_configs_map_.end()) return it->second;
  // If we didn't find a match for the path, try looking for a wildcard
  // entry (i.e., change "/service/method" to "/service/").
  UniquePtr<char> path_str(grpc_slice_to_c_string(path));
  char* sep = strrchr(path_str.get(), '/') + 1;
  if (sep == nullptr) return nullptr;  // Shouldn't ever happen.
  *sep = '\0';
  grpc_slice wildcard_path = grpc_slice_from_static_string(path_str.get());
  it = parsed_method_configs_map_.find(wildcard_path);
  if (it != parsed_method_configs_map_.end()) return it->second;
  // Try default method config, if set.
  return default_method_config_vector_;
}

}  // namespace grpc_core
