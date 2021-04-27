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

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <limits.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

TraceFlag grpc_lb_priority_trace(false, "priority_lb");

namespace {

constexpr char kPriority[] = "priority_experimental";

// How long we keep a child around for after it is no longer being used
// (either because it has been removed from the config or because we
// have switched to a higher-priority child).
constexpr int kChildRetentionIntervalMs = 15 * 60 * 1000;

// Default for how long we wait for a newly created child to get connected
// before starting to attempt the next priority.  Overridable via channel arg.
constexpr int kDefaultChildFailoverTimeoutMs = 10000;

// Config for priority LB policy.
class PriorityLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct PriorityLbChild {
    RefCountedPtr<LoadBalancingPolicy::Config> config;
    bool ignore_reresolution_requests = false;
  };

  PriorityLbConfig(std::map<std::string, PriorityLbChild> children,
                   std::vector<std::string> priorities)
      : children_(std::move(children)), priorities_(std::move(priorities)) {}

  const char* name() const override { return kPriority; }

  const std::map<std::string, PriorityLbChild>& children() const {
    return children_;
  }
  const std::vector<std::string>& priorities() const { return priorities_; }

 private:
  const std::map<std::string, PriorityLbChild> children_;
  const std::vector<std::string> priorities_;
};

// priority LB policy.
class PriorityLb : public LoadBalancingPolicy {
 public:
  explicit PriorityLb(Args args);

  const char* name() const override { return kPriority; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // Each ChildPriority holds a ref to the PriorityLb.
  class ChildPriority : public InternallyRefCounted<ChildPriority> {
   public:
    ChildPriority(RefCountedPtr<PriorityLb> priority_policy, std::string name);

    ~ChildPriority() override {
      priority_policy_.reset(DEBUG_LOCATION, "ChildPriority");
    }

    const std::string& name() const { return name_; }

    void UpdateLocked(RefCountedPtr<LoadBalancingPolicy::Config> config,
                      bool ignore_reresolution_requests);
    void ExitIdleLocked();
    void ResetBackoffLocked();
    void DeactivateLocked();
    void MaybeReactivateLocked();
    void MaybeCancelFailoverTimerLocked();

    void Orphan() override;

    std::unique_ptr<SubchannelPicker> GetPicker() {
      return absl::make_unique<RefCountedPickerWrapper>(picker_wrapper_);
    }

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    const absl::Status& connectivity_status() const {
      return connectivity_status_;
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
      PickResult Pick(PickArgs args) { return picker_->Pick(args); }

     private:
      std::unique_ptr<SubchannelPicker> picker_;
    };

    // A non-ref-counted wrapper for RefCountedPicker.
    class RefCountedPickerWrapper : public SubchannelPicker {
     public:
      explicit RefCountedPickerWrapper(RefCountedPtr<RefCountedPicker> picker)
          : picker_(std::move(picker)) {}
      PickResult Pick(PickArgs args) override { return picker_->Pick(args); }

     private:
      RefCountedPtr<RefCountedPicker> picker_;
    };

    class Helper : public ChannelControlHelper {
     public:
      explicit Helper(RefCountedPtr<ChildPriority> priority)
          : priority_(std::move(priority)) {}

      ~Helper() override { priority_.reset(DEBUG_LOCATION, "Helper"); }

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          ServerAddress address, const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      void AddTraceEvent(TraceSeverity severity,
                         absl::string_view message) override;

     private:
      RefCountedPtr<ChildPriority> priority_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const grpc_channel_args* args);

    void OnConnectivityStateUpdateLocked(
        grpc_connectivity_state state, const absl::Status& status,
        std::unique_ptr<SubchannelPicker> picker);

    void StartFailoverTimerLocked();

    static void OnFailoverTimer(void* arg, grpc_error_handle error);
    void OnFailoverTimerLocked(grpc_error_handle error);
    static void OnDeactivationTimer(void* arg, grpc_error_handle error);
    void OnDeactivationTimerLocked(grpc_error_handle error);

    RefCountedPtr<PriorityLb> priority_policy_;
    const std::string name_;
    bool ignore_reresolution_requests_ = false;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_CONNECTING;
    absl::Status connectivity_status_;
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

  ~PriorityLb() override;

  void ShutdownLocked() override;

  // Returns UINT32_MAX if child is not in current priority list.
  uint32_t GetChildPriorityLocked(const std::string& child_name) const;

  void HandleChildConnectivityStateChangeLocked(ChildPriority* child);
  void DeleteChild(ChildPriority* child);

  void TryNextPriorityLocked(bool report_connecting);
  void SelectPriorityLocked(uint32_t priority);

  const int child_failover_timeout_ms_;

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<PriorityLbConfig> config_;
  HierarchicalAddressMap addresses_;

  // Internal state.
  bool shutting_down_ = false;

  std::map<std::string, OrphanablePtr<ChildPriority>> children_;
  // The priority that is being used.
  uint32_t current_priority_ = UINT32_MAX;
  // Points to the current child from before the most recent update.
  // We will continue to use this child until we decide which of the new
  // children to use.
  ChildPriority* current_child_from_before_update_ = nullptr;
};

//
// PriorityLb
//

PriorityLb::PriorityLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      child_failover_timeout_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_PRIORITY_FAILOVER_TIMEOUT_MS,
          {kDefaultChildFailoverTimeoutMs, 0, INT_MAX})) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] created", this);
  }
}

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

