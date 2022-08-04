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
#include <stddef.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_priority_trace(false, "priority_lb");

namespace {

constexpr absl::string_view kPriority = "priority_experimental";

// How long we keep a child around for after it is no longer being used
// (either because it has been removed from the config or because we
// have switched to a higher-priority child).
constexpr Duration kChildRetentionInterval = Duration::Minutes(15);

// Default for how long we wait for a newly created child to get connected
// before starting to attempt the next priority.  Overridable via channel arg.
constexpr Duration kDefaultChildFailoverTimeout = Duration::Seconds(10);

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

  absl::string_view name() const override { return kPriority; }

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

  absl::string_view name() const override { return kPriority; }

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
    void MaybeDeactivateLocked();
    void MaybeReactivateLocked();

    void Orphan() override;

    std::unique_ptr<SubchannelPicker> GetPicker();

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    const absl::Status& connectivity_status() const {
      return connectivity_status_;
    }

    bool FailoverTimerPending() const { return failover_timer_ != nullptr; }

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
          ServerAddress address, const ChannelArgs& args) override;
      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      absl::string_view GetAuthority() override;
      void AddTraceEvent(TraceSeverity severity,
                         absl::string_view message) override;

     private:
      RefCountedPtr<ChildPriority> priority_;
    };

    class DeactivationTimer : public InternallyRefCounted<DeactivationTimer> {
     public:
      explicit DeactivationTimer(RefCountedPtr<ChildPriority> child_priority);

      void Orphan() override;

     private:
      static void OnTimer(void* arg, grpc_error_handle error);
      void OnTimerLocked(grpc_error_handle);

      RefCountedPtr<ChildPriority> child_priority_;
      grpc_timer timer_;
      grpc_closure on_timer_;
      bool timer_pending_ = true;
    };

    class FailoverTimer : public InternallyRefCounted<FailoverTimer> {
     public:
      explicit FailoverTimer(RefCountedPtr<ChildPriority> child_priority);

      void Orphan() override;

     private:
      static void OnTimer(void* arg, grpc_error_handle error);
      void OnTimerLocked(grpc_error_handle);

      RefCountedPtr<ChildPriority> child_priority_;
      grpc_timer timer_;
      grpc_closure on_timer_;
      bool timer_pending_ = true;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const ChannelArgs& args);

    void OnConnectivityStateUpdateLocked(
        grpc_connectivity_state state, const absl::Status& status,
        std::unique_ptr<SubchannelPicker> picker);

    RefCountedPtr<PriorityLb> priority_policy_;
    const std::string name_;
    bool ignore_reresolution_requests_ = false;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_CONNECTING;
    absl::Status connectivity_status_;
    RefCountedPtr<RefCountedPicker> picker_wrapper_;

    bool seen_ready_or_idle_since_transient_failure_ = true;

    OrphanablePtr<DeactivationTimer> deactivation_timer_;
    OrphanablePtr<FailoverTimer> failover_timer_;
  };

  ~PriorityLb() override;

  void ShutdownLocked() override;

  // Returns the priority of the specified child name, or UINT32_MAX if
  // the child is not in the current priority list.
  uint32_t GetChildPriorityLocked(const std::string& child_name) const;

  // Called when a child's connectivity state has changed.
  // May propagate the update to the channel or trigger choosing a new
  // priority.
  void HandleChildConnectivityStateChangeLocked(ChildPriority* child);

  // Deletes a child.  Called when the child's deactivation timer fires.
  void DeleteChild(ChildPriority* child);

  // Iterates through the list of priorities to choose one:
  // - If the child for a priority doesn't exist, creates it.
  // - If a child's failover timer is pending, selects that priority
  //   while we wait for the child to attempt to connect.
  // - If the child is connected, selects that priority.
  // - Otherwise, continues on to the next child.
  // Delegates to the last child if none of the children are connecting.
  // Reports TRANSIENT_FAILURE if the priority list is empty.
  //
  // This method is idempotent; it should yield the same result every
  // time as a function of the state of the children.
  void ChoosePriorityLocked();

  // Sets the specified priority as the current priority.
  // Deactivates any children at lower priorities.
  // Returns the child's picker to the channel.
  void SetCurrentPriorityLocked(uint32_t priority);

  const Duration child_failover_timeout_;

  // Current channel args and config from the resolver.
  ChannelArgs args_;
  RefCountedPtr<PriorityLbConfig> config_;
  absl::StatusOr<HierarchicalAddressMap> addresses_;
  std::string resolution_note_;

  // Internal state.
  bool shutting_down_ = false;

  bool update_in_progress_ = false;

  // All children that currently exist.
  // Some of these children may be in deactivated state.
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
      child_failover_timeout_(std::max(
          Duration::Zero(),
          args.args
              .GetDurationFromIntMillis(GRPC_ARG_PRIORITY_FAILOVER_TIMEOUT_MS)
              .value_or(kDefaultChildFailoverTimeout))) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] created", this);
  }
}

