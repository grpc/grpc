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

#include <string.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

namespace grpc_core {

namespace {
typedef InlinedVector<std::unique_ptr<ServiceConfig::Parser>,
                      ServiceConfig::kNumPreallocatedParsers>
    ServiceConfigParserList;
ServiceConfigParserList* g_registered_parsers;
}  // namespace

RefCountedPtr<ServiceConfig> ServiceConfig::Create(StringView json_string,
                                                   grpc_error** error) {
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
  grpc_error* error_list[2];
  int error_count = 0;
  grpc_error* global_error = ParseGlobalParams();
  grpc_error* local_error = ParsePerMethodParams();
  if (global_error != GRPC_ERROR_NONE) {
    error_list[error_count++] = global_error;
  }
  if (local_error != GRPC_ERROR_NONE) {
    error_list[error_count++] = local_error;
  }
  if (error_count > 0) {
    *error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Service config parsing error", error_list, error_count);
    GRPC_ERROR_UNREF(global_error);
    GRPC_ERROR_UNREF(local_error);
  }
}

grpc_error* ServiceConfig::ParseGlobalParams() {
  std::vector<grpc_error*> error_list;
  for (size_t i = 0; i < g_registered_parsers->size(); i++) {
    grpc_error* parser_error = GRPC_ERROR_NONE;
    auto parsed_obj =
        (*g_registered_parsers)[i]->ParseGlobalParams(json_, &parser_error);
    if (parser_error != GRPC_ERROR_NONE) {
      error_list.push_back(parser_error);
    }
    parsed_global_configs_.push_back(std::move(parsed_obj));
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("Global Params", &error_list);
}

grpc_error* ServiceConfig::ParseJsonMethodConfigToServiceConfigVectorTable(
    const Json& json,
    InlinedVector<SliceHashTable<const ParsedConfigVector*>::Entry, 10>*
        entries) {
  auto objs_vector = absl::make_unique<ParsedConfigVector>();
  InlinedVector<grpc_error*, 4> error_list;
  for (size_t i = 0; i < g_registered_parsers->size(); i++) {
    grpc_error* parser_error = GRPC_ERROR_NONE;
    auto parsed_obj =
        (*g_registered_parsers)[i]->ParsePerMethodParams(json, &parser_error);
    if (parser_error != GRPC_ERROR_NONE) {
      error_list.push_back(parser_error);
    }
    objs_vector->push_back(std::move(parsed_obj));
  }
  parsed_method_config_vectors_storage_.push_back(std::move(objs_vector));
  const auto* vector_ptr = parsed_method_config_vectors_storage_.back().get();
  // Construct list of paths.
  InlinedVector<UniquePtr<char>, 10> paths;
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
      UniquePtr<char> path = ParseJsonMethodName(name, &parse_error);
      if (path == nullptr) {
        error_list.push_back(parse_error);
      } else {
        GPR_DEBUG_ASSERT(parse_error == GRPC_ERROR_NONE);
        paths.push_back(std::move(path));
      }
    }
  }
  if (paths.size() == 0) {
    error_list.push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No names specified"));
  }
  // Add entry for each path.
  for (size_t i = 0; i < paths.size(); ++i) {
    entries->push_back(
        {grpc_slice_from_copied_string(paths[i].get()), vector_ptr});
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("methodConfig", &error_list);
}

grpc_error* ServiceConfig::ParsePerMethodParams() {
  InlinedVector<SliceHashTable<const ParsedConfigVector*>::Entry, 10> entries;
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
      grpc_error* error = ParseJsonMethodConfigToServiceConfigVectorTable(
          method_config, &entries);
      if (error != GRPC_ERROR_NONE) {
        error_list.push_back(error);
      }
    }
  }
  if (!entries.empty()) {
    parsed_method_configs_table_ =
        SliceHashTable<const ParsedConfigVector*>::Create(
            entries.size(), entries.data(), nullptr);
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("Method Params", &error_list);
}

UniquePtr<char> ServiceConfig::ParseJsonMethodName(const Json& json,
                                                   grpc_error** error) {
  if (json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:name error:type is not object");
    return nullptr;
  }
  // Find service name.
  auto it = json.object_value().find("service");
  if (it == json.object_value().end()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:name error: field:service error:not found");
    return nullptr;  // Required field.
  }
  if (it->second.type() != Json::Type::STRING) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:name error: field:service error:not of type string");
    return nullptr;
  }
  if (it->second.string_value().empty()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:name error: field:service error:empty value");
    return nullptr;
  }
  const char* service_name = it->second.string_value().c_str();
  const char* method_name = nullptr;
  // Find method name.
  it = json.object_value().find("method");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::STRING) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:name error: field:method error:not of type string");
      return nullptr;
    }
    if (it->second.string_value().empty()) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:name error: field:method error:empty value");
      return nullptr;
    }
    method_name = it->second.string_value().c_str();
  }
  char* path;
  gpr_asprintf(&path, "/%s/%s", service_name,
               method_name == nullptr ? "" : method_name);
  return grpc_core::UniquePtr<char>(path);
}

const ServiceConfig::ParsedConfigVector*
ServiceConfig::GetMethodParsedConfigVector(const grpc_slice& path) {
  if (parsed_method_configs_table_.get() == nullptr) {
    return nullptr;
  }
  const auto* value = parsed_method_configs_table_->Get(path);
  // If we didn't find a match for the path, try looking for a wildcard
  // entry (i.e., change "/service/method" to "/service/").
  if (value == nullptr) {
    char* path_str = grpc_slice_to_c_string(path);
    const char* sep = strrchr(path_str, '/') + 1;
    const size_t len = (size_t)(sep - path_str);
    char* buf = (char*)gpr_malloc(len + 1);  // trailing NUL
    memcpy(buf, path_str, len);
    buf[len] = '\0';
    grpc_slice wildcard_path = grpc_slice_from_copied_string(buf);
    gpr_free(buf);
    value = parsed_method_configs_table_->Get(wildcard_path);
    grpc_slice_unref_internal(wildcard_path);
    gpr_free(path_str);
    if (value == nullptr) return nullptr;
  }
  return *value;
}

size_t ServiceConfig::RegisterParser(std::unique_ptr<Parser> parser) {
  g_registered_parsers->push_back(std::move(parser));
  return g_registered_parsers->size() - 1;
}

void ServiceConfig::Init() {
  GPR_ASSERT(g_registered_parsers == nullptr);
  g_registered_parsers = new ServiceConfigParserList();
}

void ServiceConfig::Shutdown() {
  delete g_registered_parsers;
  g_registered_parsers = nullptr;
}

}  // namespace grpc_core