void PriorityLb::ExitIdleLocked() {
  if (current_priority_ != UINT32_MAX) {
    const std::string& child_name = config_->priorities()[current_priority_];
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] exiting IDLE for current priority %d child %s",
              this, current_priority_, child_name.c_str());
    }
    children_[child_name]->ExitIdleLocked();
  }
}

void PriorityLb::ResetBackoffLocked() {
  for (const auto& p : children_) p.second->ResetBackoffLocked();
}

void PriorityLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] received update", this);
  }
  // Save current child.
  if (current_priority_ != UINT32_MAX) {
    const std::string& child_name = config_->priorities()[current_priority_];
    current_child_from_before_update_ = children_[child_name].get();
    // Unset current_priority_, since it was an index into the old
    // config's priority list and may no longer be valid.  It will be
    // reset later by TryNextPriorityLocked(), but we unset it here in
    // case updating any of our children triggers a state update.
    current_priority_ = UINT32_MAX;
  }
  // Update config.
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  // Update addresses.
  addresses_ = MakeHierarchicalAddressMap(args.addresses);
  // Check all existing children against the new config.
  for (const auto& p : children_) {
    const std::string& child_name = p.first;
    auto& child = p.second;
    auto config_it = config_->children().find(child_name);
    if (config_it == config_->children().end()) {
      // Existing child not found in new config.  Deactivate it.
      child->DeactivateLocked();
    } else {
      // Existing child found in new config.  Update it.
      child->UpdateLocked(config_it->second.config,
                          config_it->second.ignore_reresolution_requests);
    }
  }
  // Try to get connected.
  TryNextPriorityLocked(/*report_connecting=*/children_.empty());
}

uint32_t PriorityLb::GetChildPriorityLocked(
    const std::string& child_name) const {
  for (uint32_t priority = 0; priority < config_->priorities().size();
       ++priority) {
    if (config_->priorities()[priority] == child_name) return priority;
  }
  return UINT32_MAX;
}

void PriorityLb::HandleChildConnectivityStateChangeLocked(
    ChildPriority* child) {
  // Special case for the child that was the current child before the
  // most recent update.
  if (child == current_child_from_before_update_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] state update for current child from before "
              "config update",
              this);
    }
    if (child->connectivity_state() == GRPC_CHANNEL_READY ||
        child->connectivity_state() == GRPC_CHANNEL_IDLE) {
      // If it's still READY or IDLE, we stick with this child, so pass
      // the new picker up to our parent.
      channel_control_helper()->UpdateState(child->connectivity_state(),
                                            child->connectivity_status(),
                                            child->GetPicker());
    } else {
      // If it's no longer READY or IDLE, we should stop using it.
      // We already started trying other priorities as a result of the
      // update, but calling TryNextPriorityLocked() ensures that we will
      // properly select between CONNECTING and TRANSIENT_FAILURE as the
      // new state to report to our parent.
      current_child_from_before_update_ = nullptr;
      TryNextPriorityLocked(/*report_connecting=*/true);
    }
    return;
  }
  // Otherwise, find the child's priority.
  uint32_t child_priority = GetChildPriorityLocked(child->name());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] state update for priority %u, child %s, current "
            "priority %u",
            this, child_priority, child->name().c_str(), current_priority_);
  }
  // Ignore priorities not in the current config.
  if (child_priority == UINT32_MAX) return;
  // Ignore lower-than-current priorities.
  if (child_priority > current_priority_) return;
  // If a child reports TRANSIENT_FAILURE, start trying the next priority.
  // Note that even if this is for a higher-than-current priority, we
  // may still need to create some children between this priority and
  // the current one (e.g., if we got an update that inserted new
  // priorities ahead of the current one).
  if (child->connectivity_state() == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    TryNextPriorityLocked(
        /*report_connecting=*/child_priority == current_priority_);
    return;
  }
  // The update is for a higher-than-current priority (or for any
  // priority if we don't have any current priority).
  if (child_priority < current_priority_) {
    // If the child reports READY or IDLE, switch to that priority.
    // Otherwise, ignore the update.
    if (child->connectivity_state() == GRPC_CHANNEL_READY ||
        child->connectivity_state() == GRPC_CHANNEL_IDLE) {
      SelectPriorityLocked(child_priority);
    }
    return;
  }
  // The current priority has returned a new picker, so pass it up to
  // our parent.
  channel_control_helper()->UpdateState(child->connectivity_state(),
                                        child->connectivity_status(),
                                        child->GetPicker());
}

