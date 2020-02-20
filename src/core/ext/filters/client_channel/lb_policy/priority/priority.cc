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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>

// FIXME: audit includes
// (not just here, but for all policies)
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/static_metadata.h"

#define GRPC_XDS_DEFAULT_LOCALITY_RETENTION_INTERVAL_MS (15 * 60 * 1000)
#define GRPC_XDS_DEFAULT_FAILOVER_TIMEOUT_MS 10000

namespace grpc_core {

TraceFlag grpc_lb_priority_trace(false, "priority_lb");

namespace {

constexpr char kPriority[] = "priority_experimental";

class PriorityLbConfig : public LoadBalancingPolicy::Config {
 public:
  PriorityLbConfig(
      std::map<std::string, RefCountedPtr<LoadBalancingPolicy::Config>>
          children,
      std::vector<std::string> priorities)
      : children_(std::move(children)),
        priorities_(std::move(priorities)) {}

  const char* name() const override { return kPriority; }

  const std::map<std::string, RefCountedPtr<LoadBalancingPolicy::Config>>&
      children() const {
    return children_;
  }
  const std::vector<std::string>& priorities() const { return priorities_; }

 private:
  const std::map<std::string, RefCountedPtr<LoadBalancingPolicy::Config>>
      children_;
  const std::vector<std::string> priorities_;
};

class PriorityLb : public LoadBalancingPolicy {
 public:
  explicit PriorityLb(Args args);

  const char* name() const override { return kPriority; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Each ChildPriority holds a ref to the PriorityLb.
  class ChildPriority : public InternallyRefCounted<ChildPriority> {
   public:
    ChildPriority(RefCountedPtr<PriorityLb> priority_policy, std::string name);

    ~ChildPriority() {
      priority_policy_.reset(DEBUG_LOCATION, "ChildPriority");
    }

    const std::string& name() const { return name_; }

    void UpdateLocked(RefCountedPtr<LoadBalancingPolicy::Config> config);
    void ResetBackoffLocked();
    void DeactivateLocked();
    void MaybeReactivateLocked();
    void MaybeCancelFailoverTimerLocked();

    void Orphan() override;

    std::unique_ptr<SubchannelPicker> GetPicker() {
      return grpc_core::MakeUnique<RefCountedPickerWrapper>(picker_wrapper_);
    }

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }
    bool failover_timer_callback_pending() const {
      return failover_timer_callback_pending_;
    }

   private:
    // A simple wrapper for ref-counting a picker from the child policy.
    class RefCountedPicker : public RefCounted<RefCountedPicker> {
     public:
      explicit RefCountedPicker(std::unique_ptr<SubchannelPicker> picker)
          : picker_(std::move(picker)) {}
      PickResult Pick(PickArgs args) { return picker_->Pick(std::move(args)); }
     private:
      std::unique_ptr<SubchannelPicker> picker_;
    };

    // A non-ref-counted wrapper for RefCountedPicker.
    class RefCountedPickerWrapper : public SubchannelPicker {
     public:
      explicit RefCountedPickerWrapper(RefCountedPtr<RefCountedPicker> picker)
          : picker_(std::move(picker)) {}
      PickResult Pick(PickArgs args) override {
        return picker_->Pick(std::move(args));
      }
     private:
      RefCountedPtr<RefCountedPicker> picker_;
    };

    class Helper : public ChannelControlHelper {
     public:
      explicit Helper(RefCountedPtr<ChildPriority> priority)
          : priority_(std::move(priority)) {}

      ~Helper() { priority_.reset(DEBUG_LOCATION, "Helper"); }

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      void AddTraceEvent(TraceSeverity severity, StringView message) override;
      void set_child(LoadBalancingPolicy* child) { child_ = child; }

     private:
      bool CalledByPendingChild() const;
      bool CalledByCurrentChild() const;

      RefCountedPtr<ChildPriority> priority_;
      LoadBalancingPolicy* child_ = nullptr;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const char* name, const grpc_channel_args* args);

    void OnConnectivityStateUpdate(grpc_connectivity_state state,
                                   std::unique_ptr<SubchannelPicker> picker);

    static void OnFailoverTimer(void* arg, grpc_error* error);
    static void OnFailoverTimerLocked(void* arg, grpc_error* error);
    static void OnDeactivationTimer(void* arg, grpc_error* error);
    static void OnDeactivationTimerLocked(void* arg, grpc_error* error);

    RefCountedPtr<PriorityLb> priority_policy_;
    const std::string name_;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;
    OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    RefCountedPtr<RefCountedPicker> picker_wrapper_;

    // States for delayed removal.
    grpc_timer deactivation_timer_;
    grpc_closure on_deactivation_timer_;
    bool deactivation_timer_callback_pending_ = false;

    // States of failover.
    grpc_timer failover_timer_;
    grpc_closure on_failover_timer_;
    bool failover_timer_callback_pending_ = false;
  };

