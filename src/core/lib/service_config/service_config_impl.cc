//
// Copyright 2022 gRPC authors.
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

#include "src/core/lib/service_config/service_config_impl.h"

#include <string.h>

#include <map>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include <grpc/support/log.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<ServiceConfig>> ServiceConfigImpl::Create(
    const ChannelArgs& args, absl::string_view json_string) {
  auto json = Json::Parse(json_string);
  if (!json.ok()) return json.status();
  absl::Status status;
  auto service_config = MakeRefCounted<ServiceConfigImpl>(
      args, std::string(json_string), std::move(*json), &status);
  if (!status.ok()) return status;
  return service_config;
}

ServiceConfigImpl::ServiceConfigImpl(const ChannelArgs& args,
                                     std::string json_string, Json json,
                                     absl::Status* status)
    : json_string_(std::move(json_string)), json_(std::move(json)) {
  GPR_DEBUG_ASSERT(status != nullptr);
  if (json_.type() != Json::Type::OBJECT) {
    *status = absl::InvalidArgumentError("JSON value is not an object");
    return;
  }
  std::vector<std::string> errors;
  auto parsed_global_configs =
      CoreConfiguration::Get().service_config_parser().ParseGlobalParameters(
          args, json_);
  if (!parsed_global_configs.ok()) {
    errors.emplace_back(parsed_global_configs.status().message());
  } else {
    parsed_global_configs_ = std::move(*parsed_global_configs);
  }
  absl::Status local_status = ParsePerMethodParams(args);
  if (!local_status.ok()) errors.emplace_back(local_status.message());
  if (!errors.empty()) {
    *status = absl::InvalidArgumentError(absl::StrCat(
        "Service config parsing errors: [", absl::StrJoin(errors, "; "), "]"));
  }
}

ServiceConfigImpl::~ServiceConfigImpl() {
  for (auto& p : parsed_method_configs_map_) {
    grpc_slice_unref_internal(p.first);
  }
}

absl::Status ServiceConfigImpl::ParseJsonMethodConfig(const ChannelArgs& args,
                                                      const Json& json,
                                                      size_t index) {
  std::vector<std::string> errors;
  const ServiceConfigParser::ParsedConfigVector* vector_ptr = nullptr;
  // Parse method config with each registered parser.
  auto parsed_configs_or =
      CoreConfiguration::Get().service_config_parser().ParsePerMethodParameters(
          args, json);
  if (!parsed_configs_or.ok()) {
    errors.emplace_back(parsed_configs_or.status().message());
  } else {
    auto parsed_configs =
        absl::make_unique<ServiceConfigParser::ParsedConfigVector>(
            std::move(*parsed_configs_or));
    parsed_method_config_vectors_storage_.push_back(std::move(parsed_configs));
    vector_ptr = parsed_method_config_vectors_storage_.back().get();
  }
  // Add an entry for each path.
  auto it = json.object_value().find("name");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      errors.emplace_back("field:name error:not of type Array");
    } else {
      const Json::Array& name_array = it->second.array_value();
      for (const Json& name : name_array) {
        absl::StatusOr<std::string> path = ParseJsonMethodName(name);
        if (!path.ok()) {
          errors.emplace_back(path.status().message());
        } else {
          if (path->empty()) {
            if (default_method_config_vector_ != nullptr) {
              errors.emplace_back(
                  "field:name error:multiple default method configs");
            }
            default_method_config_vector_ = vector_ptr;
          } else {
            grpc_slice key = grpc_slice_from_cpp_string(std::move(*path));
            // If the key is not already present in the map, this will
            // store a ref to the key in the map.
            auto& value = parsed_method_configs_map_[key];
            if (value != nullptr) {
              errors.emplace_back(
                  "field:name error:multiple method configs with same name");
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
  }
  if (!errors.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("index ", index, ": [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

absl::Status ServiceConfigImpl::ParsePerMethodParams(const ChannelArgs& args) {
  auto it = json_.object_value().find("methodConfig");
  if (it == json_.object_value().end()) return absl::OkStatus();
  if (it->second.type() != Json::Type::ARRAY) {
    return absl::InvalidArgumentError("field must be of type array");
  }
  std::vector<std::string> errors;
  for (size_t i = 0; i < it->second.array_value().size(); ++i) {
    const Json& method_config = it->second.array_value()[i];
    if (method_config.type() != Json::Type::OBJECT) {
      errors.emplace_back(absl::StrCat("index ", i, ": not of type Object"));
    } else {
      absl::Status status = ParseJsonMethodConfig(args, method_config, i);
      if (!status.ok()) errors.emplace_back(status.message());
    }
  }
  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "errors parsing methodConfig: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ServiceConfigImpl::ParseJsonMethodName(
    const Json& json) {
  if (json.type() != Json::Type::OBJECT) {
    return absl::InvalidArgumentError("field:name error:type is not object");
  }
  // Find service name.
  const std::string* service_name = nullptr;
  auto it = json.object_value().find("service");
  if (it != json.object_value().end() &&
      it->second.type() != Json::Type::JSON_NULL) {
    if (it->second.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          "field:name error: field:service error:not of type string");
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
      return absl::InvalidArgumentError(
          "field:name error: field:method error:not of type string");
    }
    if (!it->second.string_value().empty()) {
      method_name = &it->second.string_value();
    }
  }
  // If neither service nor method are specified, it's the default.
  // Method name may not be specified without service name.
  if (service_name == nullptr) {
    if (method_name != nullptr) {
      return absl::InvalidArgumentError(
          "field:name error:method name populated without service name");
    }
    return "";
  }
  // Construct path.
  return absl::StrCat("/", *service_name, "/",
                      method_name == nullptr ? "" : *method_name);
}

const ServiceConfigParser::ParsedConfigVector*
ServiceConfigImpl::GetMethodParsedConfigVector(const grpc_slice& path) const {
  if (parsed_method_configs_map_.empty()) {
    return default_method_config_vector_;
  }
  // Try looking up the full path in the map.
  auto it = parsed_method_configs_map_.find(path);
  if (it != parsed_method_configs_map_.end()) return it->second;
  // If we didn't find a match for the path, try looking for a wildcard
  // entry (i.e., change "/service/method" to "/service/").
  UniquePtr<char> path_str(grpc_slice_to_c_string(path));
  char* sep = strrchr(path_str.get(), '/');
  if (sep == nullptr) return nullptr;  // Shouldn't ever happen.
  sep[1] = '\0';
  grpc_slice wildcard_path = grpc_slice_from_static_string(path_str.get());
  it = parsed_method_configs_map_.find(wildcard_path);
  if (it != parsed_method_configs_map_.end()) return it->second;
  // Try default method config, if set.
  return default_method_config_vector_;
}

}  // namespace grpc_core
