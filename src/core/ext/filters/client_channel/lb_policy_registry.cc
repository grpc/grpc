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

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"

#include <string.h>

#include "src/core/lib/gpr/string.h"

namespace grpc_core {

namespace {
LoadBalancingPolicyRegistry* g_registry = nullptr;
}  // namespace

LoadBalancingPolicyRegistry* LoadBalancingPolicyRegistry::Global() {
  return g_registry;
}

void LoadBalancingPolicyRegistry::Init() {
  g_registry = New<LoadBalancingPolicyRegistry>();
}

void LoadBalancingPolicyRegistry::Shutdown() {
  Delete(g_registry);
  g_registry = nullptr;
}

LoadBalancingPolicyRegistry::LoadBalancingPolicyRegistry() {}

LoadBalancingPolicyRegistry::~LoadBalancingPolicyRegistry() {}

void LoadBalancingPolicyRegistry::RegisterLoadBalancingPolicyFactory(
    UniquePtr<LoadBalancingPolicyFactory> factory) {
  for (size_t i = 0; i < factories_.size(); ++i) {
    GPR_ASSERT(strcmp(factories_[i]->name(), factory->name()) != 0);
  }
  factories_.push_back(std::move(factory));
}

OrphanablePtr<LoadBalancingPolicy>
LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
    const char* name, const LoadBalancingPolicy::Args& args) {
  // Find factory.
  LoadBalancingPolicyFactory* factory = nullptr;
  for (size_t i = 0; i < factories_.size(); ++i) {
    if (strcmp(name, factories_[i]->name()) == 0) {
      factory = factories_[i].get();
    }
  }
  if (factory == nullptr) return nullptr;  // Specified name not found.
  // Create policy via factory.
  return factory->CreateLoadBalancingPolicy(args);
}

}  // namespace grpc_core
