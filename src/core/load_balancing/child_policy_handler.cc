//
// Copyright 2018 gRPC authors.
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

#include "src/core/load_balancing/child_policy_handler.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/util/debug_location.h"

namespace grpc_core {

//
// ChildPolicyHandler::Helper
//

class ChildPolicyHandler::Helper final
    : public LoadBalancingPolicy::ParentOwningDelegatingChannelControlHelper<
          ChildPolicyHandler> {
 public:
  explicit Helper(RefCountedPtr<ChildPolicyHandler> parent)
      : ParentOwningDelegatingChannelControlHelper(std::move(parent)) {}

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& per_address_args,
      const ChannelArgs& args) override {
    if (parent()->shutting_down_) return nullptr;
    if (!CalledByCurrentChild() && !CalledByPendingChild()) return nullptr;
    return parent()->channel_control_helper()->CreateSubchannel(
        address, per_address_args, args);
  }

  void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                   RefCountedPtr<SubchannelPicker> picker) override {
    if (parent()->shutting_down_) return;
    // If this request is from the pending child policy, ignore it until
    // it reports something other than CONNECTING, at which point we swap it
    // into place.
    if (CalledByPendingChild()) {
      if (GRPC_TRACE_FLAG_ENABLED_OBJ(*(parent()->tracer_))) {
        LOG(INFO) << "[child_policy_handler " << parent() << "] helper " << this
                  << ": pending child policy " << child_
                  << " reports state=" << ConnectivityStateName(state) << " ("
                  << status << ")";
      }
      if (state == GRPC_CHANNEL_CONNECTING) return;
      grpc_pollset_set_del_pollset_set(
          parent()->child_policy_->interested_parties(),
          parent()->interested_parties());
      parent()->child_policy_ = std::move(parent()->pending_child_policy_);
    } else if (!CalledByCurrentChild()) {
      // This request is from an outdated child, so ignore it.
      return;
    }
    parent()->channel_control_helper()->UpdateState(state, status,
                                                    std::move(picker));
  }

  void RequestReresolution() override {
    if (parent()->shutting_down_) return;
    // Only forward re-resolution requests from the most recent child,
    // since that's the one that will be receiving any update we receive
    // from the resolver.
    const LoadBalancingPolicy* latest_child_policy =
        parent()->pending_child_policy_ != nullptr
            ? parent()->pending_child_policy_.get()
            : parent()->child_policy_.get();
    if (child_ != latest_child_policy) return;
    if (GRPC_TRACE_FLAG_ENABLED_OBJ(*(parent()->tracer_))) {
      LOG(INFO) << "[child_policy_handler " << parent()
                << "] requesting re-resolution";
    }
    parent()->channel_control_helper()->RequestReresolution();
  }

  void AddTraceEvent(TraceSeverity severity,
                     absl::string_view message) override {
    if (parent()->shutting_down_) return;
    if (!CalledByPendingChild() && !CalledByCurrentChild()) return;
    parent()->channel_control_helper()->AddTraceEvent(severity, message);
  }

  void set_child(LoadBalancingPolicy* child) { child_ = child; }

 private:
  bool CalledByPendingChild() const {
    CHECK_NE(child_, nullptr);
    return child_ == parent()->pending_child_policy_.get();
  }

  bool CalledByCurrentChild() const {
    CHECK_NE(child_, nullptr);
    return child_ == parent()->child_policy_.get();
  };

  LoadBalancingPolicy* child_ = nullptr;
};

//
// ChildPolicyHandler
//

void ChildPolicyHandler::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
    LOG(INFO) << "[child_policy_handler " << this << "] shutting down";
  }
  shutting_down_ = true;
  if (child_policy_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
      LOG(INFO) << "[child_policy_handler " << this
                << "] shutting down lb_policy " << child_policy_.get();
    }
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  if (pending_child_policy_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
      LOG(INFO) << "[child_policy_handler " << this
                << "] shutting down pending lb_policy "
                << pending_child_policy_.get();
    }
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(), interested_parties());
    pending_child_policy_.reset();
  }
}

