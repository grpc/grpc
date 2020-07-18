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

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"

#include "src/core/lib/gpr/string.h"

namespace grpc_core {

namespace {

class RegistryState {
 public:
  RegistryState() {}

  void RegisterLoadBalancingPolicyFactory(
      std::unique_ptr<LoadBalancingPolicyFactory> factory) {
    gpr_log(GPR_DEBUG, "registering LB policy factory for \"%s\"",
            factory->name());
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
  absl::InlinedVector<std::unique_ptr<LoadBalancingPolicyFactory>, 10>
      factories_;
};

RegistryState* g_state = nullptr;

}  // namespace

//
// LoadBalancingPolicyRegistry::Builder
//

void LoadBalancingPolicyRegistry::Builder::InitRegistry() {
  if (g_state == nullptr) g_state = new RegistryState();
}

void LoadBalancingPolicyRegistry::Builder::ShutdownRegistry() {
  delete g_state;
  g_state = nullptr;
}

void LoadBalancingPolicyRegistry::Builder::RegisterLoadBalancingPolicyFactory(
    std::unique_ptr<LoadBalancingPolicyFactory> factory) {
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
        factory->ParseLoadBalancingConfig(Json(), &error) == nullptr;
    GRPC_ERROR_UNREF(error);
  }
  return true;
}

namespace {

// Returns the JSON node of policy (with both policy name and config content)
// given the JSON node of a LoadBalancingConfig array.
grpc_error* ParseLoadBalancingConfigHelper(
    const Json& lb_config_array, Json::Object::const_iterator* result) {
  if (lb_config_array.type() != Json::Type::ARRAY) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("type should be array");
  }
  // Find the first LB policy that this client supports.
  for (const Json& lb_config : lb_config_array.array_value()) {
    if (lb_config.type() != Json::Type::OBJECT) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "child entry should be of type object");
    }
    if (lb_config.object_value().empty()) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "no policy found in child entry");
    }
    if (lb_config.object_value().size() > 1) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("oneOf violation");
    }
    auto it = lb_config.object_value().begin();
    if (it->second.type() != Json::Type::OBJECT) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "child entry should be of type object");
    }
    // If we support this policy, then select it.
    if (LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(
            it->first.c_str(), nullptr)) {
      *result = it;
      return GRPC_ERROR_NONE;
    }
  }
  return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No known policy");
}

}  // namespace

RefCountedPtr<LoadBalancingPolicy::Config>
LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(const Json& json,
                                                      grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  GPR_ASSERT(g_state != nullptr);
  Json::Object::const_iterator policy;
  *error = ParseLoadBalancingConfigHelper(json, &policy);
  if (*error != GRPC_ERROR_NONE) {
    return nullptr;
  }
  // Find factory.
  LoadBalancingPolicyFactory* factory =
      g_state->GetLoadBalancingPolicyFactory(policy->first.c_str());
  if (factory == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat("Factory not found for policy \"%s\"", policy->first)
            .c_str());
    return nullptr;
  }
  // Parse load balancing config via factory.
  return factory->ParseLoadBalancingConfig(policy->second, error);
}

}  // namespace grpc_core