PriorityLb::~PriorityLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] destroying priority LB policy", this);
  }
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
    auto* child = children_[child_name].get();
    GPR_ASSERT(child != nullptr);
    if (child->connectivity_state() == GRPC_CHANNEL_READY) {
      current_child_from_before_update_ = children_[child_name].get();
    }
  }
  // Update config.
  config_ = std::move(args.config);
  // Update args.
  args_ = std::move(args.args);
  // Update addresses.
  addresses_ = MakeHierarchicalAddressMap(args.addresses);
  resolution_note_ = std::move(args.resolution_note);
  // Check all existing children against the new config.
  update_in_progress_ = true;
  for (const auto& p : children_) {
    const std::string& child_name = p.first;
    auto& child = p.second;
    auto config_it = config_->children().find(child_name);
    if (config_it == config_->children().end()) {
      // Existing child not found in new config.  Deactivate it.
      child->MaybeDeactivateLocked();
    } else {
      // Existing child found in new config.  Update it.
      child->UpdateLocked(config_it->second.config,
                          config_it->second.ignore_reresolution_requests);
    }
  }
  update_in_progress_ = false;
  // Try to get connected.
  ChoosePriorityLocked();
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
  // If we're in the process of propagating an update from our parent to
  // our children, ignore any updates that come from the children.  We
  // will instead choose a new priority once the update has been seen by
  // all children.  This ensures that we don't incorrectly do the wrong
  // thing while state is inconsistent.
  if (update_in_progress_) return;
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
      // update, but calling ChoosePriorityLocked() ensures that we will
      // properly select between CONNECTING and TRANSIENT_FAILURE as the
      // new state to report to our parent.
      current_child_from_before_update_ = nullptr;
      ChoosePriorityLocked();
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
  // Unconditionally call ChoosePriorityLocked().  It should do the
  // right thing based on the state of all children.
  ChoosePriorityLocked();
}

void PriorityLb::DeleteChild(ChildPriority* child) {
  // If this was the current child from before the most recent update,
  // stop using it.  We already started trying other priorities as a
  // result of the update, but calling ChoosePriorityLocked() ensures that
  // we will properly select between CONNECTING and TRANSIENT_FAILURE as the
  // new state to report to our parent.
  if (current_child_from_before_update_ == child) {
    current_child_from_before_update_ = nullptr;
    ChoosePriorityLocked();
  }
  children_.erase(child->name());
}

void PriorityLb::ChoosePriorityLocked() {
  // If priority list is empty, report TF.
  if (config_->priorities().empty()) {
    current_child_from_before_update_ = nullptr;
    absl::Status status =
        absl::UnavailableError("priority policy has empty priority list");
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        absl::make_unique<TransientFailurePicker>(status));
    return;
  }
  // Iterate through priorities, searching for one in READY or IDLE,
  // creating new children as needed.
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
      // If we're not still using an old child from before the last
      // update, report CONNECTING here.
      // This is probably not strictly necessary, since the child should
      // immediately report CONNECTING and cause us to report that state
      // anyway, but we do this just in case the child fails to report
      // state before UpdateLocked() returns.
      if (current_child_from_before_update_ == nullptr) {
        channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, absl::Status(),
            absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker")));
      }
      current_priority_ = priority;
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
      SetCurrentPriorityLocked(priority);
      return;
    }
    // Child is not READY or IDLE.
    // If its failover timer is still pending, give it time to fire.
    if (child->FailoverTimerPending()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
        gpr_log(GPR_INFO,
                "[priority_lb %p] priority %u, child %s: child still "
                "attempting to connect, will wait",
                this, priority, child_name.c_str());
      }
      current_priority_ = priority;
      // If we're not still using an old child from before the last
      // update, report CONNECTING here.
      if (current_child_from_before_update_ == nullptr) {
        channel_control_helper()->UpdateState(child->connectivity_state(),
                                              child->connectivity_status(),
                                              child->GetPicker());
      }
      return;
    }
    // Child has been failing for a while.  Move on to the next priority.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] skipping priority %u, child %s: state=%s, "
              "failover timer not pending",
              this, priority, child_name.c_str(),
              ConnectivityStateName(child->connectivity_state()));
    }
  }
  // If we didn't find any priority to try, pick the first one in state
  // CONNECTING.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] no priority reachable, checking for CONNECTING "
            "priority to delegate to",
            this);
  }
  for (uint32_t priority = 0; priority < config_->priorities().size();
       ++priority) {
    // If the child for the priority does not exist yet, create it.
    const std::string& child_name = config_->priorities()[priority];
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO, "[priority_lb %p] trying priority %u, child %s", this,
              priority, child_name.c_str());
    }
    auto& child = children_[child_name];
    GPR_ASSERT(child != nullptr);
    if (child->connectivity_state() == GRPC_CHANNEL_CONNECTING) {
      channel_control_helper()->UpdateState(child->connectivity_state(),
                                            child->connectivity_status(),
                                            child->GetPicker());
      return;
    }
  }
  // Did not find any child in CONNECTING, delegate to last child.
  const std::string& child_name = config_->priorities().back();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] no priority in CONNECTING, delegating to "
            "lowest priority child %s",
            this, child_name.c_str());
  }
  auto& child = children_[child_name];
  GPR_ASSERT(child != nullptr);
  channel_control_helper()->UpdateState(child->connectivity_state(),
                                        child->connectivity_status(),
                                        child->GetPicker());
}

