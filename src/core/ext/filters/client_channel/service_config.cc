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

RefCountedPtr<ServiceConfig> ServiceConfig::Create(const char* json) {
  UniquePtr<char> service_config_json(gpr_strdup(json));
  UniquePtr<char> json_string(gpr_strdup(json));
  grpc_json* json_tree = grpc_json_parse_string(json_string.get());
  if (json_tree == nullptr) {
    gpr_log(GPR_INFO, "failed to parse JSON for service config");
    return nullptr;
  }
  return MakeRefCounted<ServiceConfig>(std::move(service_config_json),
                                       std::move(json_string), json_tree);
}

ServiceConfig::ServiceConfig(UniquePtr<char> service_config_json,
                             UniquePtr<char> json_string, grpc_json* json_tree)
    : service_config_json_(std::move(service_config_json)),
      json_string_(std::move(json_string)),
      json_tree_(json_tree) {}

ServiceConfig::~ServiceConfig() { grpc_json_destroy(json_tree_); }

const char* ServiceConfig::GetLoadBalancingPolicyName() const {
  if (json_tree_->type != GRPC_JSON_OBJECT || json_tree_->key != nullptr) {
    return nullptr;
  }
  const char* lb_policy_name = nullptr;
  for (grpc_json* field = json_tree_->child; field != nullptr;
       field = field->next) {
    if (field->key == nullptr) return nullptr;
    if (strcmp(field->key, "loadBalancingPolicy") == 0) {
      if (lb_policy_name != nullptr) return nullptr;  // Duplicate.
      if (field->type != GRPC_JSON_STRING) return nullptr;
      lb_policy_name = field->value;
    }
  }
  return lb_policy_name;
}

int ServiceConfig::CountNamesInMethodConfig(grpc_json* json) {
  int num_names = 0;
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key != nullptr && strcmp(field->key, "name") == 0) {
      if (field->type != GRPC_JSON_ARRAY) return -1;
      for (grpc_json* name = field->child; name != nullptr; name = name->next) {
        if (name->type != GRPC_JSON_OBJECT) return -1;
        ++num_names;
      }
    }
  }
  return num_names;
}

UniquePtr<char> ServiceConfig::ParseJsonMethodName(grpc_json* json) {
  if (json->type != GRPC_JSON_OBJECT) return nullptr;
  const char* service_name = nullptr;
  const char* method_name = nullptr;
  for (grpc_json* child = json->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) return nullptr;
    if (child->type != GRPC_JSON_STRING) return nullptr;
    if (strcmp(child->key, "service") == 0) {
      if (service_name != nullptr) return nullptr;  // Duplicate.
      if (child->value == nullptr) return nullptr;
      service_name = child->value;
    } else if (strcmp(child->key, "method") == 0) {
      if (method_name != nullptr) return nullptr;  // Duplicate.
      if (child->value == nullptr) return nullptr;
      method_name = child->value;
    }
  }
  if (service_name == nullptr) return nullptr;  // Required field.
  char* path;
  gpr_asprintf(&path, "/%s/%s", service_name,
               method_name == nullptr ? "*" : method_name);
  return UniquePtr<char>(path);
}

}  // namespace grpc_core