  ~PriorityLb();

  void ShutdownLocked() override;

  // Returns UINT32_MAX if child is not in current priority list.
  uint32_t GetChildPriority(const std::string& child_name) const;

  void HandleChildConnectivityStateChange(ChildPriority* child);
  void DeleteChild(ChildPriority* child);

  void DeactivateChildrenNotInConfig();

  void UpdatePickerLocked();

  void TryNextPriorityLocked(uint32_t priority);
  void SwitchToHigherPriorityLocked(uint32_t priority);

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<PriorityLbConfig> config_;
  ServerAddressList addresses_;

  // Internal state.
  bool shutting_down_ = false;

  const grpc_millis child_retention_interval_ms_;
  const grpc_millis child_failover_timeout_ms_;

  std::map<std::string, OrphanablePtr<ChildPriority>> children_;
  // The priority that is being used.
  uint32_t current_priority_ = UINT32_MAX;
};

//
// PriorityLb
//

PriorityLb::PriorityLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
// FIXME: need new channel args here
      child_retention_interval_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_LOCALITY_RETENTION_INTERVAL_MS,
          {GRPC_XDS_DEFAULT_LOCALITY_RETENTION_INTERVAL_MS, 0, INT_MAX})),
      child_failover_timeout_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_XDS_FAILOVER_TIMEOUT_MS,
          {GRPC_XDS_DEFAULT_FAILOVER_TIMEOUT_MS, 0, INT_MAX})) {}

PriorityLb::~PriorityLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] destroying priority LB policy", this);
  }
  grpc_channel_args_destroy(args_);
}

void PriorityLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  children_.clear();
}

void PriorityLb::ResetBackoffLocked() {
  for (const auto& p : children_) {
    p.second->ResetBackoffLocked();
  }
}

uint32_t PriorityLb::GetChildPriority(const std::string& child_name) const {
  for (uint32_t priority = 0; priority <= config_->priorities().size();
       ++priority) {
    if (config_->priorities().at(priority) == child_name) return priority;
  }
  return UINT32_MAX;
}

void PriorityLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] Received update", this);
  }
  // Update config.
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  // Update addresses.
  addresses_ = std::move(args.addresses);
  // Unset current_priority_, since it was an index into the old
  // config's priority list and may no longer be valid.  It will be
  // reset later.
  current_priority_ = UINT32_MAX;
  // Deactivate children that are not present in the new config.
  DeactivateChildrenNotInConfig();
  // Update all existing children.
  for (const auto& p : config_->children()) {
    auto it = children_.find(p.first);
    if (it != children_.end()) {
      it->second->UpdateLocked(p.second);
    }
  }
  // Try to get connected, starting from the highest priority.
// FIXME: do we want to stick with the old current priority if it is no
// longer in the new config but we have not yet gotten another child
// connected?
  TryNextPriorityLocked(0);
}

void PriorityLb::DeactivateChildrenNotInConfig() {
  for (auto it = children_.begin(); it != children_.end();) {
    if (config_->children().find(it->first) == config_->children().end()) {
      if (child_retention_interval_ms_ == 0) {
        it = children_.erase(it);
      } else {
        it->second->DeactivateLocked();
        ++it;
      }
    }
  }
}

