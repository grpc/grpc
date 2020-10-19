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

#include "src/core/ext/filters/client_channel/resolving_lb_policy.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_metadata.h"

namespace grpc_core {

//
// ResolvingLoadBalancingPolicy::ResolverResultHandler
//

class ResolvingLoadBalancingPolicy::ResolverResultHandler
    : public Resolver::ResultHandler {
 public:
  explicit ResolverResultHandler(
      RefCountedPtr<ResolvingLoadBalancingPolicy> parent)
      : parent_(std::move(parent)) {}

  ~ResolverResultHandler() override {
    if (GRPC_TRACE_FLAG_ENABLED(*(parent_->tracer_))) {
      gpr_log(GPR_INFO, "resolving_lb=%p: resolver shutdown complete",
              parent_.get());
    }
  }

  void ReturnResult(Resolver::Result result) override {
    parent_->OnResolverResultChangedLocked(std::move(result));
  }

  void ReturnError(grpc_error* error) override {
    parent_->OnResolverError(error);
  }

 private:
  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
};

//
// ResolvingLoadBalancingPolicy::ResolvingControlHelper
//

class ResolvingLoadBalancingPolicy::ResolvingControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ResolvingControlHelper(
      RefCountedPtr<ResolvingLoadBalancingPolicy> parent)
      : parent_(std::move(parent)) {}

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      ServerAddress address, const grpc_channel_args& args) override {
    if (parent_->resolver_ == nullptr) return nullptr;  // Shutting down.
    return parent_->channel_control_helper()->CreateSubchannel(
        std::move(address), args);
  }

  void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                   std::unique_ptr<SubchannelPicker> picker) override {
    if (parent_->resolver_ == nullptr) return;  // Shutting down.
    parent_->channel_control_helper()->UpdateState(state, status,
                                                   std::move(picker));
  }

  void RequestReresolution() override {
    if (parent_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(*(parent_->tracer_))) {
      gpr_log(GPR_INFO, "resolving_lb=%p: started name re-resolving",
              parent_.get());
    }
    parent_->resolver_->RequestReresolutionLocked();
  }

  void AddTraceEvent(TraceSeverity severity,
                     absl::string_view message) override {
    if (parent_->resolver_ == nullptr) return;  // Shutting down.
    parent_->channel_control_helper()->AddTraceEvent(severity, message);
  }

 private:
  RefCountedPtr<ResolvingLoadBalancingPolicy> parent_;
};

//
// ResolvingLoadBalancingPolicy
//

ResolvingLoadBalancingPolicy::ResolvingLoadBalancingPolicy(
    Args args, TraceFlag* tracer, grpc_core::UniquePtr<char> target_uri,
    ChannelConfigHelper* helper)
    : LoadBalancingPolicy(std::move(args)),
      tracer_(tracer),
      target_uri_(std::move(target_uri)),
      helper_(helper) {
  GPR_ASSERT(helper_ != nullptr);
  resolver_ = ResolverRegistry::CreateResolver(
      target_uri_.get(), args.args, interested_parties(), work_serializer(),
      absl::make_unique<ResolverResultHandler>(Ref()));
  // Since the validity of args has been checked when create the channel,
  // CreateResolver() must return a non-null result.
  GPR_ASSERT(resolver_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: starting name resolution", this);
  }
  channel_control_helper()->UpdateState(GRPC_CHANNEL_CONNECTING, absl::Status(),
                                        absl::make_unique<QueuePicker>(Ref()));
  resolver_->StartLocked();
}

ResolvingLoadBalancingPolicy::~ResolvingLoadBalancingPolicy() {
  GPR_ASSERT(resolver_ == nullptr);
  GPR_ASSERT(lb_policy_ == nullptr);
}

void ResolvingLoadBalancingPolicy::ShutdownLocked() {
  if (resolver_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
      gpr_log(GPR_INFO, "resolving_lb=%p: shutting down resolver=%p", this,
              resolver_.get());
    }
    resolver_.reset();
    if (lb_policy_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
        gpr_log(GPR_INFO, "resolving_lb=%p: shutting down lb_policy=%p", this,
                lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties());
      lb_policy_.reset();
    }
  }
}

void ResolvingLoadBalancingPolicy::ExitIdleLocked() {
  if (lb_policy_ != nullptr) lb_policy_->ExitIdleLocked();
}

void ResolvingLoadBalancingPolicy::ResetBackoffLocked() {
  if (resolver_ != nullptr) {
    resolver_->ResetBackoffLocked();
    resolver_->RequestReresolutionLocked();
  }
  if (lb_policy_ != nullptr) lb_policy_->ResetBackoffLocked();
}

void ResolvingLoadBalancingPolicy::OnResolverError(grpc_error* error) {
  if (resolver_ == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: resolver transient failure: %s", this,
            grpc_error_string(error));
  }
  // If we already have an LB policy from a previous resolution
  // result, then we continue to let it set the connectivity state.
  // Otherwise, we go into TRANSIENT_FAILURE.
  if (lb_policy_ == nullptr) {
    grpc_error* state_error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Resolver transient failure", &error, 1);
    helper_->ResolverTransientFailure(GRPC_ERROR_REF(state_error));
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, grpc_error_to_absl_status(state_error),
        absl::make_unique<TransientFailurePicker>(state_error));
  }
  GRPC_ERROR_UNREF(error);
}