void PriorityLb::SetCurrentPriorityLocked(uint32_t priority) {
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
    if (it != children_.end()) it->second->MaybeDeactivateLocked();
  }
  // Update picker.
  auto& child = children_[config_->priorities()[priority]];
  channel_control_helper()->UpdateState(child->connectivity_state(),
                                        child->connectivity_status(),
                                        child->GetPicker());
}

//
// PriorityLb::ChildPriority::DeactivationTimer
//

PriorityLb::ChildPriority::DeactivationTimer::DeactivationTimer(
    RefCountedPtr<PriorityLb::ChildPriority> child_priority)
    : child_priority_(std::move(child_priority)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): deactivating -- will remove in "
            "%" PRId64 "ms",
            child_priority_->priority_policy_.get(),
            child_priority_->name_.c_str(), child_priority_.get(),
            kChildRetentionInterval.millis());
  }
  GRPC_CLOSURE_INIT(&on_timer_, OnTimer, this, nullptr);
  Ref(DEBUG_LOCATION, "Timer").release();
  grpc_timer_init(&timer_, ExecCtx::Get()->Now() + kChildRetentionInterval,
                  &on_timer_);
}

void PriorityLb::ChildPriority::DeactivationTimer::Orphan() {
  if (timer_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO, "[priority_lb %p] child %s (%p): reactivating",
              child_priority_->priority_policy_.get(),
              child_priority_->name_.c_str(), child_priority_.get());
    }
    timer_pending_ = false;
    grpc_timer_cancel(&timer_);
  }
  Unref();
}

void PriorityLb::ChildPriority::DeactivationTimer::OnTimer(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<DeactivationTimer*>(arg);
  (void)GRPC_ERROR_REF(error);  // ref owned by lambda
  self->child_priority_->priority_policy_->work_serializer()->Run(
      [self, error]() { self->OnTimerLocked(error); }, DEBUG_LOCATION);
}

void PriorityLb::ChildPriority::DeactivationTimer::OnTimerLocked(
    grpc_error_handle error) {
  if (GRPC_ERROR_IS_NONE(error) && timer_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): deactivation timer fired, "
              "deleting child",
              child_priority_->priority_policy_.get(),
              child_priority_->name_.c_str(), child_priority_.get());
    }
    timer_pending_ = false;
    child_priority_->priority_policy_->DeleteChild(child_priority_.get());
  }
  Unref(DEBUG_LOCATION, "Timer");
  GRPC_ERROR_UNREF(error);
}

//
// PriorityLb::ChildPriority::FailoverTimer
//

