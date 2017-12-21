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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_REGISTRY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_REGISTRY_H

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/memory.h"
#include "src/core/lib/support/orphanable.h"
#include "src/core/lib/support/vector.h"

namespace grpc_core {

class LoadBalancingPolicyRegistry {
 public:
  /// Returns the global LB policy registry.
  static LoadBalancingPolicyRegistry* Global();

  /// Registers an LB policy factory.  The factory will be used to create an
  /// LB policy for any URI whose scheme matches that of the factory.
  void RegisterLoadBalancingPolicyFactory(
      UniquePtr<LoadBalancingPolicyFactory> factory);

  /// Creates a LB policy of the type specified by \a name.
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      const char* name, const LoadBalancingPolicy::Args& args);

  /// Global initialization and shutdown hooks.
  static void Init();
  static void Shutdown();

  // DO NOT USE THESE.
  // Instead, use the singleton instance returned by Global().
  // The only reason these are not private is that they need to be
  // accessed by the gRPC-specific New<> and Delete<>.
  LoadBalancingPolicyRegistry();
  ~LoadBalancingPolicyRegistry();

 private:
  InlinedVector<UniquePtr<LoadBalancingPolicyFactory>, 10> factories_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_REGISTRY_H */