void ResolvingLoadBalancingPolicy::CreateOrUpdateLbPolicyLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config,
    Resolver::Result result) {
  // Construct update.
  UpdateArgs update_args;
  update_args.addresses = std::move(result.addresses);
  update_args.config = std::move(lb_policy_config);
  // Remove the config selector from channel args so that we're not holding
  // unnecessary refs that cause it to be destroyed somewhere other than in the
  // WorkSerializer.
  const char* arg_name = GRPC_ARG_CONFIG_SELECTOR;
  update_args.args =
      grpc_channel_args_copy_and_remove(result.args, &arg_name, 1);
  // Create policy if needed.
  if (lb_policy_ == nullptr) {
    lb_policy_ = CreateLbPolicyLocked(*update_args.args);
  }
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: Updating child policy %p", this,
            lb_policy_.get());
  }
  lb_policy_->UpdateLocked(std::move(update_args));
}

// Creates a new LB policy.
OrphanablePtr<LoadBalancingPolicy>
ResolvingLoadBalancingPolicy::CreateLbPolicyLocked(
    const grpc_channel_args& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.channel_control_helper =
      absl::make_unique<ResolvingControlHelper>(Ref());
  lb_policy_args.args = &args;
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args), tracer_);
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: created new LB policy %p", this,
            lb_policy.get());
  }
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void ResolvingLoadBalancingPolicy::MaybeAddTraceMessagesForAddressChangesLocked(
    bool resolution_contains_addresses, TraceStringVector* trace_strings) {
  if (!resolution_contains_addresses &&
      previous_resolution_contained_addresses_) {
    trace_strings->push_back("Address list became empty");
  } else if (resolution_contains_addresses &&
             !previous_resolution_contained_addresses_) {
    trace_strings->push_back("Address list became non-empty");
  }
  previous_resolution_contained_addresses_ = resolution_contains_addresses;
}

void ResolvingLoadBalancingPolicy::ConcatenateAndAddChannelTraceLocked(
    const TraceStringVector& trace_strings) const {
  if (!trace_strings.empty()) {
    std::string message =
        absl::StrCat("Resolution event: ", absl::StrJoin(trace_strings, ", "));
    channel_control_helper()->AddTraceEvent(ChannelControlHelper::TRACE_INFO,
                                            message);
  }
}

void ResolvingLoadBalancingPolicy::OnResolverResultChangedLocked(
    Resolver::Result result) {
  // Handle race conditions.
  if (resolver_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(*tracer_)) {
    gpr_log(GPR_INFO, "resolving_lb=%p: got resolver result", this);
  }
  // We only want to trace the address resolution in the follow cases:
  // (a) Address resolution resulted in service config change.
  // (b) Address resolution that causes number of backends to go from
  //     zero to non-zero.
  // (c) Address resolution that causes number of backends to go from
  //     non-zero to zero.
  // (d) Address resolution that causes a new LB policy to be created.
  //
  // We track a list of strings to eventually be concatenated and traced.
  TraceStringVector trace_strings;
  MaybeAddTraceMessagesForAddressChangesLocked(!result.addresses.empty(),
                                               &trace_strings);
  // The result of grpc_error_string() is owned by the error itself.
  // We're storing that string in trace_strings, so we need to make sure
  // that the error lives until we're done with the string.
  grpc_error* service_config_error =
      GRPC_ERROR_REF(result.service_config_error);
  if (service_config_error != GRPC_ERROR_NONE) {
    trace_strings.push_back(grpc_error_string(service_config_error));
  }
  // Choose the service config.
  ChannelConfigHelper::ChooseServiceConfigResult service_config_result;
  if (helper_ != nullptr) {
    service_config_result = helper_->ChooseServiceConfig(result);
  } else {
    service_config_result.lb_policy_config = child_lb_config_;
  }
  if (service_config_result.no_valid_service_config) {
    // We received an invalid service config and we don't have a
    // previous service config to fall back to.
    OnResolverError(GRPC_ERROR_REF(service_config_error));
    trace_strings.push_back("no valid service config");
  } else {
    // Create or update LB policy, as needed.
    CreateOrUpdateLbPolicyLocked(
        std::move(service_config_result.lb_policy_config), std::move(result));
    if (service_config_result.service_config_changed) {
      // Tell channel to start using new service config for calls.
      // This needs to happen after the LB policy has been updated, since
      // the ConfigSelector may need the LB policy to know about new
      // destinations before it can send RPCs to those destinations.
      if (helper_ != nullptr) helper_->StartUsingServiceConfigForCalls();
      // TODO(ncteisen): might be worth somehow including a snippet of the
      // config in the trace, at the risk of bloating the trace logs.
      trace_strings.push_back("Service config changed");
    }
  }
  // Add channel trace event.
  ConcatenateAndAddChannelTraceLocked(trace_strings);
  GRPC_ERROR_UNREF(service_config_error);
}

}  // namespace grpc_core