PriorityLb::ChildPriority::FailoverTimer::FailoverTimer(
    RefCountedPtr<PriorityLb::ChildPriority> child_priority)
    : child_priority_(std::move(child_priority)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(
        GPR_INFO,
        "[priority_lb %p] child %s (%p): starting failover timer for %" PRId64
        "ms",
        child_priority_->priority_policy_.get(), child_priority_->name_.c_str(),
        child_priority_.get(),
        child_priority_->priority_policy_->child_failover_timeout_.millis());
  }
  GRPC_CLOSURE_INIT(&on_timer_, OnTimer, this, nullptr);
  Ref(DEBUG_LOCATION, "Timer").release();
  grpc_timer_init(
      &timer_,
      ExecCtx::Get()->Now() +
          child_priority_->priority_policy_->child_failover_timeout_,
      &on_timer_);
}

void PriorityLb::ChildPriority::FailoverTimer::Orphan() {
  if (timer_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): cancelling failover timer",
              child_priority_->priority_policy_.get(),
              child_priority_->name_.c_str(), child_priority_.get());
    }
    timer_pending_ = false;
    grpc_timer_cancel(&timer_);
  }
  Unref();
}

void PriorityLb::ChildPriority::FailoverTimer::OnTimer(
    void* arg, grpc_error_handle error) {
  auto* self = static_cast<FailoverTimer*>(arg);
  (void)GRPC_ERROR_REF(error);  // ref owned by lambda
  self->child_priority_->priority_policy_->work_serializer()->Run(
      [self, error]() { self->OnTimerLocked(error); }, DEBUG_LOCATION);
}

void PriorityLb::ChildPriority::FailoverTimer::OnTimerLocked(
    grpc_error_handle error) {
  if (GRPC_ERROR_IS_NONE(error) && timer_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p] child %s (%p): failover timer fired, "
              "reporting TRANSIENT_FAILURE",
              child_priority_->priority_policy_.get(),
              child_priority_->name_.c_str(), child_priority_.get());
    }
    timer_pending_ = false;
    child_priority_->OnConnectivityStateUpdateLocked(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::Status(absl::StatusCode::kUnavailable, "failover timer fired"),
        nullptr);
  }
  Unref(DEBUG_LOCATION, "Timer");
  GRPC_ERROR_UNREF(error);
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
  // Start the failover timer.
  failover_timer_ = MakeOrphanable<FailoverTimer>(Ref());
}

void PriorityLb::ChildPriority::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] child %s (%p): orphaned",
            priority_policy_.get(), name_.c_str(), this);
  }
  failover_timer_.reset();
  deactivation_timer_.reset();
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   priority_policy_->interested_parties());
  child_policy_.reset();
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_wrapper_.reset();
  Unref(DEBUG_LOCATION, "ChildPriority+Orphan");
}

std::unique_ptr<LoadBalancingPolicy::SubchannelPicker>
PriorityLb::ChildPriority::GetPicker() {
  if (picker_wrapper_ == nullptr) {
    return absl::make_unique<QueuePicker>(
        priority_policy_->Ref(DEBUG_LOCATION, "QueuePicker"));
  }
  return absl::make_unique<RefCountedPickerWrapper>(picker_wrapper_);
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
  if (priority_policy_->addresses_.ok()) {
    update_args.addresses = (*priority_policy_->addresses_)[name_];
  } else {
    update_args.addresses = priority_policy_->addresses_.status();
  }
  update_args.resolution_note = priority_policy_->resolution_note_;
  update_args.args = priority_policy_->args_;
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] child %s (%p): updating child policy handler %p",
            priority_policy_.get(), name_.c_str(), this, child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy>
PriorityLb::ChildPriority::CreateChildPolicyLocked(const ChannelArgs& args) {
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
  // If we transition to state CONNECTING and we've not seen
  // TRANSIENT_FAILURE more recently than READY or IDLE, start failover
  // timer if not already pending.
  // In any other state, update seen_ready_or_idle_since_transient_failure_
  // and cancel failover timer.
  if (state == GRPC_CHANNEL_CONNECTING) {
    if (seen_ready_or_idle_since_transient_failure_ &&
        failover_timer_ == nullptr) {
      failover_timer_ = MakeOrphanable<FailoverTimer>(Ref());
    }
  } else if (state == GRPC_CHANNEL_READY || state == GRPC_CHANNEL_IDLE) {
    seen_ready_or_idle_since_transient_failure_ = true;
    failover_timer_.reset();
  } else if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    seen_ready_or_idle_since_transient_failure_ = false;
    failover_timer_.reset();
  }
  // Notify the parent policy.
  priority_policy_->HandleChildConnectivityStateChangeLocked(this);
}

void PriorityLb::ChildPriority::MaybeDeactivateLocked() {
  if (deactivation_timer_ == nullptr) {
    deactivation_timer_ = MakeOrphanable<DeactivationTimer>(Ref());
  }
}