void PriorityLb::HandleChildConnectivityStateChange(ChildPriority* child) {
  uint32_t child_priority = GetChildPriority(child->name());
  // Ignore priorities not in the current config.
  if (child_priority == UINT32_MAX) return;
  // Ignore lower-than-current priorities.
  if (child_priority > current_priority_) return;
  // If a higher-than-current priority reports READY, switch to that priority.
  // Note that this also catches the case of current_priority_ == UINT32_MAX.
  if (child_priority < current_priority_ &&
      child->connectivity_state() == GRPC_CHANNEL_READY) {
    SwitchToHigherPriorityLocked(child_priority);
  }
  // If a child reports TRANSIENT_FAILURE, start trying the next priority.
  // Note that even if this is for a higher-than-current priority, we
  // may still need to create some children between this priority and
  // the current one (e.g., if we got an update that inserted new
  // priorities ahead of the current one).
  else if (child->connectivity_state() == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    TryNextPriorityLocked(child_priority + 1);
  }
  // At this point, one of the following things has happened to the current
  // priority.
  // 1. It remained the same (but received picker update from its child).
  // 2. It changed to a lower priority due to failover.
  // 3. It became invalid because failover didn't yield a READY priority.
  // In any case, update the picker.
  UpdatePickerLocked();
}

void PriorityLb::DeleteChild(ChildPriority* child) {
  children_.erase(child->name());
}

void PriorityLb::UpdatePickerLocked() {
  if (current_priority_ == UINT32_MAX) {
    grpc_error* error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("no ready priority"),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        grpc_core::MakeUnique<TransientFailurePicker>(error));
    return;
  }
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_READY,
      children_[config_->priorities().at(current_priority_)]->GetPicker());
}

void PriorityLb::TryNextPriorityLocked(uint32_t priority) {
  // If there are no more priorities to try, report TRANSIENT_FAILURE.
  if (priority >= config_->priorities().size()) {
    UpdatePickerLocked();
    return;
  }
  // If the child for the priority does not exist yet, create it.
  const std::string& child_name = config_->priorities().at(priority);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] start trying priority %d, child %s",
            this, priority, child_name.c_str());
  }
  auto& child = children_[child_name];
  if (child == nullptr) {
    // If this is the first child being created, report CONNECTING.
    if (children_.size() == 1) {
      channel_control_helper()->UpdateState(
          GRPC_CHANNEL_CONNECTING,
          grpc_core::MakeUnique<QueuePicker>(
              Ref(DEBUG_LOCATION, "QueuePicker")));
    }
    child = MakeOrphanable<ChildPriority>(
        Ref(DEBUG_LOCATION, "ChildPriority"), child_name);
    child->UpdateLocked(config_->children().find(child_name)->second);
// FIXME: report CONNECTING or TF here?
    return;
  }
  // The child already exists.
  child->MaybeReactivateLocked();
  // If the child is in state READY, switch to it.
  if (child->connectivity_state() == GRPC_CHANNEL_READY) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] selected priority %d, child %s",
              this, priority, child_name.c_str());
    }
    current_priority_ = priority;
    UpdatePickerLocked();
    return;
  }
  // Child is not READY.
  // If its failover timer is still pending, give it time to fire.
  if (child->failover_timer_callback_pending()) {
// FIXME: report CONNECTING or TF here?
    return;
  }
  // Child has been failing for a while.  Move on to the next priority.
  TryNextPriorityLocked(priority + 1);
}

void PriorityLb::SwitchToHigherPriorityLocked(uint32_t priority) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] switching to higher priority %d, child %s",
            this, priority, config_->priorities().at(priority).c_str());
  }
  current_priority_ = priority;
  // Deactivate lower priorities.
  for (uint32_t p = priority + 1; p < config_->priorities().size(); ++p) {
    const std::string& child_name = config_->priorities().at(p);
    auto it = children_.find(child_name);
    if (it == children_.end()) continue;
    if (child_retention_interval_ms_ == 0) {
      children_.erase(it);
    } else {
      it->second->DeactivateLocked();
    }
  }
  // Update picker.
  UpdatePickerLocked();
}

//
// PriorityLb::ChildPriority
//

