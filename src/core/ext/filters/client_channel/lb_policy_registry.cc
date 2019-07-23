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

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"

#include <string.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/inlined_vector.h"

namespace grpc_core {

namespace {

class RegistryState {
 public:
  RegistryState() {}

  void RegisterLoadBalancingPolicyFactory(
      UniquePtr<LoadBalancingPolicyFactory> factory) {
    for (size_t i = 0; i < factories_.size(); ++i) {
      GPR_ASSERT(strcmp(factories_[i]->name(), factory->name()) != 0);
    }
    factories_.push_back(std::move(factory));
  }

  LoadBalancingPolicyFactory* GetLoadBalancingPolicyFactory(
      const char* name) const {
    for (size_t i = 0; i < factories_.size(); ++i) {
      if (strcmp(name, factories_[i]->name()) == 0) {
        return factories_[i].get();
      }
    }
    return nullptr;
  }

 private:
  InlinedVector<UniquePtr<LoadBalancingPolicyFactory>, 10> factories_;
};

RegistryState* g_state = nullptr;

}  // namespace

//
// LoadBalancingPolicyRegistry::Builder
//

void LoadBalancingPolicyRegistry::Builder::InitRegistry() {
  if (g_state == nullptr) g_state = New<RegistryState>();
}

void LoadBalancingPolicyRegistry::Builder::ShutdownRegistry() {
  Delete(g_state);
  g_state = nullptr;
}

void LoadBalancingPolicyRegistry::Builder::RegisterLoadBalancingPolicyFactory(
    UniquePtr<LoadBalancingPolicyFactory> factory) {
  InitRegistry();
  g_state->RegisterLoadBalancingPolicyFactory(std::move(factory));
}

//
// LoadBalancingPolicyRegistry
//

OrphanablePtr<LoadBalancingPolicy>
LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
    const char* name, LoadBalancingPolicy::Args args) {
  GPR_ASSERT(g_state != nullptr);
  // Find factory.
  LoadBalancingPolicyFactory* factory =
      g_state->GetLoadBalancingPolicyFactory(name);
  if (factory == nullptr) return nullptr;  // Specified name not found.
  // Create policy via factory.
  return factory->CreateLoadBalancingPolicy(std::move(args));
}

bool LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(
    const char* name, bool* requires_config) {
  GPR_ASSERT(g_state != nullptr);
  auto* factory = g_state->GetLoadBalancingPolicyFactory(name);
  if (factory == nullptr) {
    return false;
  }
  if (requires_config != nullptr) {
    grpc_error* error = GRPC_ERROR_NONE;
    // Check if the load balancing policy allows an empty config
    *requires_config =
        factory->ParseLoadBalancingConfig(nullptr, &error) == nullptr;
    GRPC_ERROR_UNREF(error);
  }
  return true;
}

namespace {
// Returns the JSON node of policy (with both policy name and config content)
// given the JSON node of a LoadBalancingConfig array.
grpc_json* ParseLoadBalancingConfigHelper(const grpc_json* lb_config_array,
                                          grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  char* error_msg;
  if (lb_config_array == nullptr || lb_config_array->type != GRPC_JSON_ARRAY) {
    gpr_asprintf(&error_msg, "field:%s error:type should be array",
                 lb_config_array->key);
    *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    return nullptr;
  }
  const char* field_name = lb_config_array->key;
  // Find the first LB policy that this client supports.
  for (const grpc_json* lb_config = lb_config_array->child;
       lb_config != nullptr; lb_config = lb_config->next) {
    if (lb_config->type != GRPC_JSON_OBJECT) {
      gpr_asprintf(&error_msg,
                   "field:%s error:child entry should be of type object",
                   field_name);
      *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
      gpr_free(error_msg);
      return nullptr;
    }
    grpc_json* policy = nullptr;
    for (grpc_json* field = lb_config->child; field != nullptr;
         field = field->next) {
      if (field->key == nullptr || field->type != GRPC_JSON_OBJECT) {
        gpr_asprintf(&error_msg,
                     "field:%s error:child entry should be of type object",
                     field_name);
        *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
        gpr_free(error_msg);
        return nullptr;
      }
      if (policy != nullptr) {
        gpr_asprintf(&error_msg, "field:%s error:oneOf violation", field_name);
        *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
        gpr_free(error_msg);
        return nullptr;
      }  // Violate "oneof" type.
      policy = field;
    }
    if (policy == nullptr) {
      gpr_asprintf(&error_msg, "field:%s error:no policy found in child entry",
                   field_name);
      *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
      gpr_free(error_msg);
      return nullptr;
    }
    // If we support this policy, then select it.
    if (LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(policy->key,
                                                               nullptr)) {
      return policy;
    }
  }
  gpr_asprintf(&error_msg, "field:%s error:No known policy", field_name);
  *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
  gpr_free(error_msg);
  return nullptr;
}
}  // namespace

RefCountedPtr<LoadBalancingPolicy::Config>
LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(const grpc_json* json,
                                                      grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  GPR_ASSERT(g_state != nullptr);
  const grpc_json* policy = ParseLoadBalancingConfigHelper(json, error);
  if (policy == nullptr) {
    return nullptr;
  } else {
    GPR_DEBUG_ASSERT(*error == GRPC_ERROR_NONE && json != nullptr);
    // Find factory.
    LoadBalancingPolicyFactory* factory =
        g_state->GetLoadBalancingPolicyFactory(policy->key);
    if (factory == nullptr) {
      char* msg;
      gpr_asprintf(&msg, "field:%s error:Factory not found to create policy",
                   json->key);
      *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      return nullptr;
    }
    // Parse load balancing config via factory.
    return factory->ParseLoadBalancingConfig(policy, error);
  }
}

}  // namespace grpc_core