absl::Status ChildPolicyHandler::UpdateLocked(UpdateArgs args) {
  // If the child policy name changes, we need to create a new child
  // policy.  When this happens, we leave child_policy_ as-is and store
  // the new child policy in pending_child_policy_.  Once the new child
  // policy transitions into state READY, we swap it into child_policy_,
  // replacing the original child policy.  So pending_child_policy_ is
  // non-null only between when we apply an update that changes the child
  // policy name and when the new child reports state READY.
  //
  // Updates can arrive at any point during this transition.  We always
  // apply updates relative to the most recently created child policy,
  // even if the most recent one is still in pending_child_policy_.  This
  // is true both when applying the updates to an existing child policy
  // and when determining whether we need to create a new policy.
  //
  // As a result of this, there are several cases to consider here:
  //
  // 1. We have no existing child policy (i.e., this is the first update
  //    we receive after being created; in this case, both child_policy_
  //    and pending_child_policy_ are null).  In this case, we create a
  //    new child policy and store it in child_policy_.
  //
  // 2. We have an existing child policy and have no pending child policy
  //    from a previous update (i.e., either there has not been a
  //    previous update that changed the policy name, or we have already
  //    finished swapping in the new policy; in this case, child_policy_
  //    is non-null but pending_child_policy_ is null).  In this case:
  //    a. If going from the current config to the new config does not
  //       require a new policy, then we update the existing child policy.
  //    b. If going from the current config to the new config does require a
  //       new policy, we create a new policy.  The policy will be stored in
  //       pending_child_policy_ and will later be swapped into
  //       child_policy_ by the helper when the new child transitions
  //       into state READY.
  //
  // 3. We have an existing child policy and have a pending child policy
  //    from a previous update (i.e., a previous update set
  //    pending_child_policy_ as per case 2b above and that policy has
  //    not yet transitioned into state READY and been swapped into
  //    child_policy_; in this case, both child_policy_ and
  //    pending_child_policy_ are non-null).  In this case:
  //    a. If going from the current config to the new config does not
  //       require a new policy, then we update the existing pending
  //       child policy.
  //    b. If going from the current config to the new config does require a
  //       new child policy, then we create a new policy.  The new
  //       policy is stored in pending_child_policy_ (replacing the one
  //       that was there before, which will be immediately shut down)
  //       and will later be swapped into child_policy_ by the helper
  //       when the new child transitions into state READY.
  const bool create_policy =
      // case 1
      child_policy_ == nullptr ||
      // cases 2b and 3b
      ConfigChangeRequiresNewPolicyInstance(current_config_.get(),
                                            args.config.get());
  current_config_ = args.config;
  LoadBalancingPolicy* policy_to_update = nullptr;
  if (create_policy) {
    // Cases 1, 2b, and 3b: create a new child policy.
    // If child_policy_ is null, we set it (case 1), else we set
    // pending_child_policy_ (cases 2b and 3b).
    // TODO(roth): In cases 2b and 3b, we should start a timer here, so
    // that there's an upper bound on the amount of time it takes us to
    // switch to the new policy, even if the new policy stays in
    // CONNECTING for a very long period of time.
    if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
      LOG(INFO) << "[child_policy_handler " << this << "] creating new "
                << (child_policy_ == nullptr ? "" : "pending ")
                << "child policy " << args.config->name();
    }
    auto& lb_policy =
        child_policy_ == nullptr ? child_policy_ : pending_child_policy_;
    lb_policy = CreateChildPolicy(args.config->name(), args.args);
    policy_to_update = lb_policy.get();
  } else {
    // Cases 2a and 3a: update an existing policy.
    // If we have a pending child policy, send the update to the pending
    // policy (case 3a), else send it to the current policy (case 2a).
    policy_to_update = pending_child_policy_ != nullptr
                           ? pending_child_policy_.get()
                           : child_policy_.get();
  }
  CHECK_NE(policy_to_update, nullptr);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
    LOG(INFO) << "[child_policy_handler " << this << "] updating "
              << (policy_to_update == pending_child_policy_.get() ? "pending "
                                                                  : "")
              << "child policy " << policy_to_update;
  }
  return policy_to_update->UpdateLocked(std::move(args));
}

void ChildPolicyHandler::ExitIdleLocked() {
  if (child_policy_ != nullptr) {
    child_policy_->ExitIdleLocked();
    if (pending_child_policy_ != nullptr) {
      pending_child_policy_->ExitIdleLocked();
    }
  }
}

void ChildPolicyHandler::ResetBackoffLocked() {
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
    if (pending_child_policy_ != nullptr) {
      pending_child_policy_->ResetBackoffLocked();
    }
  }
}

OrphanablePtr<LoadBalancingPolicy> ChildPolicyHandler::CreateChildPolicy(
    absl::string_view child_policy_name, const ChannelArgs& args) {
  Helper* helper =
      new Helper(RefAsSubclass<ChildPolicyHandler>(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  lb_policy_args.args = args;
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      CreateLoadBalancingPolicy(child_policy_name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    LOG(ERROR) << "could not create LB policy \"" << child_policy_name << "\"";
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
    LOG(INFO) << "[child_policy_handler " << this
              << "] created new LB policy \"" << child_policy_name << "\" ("
              << lb_policy.get() << ")";
  }
  channel_control_helper()->AddTraceEvent(
      ChannelControlHelper::TRACE_INFO,
      absl::StrCat("Created new LB policy \"", child_policy_name, "\""));
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

bool ChildPolicyHandler::ConfigChangeRequiresNewPolicyInstance(
    LoadBalancingPolicy::Config* old_config,
    LoadBalancingPolicy::Config* new_config) const {
  return old_config->name() != new_config->name();
}

OrphanablePtr<LoadBalancingPolicy>
ChildPolicyHandler::CreateLoadBalancingPolicy(
    absl::string_view name, LoadBalancingPolicy::Args args) const {
  return CoreConfiguration::Get()
      .lb_policy_registry()
      .CreateLoadBalancingPolicy(name, std::move(args));
}

}  // namespace grpc_core