void PriorityLb::ChildPriority::MaybeReactivateLocked() {
  deactivation_timer_.reset();
}

//
// PriorityLb::ChildPriority::Helper
//

RefCountedPtr<SubchannelInterface>
PriorityLb::ChildPriority::Helper::CreateSubchannel(ServerAddress address,
                                                    const ChannelArgs& args) {
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

void PriorityLb::ChildPriority::Helper::RequestReresolution() {
  if (priority_->priority_policy_->shutting_down_) return;
  if (priority_->ignore_reresolution_requests_) {
    return;
  }
  priority_->priority_policy_->channel_control_helper()->RequestReresolution();
}

absl::string_view PriorityLb::ChildPriority::Helper::GetAuthority() {
  return priority_->priority_policy_->channel_control_helper()->GetAuthority();
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

  absl::string_view name() const override { return kPriority; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    if (json.type() == Json::Type::JSON_NULL) {
      // priority was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      return absl::InvalidArgumentError(
          "field:loadBalancingPolicy error:priority policy requires "
          "configuration. Please use loadBalancingConfig field of service "
          "config instead.");
    }
    std::vector<std::string> errors;
    // Children.
    std::map<std::string, PriorityLbConfig::PriorityLbChild> children;
    auto it = json.object_value().find("children");
    if (it == json.object_value().end()) {
      errors.emplace_back("field:children error:required field missing");
    } else if (it->second.type() != Json::Type::OBJECT) {
      errors.emplace_back("field:children error:type should be object");
    } else {
      const Json::Object& object = it->second.object_value();
      for (const auto& p : object) {
        const std::string& child_name = p.first;
        const Json& element = p.second;
        if (element.type() != Json::Type::OBJECT) {
          errors.emplace_back(absl::StrCat("field:children key:", child_name,
                                           " error:should be type object"));
        } else {
          auto it2 = element.object_value().find("config");
          if (it2 == element.object_value().end()) {
            errors.emplace_back(absl::StrCat("field:children key:", child_name,
                                             " error:missing 'config' field"));
          } else {
            bool ignore_resolution_requests = false;
            // If present, ignore_reresolution_requests must be of type
            // boolean.
            auto it3 =
                element.object_value().find("ignore_reresolution_requests");
            if (it3 != element.object_value().end()) {
              if (it3->second.type() == Json::Type::JSON_TRUE) {
                ignore_resolution_requests = true;
              } else if (it3->second.type() != Json::Type::JSON_FALSE) {
                errors.emplace_back(
                    absl::StrCat("field:children key:", child_name,
                                 " field:ignore_reresolution_requests:should "
                                 "be type boolean"));
              }
            }
            auto config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
                it2->second);
            if (!config.ok()) {
              errors.emplace_back(
                  absl::StrCat("field:children key:", child_name, ": ",
                               config.status().message()));
            } else {
              children[child_name].config = std::move(*config);
              children[child_name].ignore_reresolution_requests =
                  ignore_resolution_requests;
            }
          }
        }
      }
    }
    // Priorities.
    std::vector<std::string> priorities;
    it = json.object_value().find("priorities");
    if (it == json.object_value().end()) {
      errors.emplace_back("field:priorities error:required field missing");
    } else if (it->second.type() != Json::Type::ARRAY) {
      errors.emplace_back("field:priorities error:type should be array");
    } else {
      const Json::Array& array = it->second.array_value();
      for (size_t i = 0; i < array.size(); ++i) {
        const Json& element = array[i];
        if (element.type() != Json::Type::STRING) {
          errors.emplace_back(absl::StrCat("field:priorities element:", i,
                                           " error:should be type string"));
        } else if (children.find(element.string_value()) == children.end()) {
          errors.emplace_back(absl::StrCat("field:priorities element:", i,
                                           " error:unknown child '",
                                           element.string_value(), "'"));
        } else {
          priorities.emplace_back(element.string_value());
        }
      }
      if (priorities.size() != children.size()) {
        errors.emplace_back(absl::StrCat(
            "field:priorities error:priorities size (", priorities.size(),
            ") != children size (", children.size(), ")"));
      }
    }
    if (!errors.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("priority_experimental LB policy config: [",
                       absl::StrJoin(errors, "; "), "]"));
    }
    return MakeRefCounted<PriorityLbConfig>(std::move(children),
                                            std::move(priorities));
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
