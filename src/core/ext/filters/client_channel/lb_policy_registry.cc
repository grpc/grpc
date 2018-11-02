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
    const char* name, const LoadBalancingPolicy::Args& args) {
  GPR_ASSERT(g_state != nullptr);
  // Find factory.
  LoadBalancingPolicyFactory* factory =
      g_state->GetLoadBalancingPolicyFactory(name);
  if (factory == nullptr) return nullptr;  // Specified name not found.
  // Create policy via factory.
  return factory->CreateLoadBalancingPolicy(args);
}

bool LoadBalancingPolicyRegistry::LoadBalancingPolicyExists(const char* name) {
  GPR_ASSERT(g_state != nullptr);
  return g_state->GetLoadBalancingPolicyFactory(name) != nullptr;
}

}  // namespace grpc_core
