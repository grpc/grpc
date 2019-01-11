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

#include "src/core/lib/transport/service_config.h"

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

UniquePtr<ServiceConfig> ServiceConfig::Create(const char* json) {
  UniquePtr<char> json_string(gpr_strdup(json));
  grpc_json* json_tree = grpc_json_parse_string(json_string.get());
  if (json_tree == nullptr) {
    gpr_log(GPR_INFO, "failed to parse JSON for service config");
    return nullptr;
  }
  return MakeUnique<ServiceConfig>(std::move(json_string), json_tree);
}

ServiceConfig::ServiceConfig(UniquePtr<char> json_string, grpc_json* json_tree)
    : json_string_(std::move(json_string)), json_tree_(json_tree) {}

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

UniquePtr<char> ServiceConfig::ParseXdsConfig(grpc_json* xds_config_json,
                                              grpc_json** child_policy,
                                              grpc_json** fallback_policy) {
  const char* balancer_name = nullptr;
  grpc_json* output_child_policy = nullptr;
  grpc_json* output_fallback_policy = nullptr;
  for (grpc_json* field = xds_config_json; field != nullptr;
       field = field->next) {
    if (field->key == nullptr) return nullptr;
    if (strcmp(field->key, "balancer_name") == 0) {
      if (balancer_name != nullptr) return nullptr;  // Duplicate.
      if (field->type != GRPC_JSON_STRING) return nullptr;
      balancer_name = field->value;
    } else if (strcmp(field->key, "child_policy") == 0) {
      if (output_child_policy != nullptr) return nullptr;  // Duplicate.
      if (field->type != GRPC_JSON_OBJECT) return nullptr;
      output_child_policy = ParseLoadBalancingConfig(field->child);
    } else if (strcmp(field->key, "fallback_policy") == 0) {
      if (output_fallback_policy != nullptr) return nullptr;  // Duplicate.
      if (field->type != GRPC_JSON_OBJECT) return nullptr;
      output_fallback_policy = ParseLoadBalancingConfig(field->child);
    }
  }
  if (balancer_name == nullptr) return nullptr;  // Required field.
  if (output_child_policy != nullptr) *child_policy = output_child_policy;
  if (output_fallback_policy != nullptr) {
    *fallback_policy = output_fallback_policy;
  }
  return UniquePtr<char>(gpr_strdup(balancer_name));
}

grpc_json* ServiceConfig::ParseLoadBalancingConfig(grpc_json* lb_config_json) {
  // Find the policy object.
  grpc_json* policy = nullptr;
  for (grpc_json* field = lb_config_json; field != nullptr;
       field = field->next) {
    if (field->key == nullptr || strcmp(field->key, "policy") != 0 ||
        field->type != GRPC_JSON_OBJECT) {
      return nullptr;
    }
    if (policy != nullptr) return nullptr;  // Duplicate.
    policy = field;
  }
  // Find the specific policy content since the policy object is of type
  // "oneof".
  grpc_json* policy_content = nullptr;
  for (grpc_json* field = policy->child; field != nullptr;
       field = field->next) {
    if (field->key == nullptr || field->type != GRPC_JSON_OBJECT)
      return nullptr;
    if (policy_content != nullptr) return nullptr;  // Violate "oneof" type.
    policy_content = field;
  }
  return policy_content;
}

}  // namespace grpc_core