void PriorityLb::DeleteChild(ChildPriority* child) {
  // If this was the current child from before the most recent update,
  // stop using it.  We already started trying other priorities as a
  // result of the update, but calling TryNextPriorityLocked() ensures that
  // we will properly select between CONNECTING and TRANSIENT_FAILURE as the
  // new state to report to our parent.
  if (current_child_from_before_update_ == child) {
    current_child_from_before_update_ = nullptr;
    TryNextPriorityLocked(/*report_connecting=*/true);
  }
  children_.erase(child->name());
}

void PriorityLb::TryNextPriorityLocked(bool report_connecting) {
  current_priority_ = UINT32_MAX;
  for (uint32_t priority = 0; priority < config_->priorities().size();
       ++priority) {
    // If the child for the priority does not exist yet, create it.
    const std::string& child_name = config_->priorities()[priority];
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO, "[priority_lb %p] trying priority %u, child %s", this,
              priority, child_name.c_str());
    }
    auto& child = children_[child_name];
    if (child == nullptr) {
      if (report_connecting) {
        channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, absl::Status(),
            absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker")));
      }
      child = MakeOrphanable<ChildPriority>(
          Ref(DEBUG_LOCATION, "ChildPriority"), child_name);
      auto child_config = config_->children().find(child_name);
      GPR_DEBUG_ASSERT(child_config != config_->children().end());
      child->UpdateLocked(child_config->second.config,
                          child_config->second.ignore_reresolution_requests);
      return;
    }
    // The child already exists.
    child->MaybeReactivateLocked();
    // If the child is in state READY or IDLE, switch to it.
    if (child->connectivity_state() == GRPC_CHANNEL_READY ||
        child->connectivity_state() == GRPC_CHANNEL_IDLE) {
      SelectPriorityLocked(priority);
      return;
    }
    // Child is not READY or IDLE.
    // If its failover timer is still pending, give it time to fire.
    if (child->failover_timer_callback_pending()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
        gpr_log(GPR_INFO,
                "[priority_lb %p] priority %u, child %s: child still "
                "attempting to connect, will wait",
                this, priority, child_name.c_str());
      }
      if (report_connecting) {
        channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, absl::Status(),
            absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker")));
      }
      return;
    }
    // Child has been failing for a while.  Move on to the next priority.
  }
  // If there are no more priorities to try, report TRANSIENT_FAILURE.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] no priority reachable, putting channel in "
            "TRANSIENT_FAILURE",
            this);
  }
  current_child_from_before_update_ = nullptr;
  grpc_error_handle error = grpc_error_set_int(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("no ready priority"),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, grpc_error_to_absl_status(error),
      absl::make_unique<TransientFailurePicker>(error));
}

void PriorityLb::SelectPriorityLocked(uint32_t priority) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] selected priority %u, child %s", this,
            priority, config_->priorities()[priority].c_str());
  }
  current_priority_ = priority;
  current_child_from_before_update_ = nullptr;
  // Deactivate lower priorities.
  for (uint32_t p = priority + 1; p < config_->priorities().size(); ++p) {
    const std::string& child_name = config_->priorities()[p];
    auto it = children_.find(child_name);
    if (it != children_.end()) it->second->DeactivateLocked();
  }
  // Update picker.
  auto& child = children_[config_->priorities()[priority]];
  channel_control_helper()->UpdateState(child->connectivity_state(),
                                        child->connectivity_status(),
                                        child->GetPicker());
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
  GRPC_CLOSURE_INIT(&on_deactivation_timer_, OnDeactivationTimer, this,
                    grpc_schedule_on_exec_ctx);
  // Start the failover timer.
  StartFailoverTimerLocked();
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
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_wrapper_.reset();
  if (deactivation_timer_callback_pending_) {
    grpc_timer_cancel(&deactivation_timer_);
  }
  Unref(DEBUG_LOCATION, "ChildPriority+Orphan");
}