PriorityLb::ChildPriority::ChildPriority(
    RefCountedPtr<PriorityLb> priority_policy, std::string name)
    : priority_policy_(std::move(priority_policy)), name_(std::move(name)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] creating child %s (%p)",
            priority_policy_.get(), name_.c_str(), this);
  }
  GRPC_CLOSURE_INIT(&on_failover_timer_, OnFailoverTimer, this,
                    grpc_schedule_on_exec_ctx);
  // Start the failover timer.
  Ref(DEBUG_LOCATION, "ChildPriority+OnFailoverTimerLocked").release();
  grpc_timer_init(
      &failover_timer_,
      ExecCtx::Get()->Now() + priority_policy_->child_failover_timeout_ms_,
      &on_failover_timer_);
  failover_timer_callback_pending_ = true;
}

void PriorityLb::ChildPriority::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] child %s (%p): orphaned",
            priority_policy_.get(), name_.c_str(), this);
  }
  MaybeCancelFailoverTimerLocked();
  if (deactivation_timer_callback_pending_) {
    grpc_timer_cancel(&deactivation_timer_);
  }
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   priority_policy_->interested_parties());
  child_policy_.reset();
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(),
        priority_policy_->interested_parties());
    pending_child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_wrapper_.reset();
  if (deactivation_timer_callback_pending_) {
    grpc_timer_cancel(&deactivation_timer_);
  }
  Unref(DEBUG_LOCATION, "ChildPriority+Orphan");
}

void PriorityLb::ChildPriority::UpdateLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> config) {
  if (priority_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] child %s (%p): start update",
            priority_policy_.get(), name_.c_str(), this);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = std::move(config);
  update_args.addresses = priority_policy_->addresses_;
  update_args.args = grpc_channel_args_copy(priority_policy_->args_);
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
  // 1. We have no existing child policy (i.e., we have started up but
  //    have not yet received a serverlist from the balancer; in this case,
  //    both child_policy_ and pending_child_policy_ are null).  In this
  //    case, we create a new child policy and store it in child_policy_.
  //
  // 2. We have an existing child policy and have no pending child policy
  //    from a previous update (i.e., either there has not been a
  //    previous update that changed the policy name, or we have already
  //    finished swapping in the new policy; in this case, child_policy_
  //    is non-null but pending_child_policy_ is null).  In this case:
  //    a. If child_policy_->name() equals child_policy_name, then we
  //       update the existing child policy.
  //    b. If child_policy_->name() does not equal child_policy_name,
  //       we create a new policy.  The policy will be stored in
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
  //    a. If pending_child_policy_->name() equals child_policy_name,
  //       then we update the existing pending child policy.
  //    b. If pending_child_policy->name() does not equal
  //       child_policy_name, then we create a new policy.  The new
  //       policy is stored in pending_child_policy_ (replacing the one
  //       that was there before, which will be immediately shut down)
  //       and will later be swapped into child_policy_ by the helper
  //       when the new child transitions into state READY.
  const char* child_policy_name = update_args.config->name();
  const bool create_policy =
      // case 1
      child_policy_ == nullptr ||
      // case 2b
      (pending_child_policy_ == nullptr &&
       strcmp(child_policy_->name(), child_policy_name) != 0) ||
      // case 3b
      (pending_child_policy_ != nullptr &&
       strcmp(pending_child_policy_->name(), child_policy_name) != 0);
  LoadBalancingPolicy* policy_to_update = nullptr;
  if (create_policy) {
    // Cases 1, 2b, and 3b: create a new child policy.
    // If child_policy_ is null, we set it (case 1), else we set
    // pending_child_policy_ (cases 2b and 3b).
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): creating new %schild policy %s",
              priority_policy_.get(), name_.c_str(), this,
              child_policy_ == nullptr ? "" : "pending ", child_policy_name);
    }
    auto& lb_policy =
        child_policy_ == nullptr ? child_policy_ : pending_child_policy_;
    lb_policy = CreateChildPolicyLocked(child_policy_name, update_args.args);
    policy_to_update = lb_policy.get();
  } else {
    // Cases 2a and 3a: update an existing policy.
    // If we have a pending child policy, send the update to the pending
    // policy (case 3a), else send it to the current policy (case 2a).
    policy_to_update = pending_child_policy_ != nullptr
                           ? pending_child_policy_.get()
                           : child_policy_.get();
  }
  GPR_ASSERT(policy_to_update != nullptr);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): updating %schild policy %p",
            priority_policy_.get(), name_.c_str(), this,
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy> PriorityLb::ChildPriority::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = new Helper(this->Ref(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = priority_policy_->combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR,
            "[priority_lb %p] child %s (%p): failure creating child policy %s",
            priority_policy_.get(), name_.c_str(), this, name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): created new child policy %s (%p)",
            priority_policy_.get(), name_.c_str(), this, name, lb_policy.get());
  }
  // Add the parent's interested_parties pollset_set to that of the newly
  // created child policy. This will make the child policy progress upon
  // activity on the parent LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   priority_policy_->interested_parties());
  return lb_policy;
}

