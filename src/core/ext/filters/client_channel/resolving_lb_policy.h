/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVING_LB_POLICY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVING_LB_POLICY_H

#include <grpc/support/port_platform.h>

#include "absl/container/inlined_vector.h"

#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// An LB policy that wraps a resolver and a child LB policy to make use
// of the addresses returned by the resolver.
//
// When used in the client_channel code, the resolver will attempt to
// fetch the service config, and the child LB policy name and config
// will be determined based on the service config.
//
// When used in an LB policy implementation that needs to do another
// round of resolution before creating a child policy, the resolver does
// not fetch the service config, and the caller must pre-determine the
// child LB policy and config to use.
class ResolvingLoadBalancingPolicy : public LoadBalancingPolicy {
 public:
  class ChannelConfigHelper {
   public:
    struct ApplyServiceConfigResult {
      // Set to true if the service config has changed since the last result.
      bool service_config_changed = false;
      // Set to true if we don't have a valid service config to use.
      // This tells the ResolvingLoadBalancingPolicy to put the channel
      // into TRANSIENT_FAILURE.
      bool no_valid_service_config = false;
      // A service config parsing error occurred.
      grpc_error* service_config_error = GRPC_ERROR_NONE;
      // The LB policy config to use.
      RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config;
    };

    virtual ~ChannelConfigHelper() = default;

    // Applies the service config to the channel.
    virtual ApplyServiceConfigResult ApplyServiceConfig(
        const Resolver::Result& result) = 0;

    // Applies the ConfigSelector to the channel.
    virtual void ApplyConfigSelector(
        bool service_config_changed,
        RefCountedPtr<ConfigSelector> config_selector) = 0;

    // Indicates a resolver transient failure.
    virtual void ResolverTransientFailure(grpc_error* error) = 0;
  };

  ResolvingLoadBalancingPolicy(Args args, TraceFlag* tracer,
                               grpc_core::UniquePtr<char> target_uri,
                               ChannelConfigHelper* helper);

  virtual const char* name() const override { return "resolving_lb"; }

  // No-op -- should never get updates from the channel.
  // TODO(roth): Need to support updating child LB policy's config for xds
  // use case.
  void UpdateLocked(UpdateArgs /*args*/) override {}

  void ExitIdleLocked() override;

  void ResetBackoffLocked() override;

 private:
  using TraceStringVector = absl::InlinedVector<const char*, 3>;

  class ResolverResultHandler;
  class ResolvingControlHelper;

  ~ResolvingLoadBalancingPolicy();

  void ShutdownLocked() override;

  void OnResolverError(grpc_error* error);
  void CreateOrUpdateLbPolicyLocked(
      RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config,
      Resolver::Result result);
  OrphanablePtr<LoadBalancingPolicy> CreateLbPolicyLocked(
      const grpc_channel_args& args);
  void MaybeAddTraceMessagesForAddressChangesLocked(
      bool resolution_contains_addresses, TraceStringVector* trace_strings);
  void ConcatenateAndAddChannelTraceLocked(
      const TraceStringVector& trace_strings) const;
  void OnResolverResultChangedLocked(Resolver::Result result);

  // Passed in from caller at construction time.
  TraceFlag* tracer_;
  grpc_core::UniquePtr<char> target_uri_;
  ChannelConfigHelper* helper_;

  // Resolver and associated state.
  OrphanablePtr<Resolver> resolver_;
  bool previous_resolution_contained_addresses_ = false;

  // Determined by resolver results.
  grpc_core::UniquePtr<char> child_policy_name_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_lb_config_;

  // Child LB policy.
  OrphanablePtr<LoadBalancingPolicy> lb_policy_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVING_LB_POLICY_H */