void PriorityLb::ChildPriority::UpdateLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> config,
    bool ignore_reresolution_requests) {
  if (priority_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] child %s (%p): start update",
            priority_policy_.get(), name_.c_str(), this);
  }
  ignore_reresolution_requests_ = ignore_reresolution_requests;
  // Create policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(priority_policy_->args_);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = std::move(config);
  update_args.addresses = priority_policy_->addresses_[name_];
  update_args.args = grpc_channel_args_copy(priority_policy_->args_);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): updating child policy handler %p",
            priority_policy_.get(), name_.c_str(), this, child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy>
PriorityLb::ChildPriority::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = priority_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_lb_priority_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): created new child policy "
            "handler %p",
            priority_policy_.get(), name_.c_str(), this, lb_policy.get());
  }
  // Add the parent's interested_parties pollset_set to that of the newly
  // created child policy. This will make the child policy progress upon
  // activity on the parent LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   priority_policy_->interested_parties());
  return lb_policy;
}

void PriorityLb::ChildPriority::ExitIdleLocked() {
  if (connectivity_state_ == GRPC_CHANNEL_IDLE &&
      !failover_timer_callback_pending_) {
    StartFailoverTimerLocked();
  }
  child_policy_->ExitIdleLocked();
}

void PriorityLb::ChildPriority::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
}

void PriorityLb::ChildPriority::OnConnectivityStateUpdateLocked(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): state update: %s (%s) picker %p",
            priority_policy_.get(), name_.c_str(), this,
            ConnectivityStateName(state), status.ToString().c_str(),
            picker.get());
  }
  // Store the state and picker.
  connectivity_state_ = state;
  connectivity_status_ = status;
  picker_wrapper_ = MakeRefCounted<RefCountedPicker>(std::move(picker));
  // If READY or TRANSIENT_FAILURE, cancel failover timer.
  if (state == GRPC_CHANNEL_READY || state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    MaybeCancelFailoverTimerLocked();
  }
  // Notify the parent policy.
  priority_policy_->HandleChildConnectivityStateChangeLocked(this);
}

void PriorityLb::ChildPriority::StartFailoverTimerLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): starting failover timer for %d ms",
            priority_policy_.get(), name_.c_str(), this,
            priority_policy_->child_failover_timeout_ms_);
  }
  Ref(DEBUG_LOCATION, "ChildPriority+OnFailoverTimerLocked").release();
  grpc_timer_init(
      &failover_timer_,
      ExecCtx::Get()->Now() + priority_policy_->child_failover_timeout_ms_,
      &on_failover_timer_);
  failover_timer_callback_pending_ = true;
}

void PriorityLb::ChildPriority::MaybeCancelFailoverTimerLocked() {
  if (failover_timer_callback_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): cancelling failover timer",
              priority_policy_.get(), name_.c_str(), this);
    }
    grpc_timer_cancel(&failover_timer_);
    failover_timer_callback_pending_ = false;
  }
}

void PriorityLb::ChildPriority::OnFailoverTimer(void* arg,
                                                grpc_error_handle error) {
  ChildPriority* self = static_cast<ChildPriority*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  self->priority_policy_->work_serializer()->Run(
      [self, error]() { self->OnFailoverTimerLocked(error); }, DEBUG_LOCATION);
}

void PriorityLb::ChildPriority::OnFailoverTimerLocked(grpc_error_handle error) {
  if (error == GRPC_ERROR_NONE && failover_timer_callback_pending_ &&
      !priority_policy_->shutting_down_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): failover timer fired, "
              "reporting TRANSIENT_FAILURE",
              priority_policy_.get(), name_.c_str(), this);
    }
    failover_timer_callback_pending_ = false;
    OnConnectivityStateUpdateLocked(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::Status(absl::StatusCode::kUnavailable, "failover timer fired"),
        nullptr);
  }
  Unref(DEBUG_LOCATION, "ChildPriority+OnFailoverTimerLocked");
  GRPC_ERROR_UNREF(error);
}

void PriorityLb::ChildPriority::DeactivateLocked() {
  // If already deactivated, don't do it again.
  if (deactivation_timer_callback_pending_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): deactivating -- will remove in %d "
            "ms.",
            priority_policy_.get(), name_.c_str(), this,
            kChildRetentionIntervalMs);
  }
  MaybeCancelFailoverTimerLocked();
  // Start a timer to delete the child.
  Ref(DEBUG_LOCATION, "ChildPriority+timer").release();
  grpc_timer_init(&deactivation_timer_,
                  ExecCtx::Get()->Now() + kChildRetentionIntervalMs,
                  &on_deactivation_timer_);
  deactivation_timer_callback_pending_ = true;
}