void PriorityLb::ChildPriority::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void PriorityLb::ChildPriority::DeactivateLocked() {
  // If already deactivated, don't do it again.
  if (deactivation_timer_callback_pending_) return;
  MaybeCancelFailoverTimerLocked();
  // Start a timer to delete the child.
  Ref(DEBUG_LOCATION, "ChildPriority+timer").release();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): deactivating -- will remove in %"
            PRId64 " ms.",
            priority_policy_.get(), name_.c_str(), this,
            priority_policy_->child_retention_interval_ms_);
  }
  GRPC_CLOSURE_INIT(&on_deactivation_timer_, OnDeactivationTimer, this,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(
      &deactivation_timer_,
      ExecCtx::Get()->Now() + priority_policy_->child_retention_interval_ms_,
      &on_deactivation_timer_);
  deactivation_timer_callback_pending_ = true;
}

void PriorityLb::ChildPriority::MaybeReactivateLocked() {
  if (deactivation_timer_callback_pending_) {
    grpc_timer_cancel(&deactivation_timer_);
  }
}

void PriorityLb::ChildPriority::MaybeCancelFailoverTimerLocked() {
  if (failover_timer_callback_pending_) grpc_timer_cancel(&failover_timer_);
}

void PriorityLb::ChildPriority::OnConnectivityStateUpdate(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): state update: %s, picker %p",
            priority_policy_.get(), name_.c_str(), this,
            ConnectivityStateName(state), picker.get());
  }
  // Store the state and picker.
  connectivity_state_ = state;
  picker_wrapper_ = MakeRefCounted<RefCountedPicker>(std::move(picker));
  // If READY or TRANSIENT_FAILURE, cancel failover timer.
  if (state == GRPC_CHANNEL_READY || state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    MaybeCancelFailoverTimerLocked();
  }
  // Notify the parent policy.
  priority_policy_->HandleChildConnectivityStateChange(this);
}

void PriorityLb::ChildPriority::OnFailoverTimer(void* arg, grpc_error* error) {
  ChildPriority* self = static_cast<ChildPriority*>(arg);
  self->priority_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_failover_timer_, OnFailoverTimerLocked, self,
                        nullptr),
      GRPC_ERROR_REF(error));
}

void PriorityLb::ChildPriority::OnFailoverTimerLocked(void* arg, grpc_error* error) {
  ChildPriority* self = static_cast<ChildPriority*>(arg);
  self->failover_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->priority_policy_->shutting_down_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): failover timer fired, "
              "reporting TRANSIENT_FAILURE",
              self->priority_policy_.get(), self->name_.c_str(), self);
    }
    self->OnConnectivityStateUpdate(GRPC_CHANNEL_TRANSIENT_FAILURE, nullptr);
  }
  self->Unref(DEBUG_LOCATION, "ChildPriority+OnFailoverTimerLocked");
}

void PriorityLb::ChildPriority::OnDeactivationTimer(void* arg,
                                                      grpc_error* error) {
  ChildPriority* self = static_cast<ChildPriority*>(arg);
  self->priority_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_deactivation_timer_,
                        OnDeactivationTimerLocked, self, nullptr),
      GRPC_ERROR_REF(error));
}