void PriorityLb::ChildPriority::MaybeReactivateLocked() {
  if (deactivation_timer_callback_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO, "[priority_lb %p] child %s (%p): reactivating",
              priority_policy_.get(), name_.c_str(), this);
    }
    deactivation_timer_callback_pending_ = false;
    grpc_timer_cancel(&deactivation_timer_);
  }
}

void PriorityLb::ChildPriority::OnDeactivationTimer(void* arg,
                                                    grpc_error_handle error) {
  ChildPriority* self = static_cast<ChildPriority*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  self->priority_policy_->work_serializer()->Run(
      [self, error]() { self->OnDeactivationTimerLocked(error); },
      DEBUG_LOCATION);
}

void PriorityLb::ChildPriority::OnDeactivationTimerLocked(
    grpc_error_handle error) {
  if (error == GRPC_ERROR_NONE && deactivation_timer_callback_pending_ &&
      !priority_policy_->shutting_down_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): deactivation timer fired, "
              "deleting child",
              priority_policy_.get(), name_.c_str(), this);
    }
    deactivation_timer_callback_pending_ = false;
    priority_policy_->DeleteChild(this);
  }
  Unref(DEBUG_LOCATION, "ChildPriority+timer");
  GRPC_ERROR_UNREF(error);
}

//
// PriorityLb::ChildPriority::Helper
//

void PriorityLb::ChildPriority::Helper::RequestReresolution() {
  if (priority_->priority_policy_->shutting_down_) return;
  if (priority_->ignore_reresolution_requests_) {
    return;
  }
  priority_->priority_policy_->channel_control_helper()->RequestReresolution();
}

RefCountedPtr<SubchannelInterface>
PriorityLb::ChildPriority::Helper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (priority_->priority_policy_->shutting_down_) return nullptr;
  return priority_->priority_policy_->channel_control_helper()
      ->CreateSubchannel(std::move(address), args);
}

void PriorityLb::ChildPriority::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (priority_->priority_policy_->shutting_down_) return;
  // Notify the priority.
  priority_->OnConnectivityStateUpdateLocked(state, status, std::move(picker));
}

void PriorityLb::ChildPriority::Helper::AddTraceEvent(
    TraceSeverity severity, absl::string_view message) {
  if (priority_->priority_policy_->shutting_down_) return;
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
      const Json& json, grpc_error_handle* error) const override {
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
    std::vector<grpc_error_handle> error_list;
    // Children.
    std::map<std::string, PriorityLbConfig::PriorityLbChild> children;
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
                           " error:should be type object")
                  .c_str()));
        } else {
          auto it2 = element.object_value().find("config");
          if (it2 == element.object_value().end()) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat("field:children key:", child_name,
                             " error:missing 'config' field")
                    .c_str()));
          } else {
            grpc_error_handle parse_error = GRPC_ERROR_NONE;
            auto config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
                it2->second, &parse_error);
            bool ignore_resolution_requests = false;
            // If present, ignore_reresolution_requests must be of type
            // boolean.
            auto it3 =
                element.object_value().find("ignore_reresolution_requests");
            if (it3 != element.object_value().end()) {
              if (it3->second.type() == Json::Type::JSON_TRUE) {
                ignore_resolution_requests = true;
              } else if (it3->second.type() != Json::Type::JSON_FALSE) {
                error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                    absl::StrCat("field:children key:", child_name,
                                 " field:ignore_reresolution_requests:should "
                                 "be type boolean")
                        .c_str()));
              }
            }
            if (config == nullptr) {
              GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
              error_list.push_back(
                  GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(
                      absl::StrCat("field:children key:", child_name).c_str(),
                      &parse_error, 1));
              GRPC_ERROR_UNREF(parse_error);
            }
            children[child_name].config = std::move(config);
            children[child_name].ignore_reresolution_requests =
                ignore_resolution_requests;
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
                           " error:should be type string")
                  .c_str()));
        } else if (children.find(element.string_value()) == children.end()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:priorities element:", i,
                           " error:unknown child '", element.string_value(),
                           "'")
                  .c_str()));
        } else {
          priorities.emplace_back(element.string_value());
        }
      }
      if (priorities.size() != children.size()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("field:priorities error:priorities size (",
                         priorities.size(), ") != children size (",
                         children.size(), ")")
                .c_str()));
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
          absl::make_unique<grpc_core::PriorityLbFactory>());
}

void grpc_lb_policy_priority_shutdown() {}