void PriorityLb::ChildPriority::OnDeactivationTimerLocked(void* arg,
                                                            grpc_error* error) {
  ChildPriority* self = static_cast<ChildPriority*>(arg);
  self->deactivation_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->priority_policy_->shutting_down_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): deactivation timer fired, "
              "deleting child",
              self->priority_policy_.get(), self->name_.c_str(), self);
    }
    self->priority_policy_->DeleteChild(self);
  }
  self->Unref(DEBUG_LOCATION, "ChildPriority+timer");
}

//
// PriorityLb::ChildPriority::Helper
//

bool PriorityLb::ChildPriority::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == priority_->pending_child_policy_.get();
}

bool PriorityLb::ChildPriority::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == priority_->child_policy_.get();
}

void PriorityLb::ChildPriority::Helper::RequestReresolution() {
  if (priority_->priority_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  priority_->priority_policy_->channel_control_helper()->RequestReresolution();
}

RefCountedPtr<SubchannelInterface>
PriorityLb::ChildPriority::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (priority_->priority_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return priority_->priority_policy_->channel_control_helper()->CreateSubchannel(
      args);
}

void PriorityLb::ChildPriority::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (priority_->priority_policy_->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): helper %p: pending child "
              "policy %p reports state=%s",
              priority_->priority_policy_.get(), priority_->name_.c_str(),
              priority_.get(), this, priority_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        priority_->child_policy_->interested_parties(),
        priority_->priority_policy_->interested_parties());
    priority_->child_policy_ = std::move(priority_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Notify the priority.
  priority_->OnConnectivityStateUpdate(state, std::move(picker));
}

void PriorityLb::ChildPriority::Helper::AddTraceEvent(TraceSeverity severity,
                                              StringView message) {
  if (priority_->priority_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  priority_->priority_policy_->channel_control_helper()->AddTraceEvent(severity,
                                                                       message);
}

//
// factory
//

class PriorityLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<PriorityLb>(std::move(args));
  }

  const char* name() const override { return kPriority; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // priority was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:priority policy requires "
          "configuration. Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // Children.
    std::map<std::string, RefCountedPtr<LoadBalancingPolicy::Config>> children;
    auto it = json.object_value().find("children");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:children error:required field missing"));
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:children error:type should be object"));
    } else {
      const Json::Object& object = it->second.object_value();
      for (const auto& p : object) {
        const std::string& child_name = p.first;
        const Json& element = p.second;
        if (element.type() != Json::Type::OBJECT) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:children key:", child_name,
                           " error:should be type object").c_str()));
        } else {
          auto it2 = element.object_value().find("config");
          if (it2 == element.object_value().end()) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat("field:children key:", child_name,
                             " error:missing 'config' field").c_str()));
          } else {
            grpc_error* parse_error = GRPC_ERROR_NONE;
            auto config =
                LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
                    it2->second, &parse_error);
            if (config == nullptr) {
              GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
              error_list.push_back(
                  GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(
                      absl::StrCat("field:children key:", child_name).c_str(),
                      &parse_error, 1));
              GRPC_ERROR_UNREF(parse_error);
            }
            children[child_name] = std::move(config);
          }
        }
      }
    }
    // Priorities.
    std::vector<std::string> priorities;
    it = json.object_value().find("priorities");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:priorities error:required field missing"));
    } else if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:priorities error:type should be array"));
    } else {
      const Json::Array& array = it->second.array_value();
      for (size_t i = 0; i < array.size(); ++i) {
        const Json& element = array[i];
        if (element.type() != Json::Type::STRING) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:priorities element:", i,
                           " error:should be type string").c_str()));
        } else if (children.find(element.string_value()) == children.end()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:priorities element:", i,
                           " error:unknown child '", element.string_value(),
                           "'").c_str()));
        } else {
          priorities.emplace_back(element.string_value());
        }
      }
      if (priorities.size() != children.size()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("field:priorities error:priorities size (",
                         priorities.size(), ") != children size (",
                         children.size(), ")").c_str()));
      }
    }
    if (error_list.empty()) {
      return MakeRefCounted<PriorityLbConfig>(std::move(children),
                                              std::move(priorities));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "priority_experimental LB policy config", &error_list);
      return nullptr;
    }
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_priority_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::MakeUnique<grpc_core::PriorityLbFactory>());
}

void grpc_lb_policy_priority_shutdown() {}
