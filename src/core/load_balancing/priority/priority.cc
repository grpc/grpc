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

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/address_filtering.h"
#include "src/core/load_balancing/child_policy_handler.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/ref_counted_string.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"

namespace grpc_core {

namespace {

using ::grpc_event_engine::experimental::EventEngine;

constexpr absl::string_view kPriority = "priority_experimental";

// How long we keep a child around for after it is no longer being used
// (either because it has been removed from the config or because we
// have switched to a higher-priority child).
constexpr Duration kChildRetentionInterval = Duration::Minutes(15);

// Default for how long we wait for a newly created child to get connected
// before starting to attempt the next priority.  Overridable via channel arg.
constexpr Duration kDefaultChildFailoverTimeout = Duration::Seconds(10);

// Config for priority LB policy.
class PriorityLbConfig final : public LoadBalancingPolicy::Config {
 public:
  struct PriorityLbChild {
    RefCountedPtr<LoadBalancingPolicy::Config> config;
    bool ignore_reresolution_requests = false;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
    void JsonPostLoad(const Json& json, const JsonArgs&,
                      ValidationErrors* errors);
  };

  PriorityLbConfig() = default;

  PriorityLbConfig(const PriorityLbConfig&) = delete;
  PriorityLbConfig& operator=(const PriorityLbConfig&) = delete;

  PriorityLbConfig(PriorityLbConfig&& other) = delete;
  PriorityLbConfig& operator=(PriorityLbConfig&& other) = delete;

  absl::string_view name() const override { return kPriority; }

  const std::map<std::string, PriorityLbChild>& children() const {
    return children_;
  }
  const std::vector<std::string>& priorities() const { return priorities_; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors);

 private:
  std::map<std::string, PriorityLbChild> children_;
  std::vector<std::string> priorities_;
};

// priority LB policy.
class PriorityLb final : public LoadBalancingPolicy {
 public:
  explicit PriorityLb(Args args);

  absl::string_view name() const override { return kPriority; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // Each ChildPriority holds a ref to the PriorityLb.
  class ChildPriority final : public InternallyRefCounted<ChildPriority> {
   public:
    ChildPriority(RefCountedPtr<PriorityLb> priority_policy, std::string name);

    ~ChildPriority() override {
      priority_policy_.reset(DEBUG_LOCATION, "ChildPriority");
    }

    const std::string& name() const { return name_; }

    absl::Status UpdateLocked(RefCountedPtr<LoadBalancingPolicy::Config> config,
                              bool ignore_reresolution_requests);
    void ExitIdleLocked();
    void ResetBackoffLocked();
    void MaybeDeactivateLocked();
    void MaybeReactivateLocked();

    void Orphan() override;

    RefCountedPtr<SubchannelPicker> GetPicker();

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    const absl::Status& connectivity_status() const {
      return connectivity_status_;
    }

    bool FailoverTimerPending() const { return failover_timer_ != nullptr; }

   private:
    class Helper final : public DelegatingChannelControlHelper {
     public:
      explicit Helper(RefCountedPtr<ChildPriority> priority)
          : priority_(std::move(priority)) {}

      ~Helper() override { priority_.reset(DEBUG_LOCATION, "Helper"); }

      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       RefCountedPtr<SubchannelPicker> picker) override;
      void RequestReresolution() override;

     private:
      ChannelControlHelper* parent_helper() const override {
        return priority_->priority_policy_->channel_control_helper();
      }

      RefCountedPtr<ChildPriority> priority_;
    };

    class DeactivationTimer final
        : public InternallyRefCounted<DeactivationTimer> {
     public:
      explicit DeactivationTimer(RefCountedPtr<ChildPriority> child_priority);

      void Orphan() override;

     private:
      void OnTimerLocked();

      RefCountedPtr<ChildPriority> child_priority_;
      std::optional<EventEngine::TaskHandle> timer_handle_;
    };

    class FailoverTimer final : public InternallyRefCounted<FailoverTimer> {
     public:
      explicit FailoverTimer(RefCountedPtr<ChildPriority> child_priority);

      void Orphan() override;

     private:
      void OnTimerLocked();

      RefCountedPtr<ChildPriority> child_priority_;
      std::optional<EventEngine::TaskHandle> timer_handle_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const ChannelArgs& args);

    void OnConnectivityStateUpdateLocked(
        grpc_connectivity_state state, const absl::Status& status,
        RefCountedPtr<SubchannelPicker> picker);

    RefCountedPtr<PriorityLb> priority_policy_;
    const std::string name_;
    bool ignore_reresolution_requests_ = false;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_CONNECTING;
    absl::Status connectivity_status_;
    RefCountedPtr<SubchannelPicker> picker_;

    bool seen_ready_or_idle_since_transient_failure_ = true;

    OrphanablePtr<DeactivationTimer> deactivation_timer_;
    OrphanablePtr<FailoverTimer> failover_timer_;
  };

  ~PriorityLb() override;

  void ShutdownLocked() override;

  // Returns the priority of the specified child name, or UINT32_MAX if
  // the child is not in the current priority list.
  uint32_t GetChildPriorityLocked(const std::string& child_name) const;

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
  // Optionally deactivates any children at lower priorities.
  // Returns the child's picker to the channel.
  void SetCurrentPriorityLocked(int32_t priority,
                                bool deactivate_lower_priorities,
                                const char* reason);

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
};

//
// PriorityLb
//

PriorityLb::PriorityLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      child_failover_timeout_(std::max(
          Duration::Zero(),
          channel_args()
              .GetDurationFromIntMillis(GRPC_ARG_PRIORITY_FAILOVER_TIMEOUT_MS)
              .value_or(kDefaultChildFailoverTimeout))) {
  GRPC_TRACE_LOG(priority_lb, INFO) << "[priority_lb " << this << "] created";
}

PriorityLb::~PriorityLb() {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << this << "] destroying priority LB policy";
}

void PriorityLb::ShutdownLocked() {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << this << "] shutting down";
  shutting_down_ = true;
  children_.clear();
}

void PriorityLb::ExitIdleLocked() {
  if (current_priority_ != UINT32_MAX) {
    const std::string& child_name = config_->priorities()[current_priority_];
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << this << "] exiting IDLE for current priority "
        << current_priority_ << " child " << child_name;
    children_[child_name]->ExitIdleLocked();
  }
}

void PriorityLb::ResetBackoffLocked() {
  for (const auto& [_, child] : children_) child->ResetBackoffLocked();
}

absl::Status PriorityLb::UpdateLocked(UpdateArgs args) {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << this << "] received update";
  // Update config.
  config_ = args.config.TakeAsSubclass<PriorityLbConfig>();
  // Update args.
  args_ = std::move(args.args);
  // Update addresses.
  addresses_ = MakeHierarchicalAddressMap(args.addresses);
  resolution_note_ = std::move(args.resolution_note);
  // Check all existing children against the new config.
  update_in_progress_ = true;
  std::vector<std::string> errors;
  for (const auto& [child_name, child] : children_) {
    auto config_it = config_->children().find(child_name);
    if (config_it == config_->children().end()) {
      // Existing child not found in new config.  Deactivate it.
      child->MaybeDeactivateLocked();
    } else {
      // Existing child found in new config.  Update it.
      absl::Status status =
          child->UpdateLocked(config_it->second.config,
                              config_it->second.ignore_reresolution_requests);
      if (!status.ok()) {
        errors.emplace_back(
            absl::StrCat("child ", child_name, ": ", status.ToString()));
      }
    }
  }
  update_in_progress_ = false;
  // Try to get connected.
  ChoosePriorityLocked();
  // Return status.
  if (!errors.empty()) {
    return absl::UnavailableError(absl::StrCat(
        "errors from children: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

uint32_t PriorityLb::GetChildPriorityLocked(
    const std::string& child_name) const {
  for (uint32_t priority = 0; priority < config_->priorities().size();
       ++priority) {
    if (config_->priorities()[priority] == child_name) return priority;
  }
  return UINT32_MAX;
}

void PriorityLb::DeleteChild(ChildPriority* child) {
  children_.erase(child->name());
}

void PriorityLb::ChoosePriorityLocked() {
  // If priority list is empty, report TF.
  if (config_->priorities().empty()) {
    absl::Status status =
        absl::UnavailableError("priority policy has empty priority list");
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return;
  }
  // Iterate through priorities, searching for one in READY or IDLE,
  // creating new children as needed.
  current_priority_ = UINT32_MAX;
  for (uint32_t priority = 0; priority < config_->priorities().size();
       ++priority) {
    // If the child for the priority does not exist yet, create it.
    const std::string& child_name = config_->priorities()[priority];
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << this << "] trying priority " << priority
        << ", child " << child_name;
    auto& child = children_[child_name];
    // Create child if needed.
    if (child == nullptr) {
      child = MakeOrphanable<ChildPriority>(
          RefAsSubclass<PriorityLb>(DEBUG_LOCATION, "ChildPriority"),
          child_name);
      auto child_config = config_->children().find(child_name);
      DCHECK(child_config != config_->children().end());
      // If the child policy returns a non-OK status, request re-resolution.
      // Note that this will initially cause fixed backoff delay in the
      // resolver instead of exponential delay.  However, once the
      // resolver returns the initial re-resolution, we will be able to
      // return non-OK from UpdateLocked(), which will trigger
      // exponential backoff instead.
      absl::Status status = child->UpdateLocked(
          child_config->second.config,
          child_config->second.ignore_reresolution_requests);
      if (!status.ok()) channel_control_helper()->RequestReresolution();
    } else {
      // The child already exists.  Reactivate if needed.
      child->MaybeReactivateLocked();
    }
    // Select this child if it is in states READY or IDLE.
    if (child->connectivity_state() == GRPC_CHANNEL_READY ||
        child->connectivity_state() == GRPC_CHANNEL_IDLE) {
      SetCurrentPriorityLocked(
          priority, /*deactivate_lower_priorities=*/true,
          ConnectivityStateName(child->connectivity_state()));
      return;
    }
    // Select this child if its failover timer is pending.
    if (child->FailoverTimerPending()) {
      SetCurrentPriorityLocked(priority, /*deactivate_lower_priorities=*/false,
                               "failover timer pending");
      return;
    }
    // Child has been failing for a while.  Move on to the next priority.
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << this << "] skipping priority " << priority
        << ", child " << child_name
        << ": state=" << ConnectivityStateName(child->connectivity_state())
        << ", failover timer not pending";
  }
  // If we didn't find any priority to try, pick the first one in state
  // CONNECTING.
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << this
      << "] no priority reachable, checking for CONNECTING priority to "
         "delegate to";
  for (uint32_t priority = 0; priority < config_->priorities().size();
       ++priority) {
    // If the child for the priority does not exist yet, create it.
    const std::string& child_name = config_->priorities()[priority];
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << this << "] trying priority " << priority
        << ", child " << child_name;
    auto& child = children_[child_name];
    CHECK(child != nullptr);
    if (child->connectivity_state() == GRPC_CHANNEL_CONNECTING) {
      SetCurrentPriorityLocked(priority, /*deactivate_lower_priorities=*/false,
                               "CONNECTING (pass 2)");
      return;
    }
  }
  // Did not find any child in CONNECTING, delegate to last child.
  SetCurrentPriorityLocked(config_->priorities().size() - 1,
                           /*deactivate_lower_priorities=*/false,
                           "no usable children");
}

void PriorityLb::SetCurrentPriorityLocked(int32_t priority,
                                          bool deactivate_lower_priorities,
                                          const char* reason) {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << this << "] selecting priority " << priority
      << ", child " << config_->priorities()[priority] << " (" << reason
      << ", deactivate_lower_priorities=" << deactivate_lower_priorities << ")";
  current_priority_ = priority;
  if (deactivate_lower_priorities) {
    for (uint32_t p = priority + 1; p < config_->priorities().size(); ++p) {
      const std::string& child_name = config_->priorities()[p];
      auto it = children_.find(child_name);
      if (it != children_.end()) it->second->MaybeDeactivateLocked();
    }
  }
  auto& child = children_[config_->priorities()[priority]];
  CHECK(child != nullptr);
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
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << child_priority_->priority_policy_.get()
      << "] child " << child_priority_->name_ << " (" << child_priority_.get()
      << "): deactivating -- will remove in "
      << kChildRetentionInterval.millis() << "ms";
  timer_handle_ =
      child_priority_->priority_policy_->channel_control_helper()
          ->GetEventEngine()
          ->RunAfter(kChildRetentionInterval, [self = Ref(DEBUG_LOCATION,
                                                          "Timer")]() mutable {
            ExecCtx exec_ctx;
            auto self_ptr = self.get();
            self_ptr->child_priority_->priority_policy_->work_serializer()->Run(
                [self = std::move(self)]() { self->OnTimerLocked(); });
          });
}

void PriorityLb::ChildPriority::DeactivationTimer::Orphan() {
  if (timer_handle_.has_value()) {
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << child_priority_->priority_policy_.get()
        << "] child " << child_priority_->name_ << " (" << child_priority_.get()
        << "): reactivating";
    child_priority_->priority_policy_->channel_control_helper()
        ->GetEventEngine()
        ->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  Unref();
}

void PriorityLb::ChildPriority::DeactivationTimer::OnTimerLocked() {
  if (timer_handle_.has_value()) {
    timer_handle_.reset();
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << child_priority_->priority_policy_.get()
        << "] child " << child_priority_->name_ << " (" << child_priority_.get()
        << "): deactivation timer fired, deleting child";
    child_priority_->priority_policy_->DeleteChild(child_priority_.get());
  }
}

//
// PriorityLb::ChildPriority::FailoverTimer
//

PriorityLb::ChildPriority::FailoverTimer::FailoverTimer(
    RefCountedPtr<PriorityLb::ChildPriority> child_priority)
    : child_priority_(std::move(child_priority)) {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << child_priority_->priority_policy_.get()
      << "] child " << child_priority_->name_ << " (" << child_priority_.get()
      << "): starting failover timer for "
      << child_priority_->priority_policy_->child_failover_timeout_.millis()
      << "ms";
  timer_handle_ =
      child_priority_->priority_policy_->channel_control_helper()
          ->GetEventEngine()
          ->RunAfter(
              child_priority_->priority_policy_->child_failover_timeout_,
              [self = Ref(DEBUG_LOCATION, "Timer")]() mutable {
                ExecCtx exec_ctx;
                auto self_ptr = self.get();
                self_ptr->child_priority_->priority_policy_->work_serializer()
                    ->Run(
                        [self = std::move(self)]() { self->OnTimerLocked(); });
              });
}

void PriorityLb::ChildPriority::FailoverTimer::Orphan() {
  if (timer_handle_.has_value()) {
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << child_priority_->priority_policy_.get()
        << "] child " << child_priority_->name_ << " (" << child_priority_.get()
        << "): cancelling failover timer";
    child_priority_->priority_policy_->channel_control_helper()
        ->GetEventEngine()
        ->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  Unref();
}

void PriorityLb::ChildPriority::FailoverTimer::OnTimerLocked() {
  if (timer_handle_.has_value()) {
    timer_handle_.reset();
    GRPC_TRACE_LOG(priority_lb, INFO)
        << "[priority_lb " << child_priority_->priority_policy_.get()
        << "] child " << child_priority_->name_ << " (" << child_priority_.get()
        << "): failover timer fired, reporting TRANSIENT_FAILURE";
    child_priority_->OnConnectivityStateUpdateLocked(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::Status(absl::StatusCode::kUnavailable, "failover timer fired"),
        nullptr);
  }
}

//
// PriorityLb::ChildPriority
//

PriorityLb::ChildPriority::ChildPriority(
    RefCountedPtr<PriorityLb> priority_policy, std::string name)
    : priority_policy_(std::move(priority_policy)), name_(std::move(name)) {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << priority_policy_.get() << "] creating child "
      << name_ << " (" << this << ")";
  // Start the failover timer.
  failover_timer_ = MakeOrphanable<FailoverTimer>(Ref());
}

void PriorityLb::ChildPriority::Orphan() {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << priority_policy_.get() << "] child " << name_
      << " (" << this << "): orphaned";
  failover_timer_.reset();
  deactivation_timer_.reset();
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   priority_policy_->interested_parties());
  child_policy_.reset();
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_.reset();
  Unref(DEBUG_LOCATION, "ChildPriority+Orphan");
}

RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
PriorityLb::ChildPriority::GetPicker() {
  if (picker_ == nullptr) {
    return MakeRefCounted<QueuePicker>(
        priority_policy_->Ref(DEBUG_LOCATION, "QueuePicker"));
  }
  return picker_;
}

absl::Status PriorityLb::ChildPriority::UpdateLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> config,
    bool ignore_reresolution_requests) {
  if (priority_policy_->shutting_down_) return absl::OkStatus();
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << priority_policy_.get() << "] child " << name_
      << " (" << this << "): start update";
  ignore_reresolution_requests_ = ignore_reresolution_requests;
  // Create policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(priority_policy_->args_);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = std::move(config);
  if (priority_policy_->addresses_.ok()) {
    auto it = priority_policy_->addresses_->find(name_);
    if (it == priority_policy_->addresses_->end()) {
      update_args.addresses = std::make_shared<EndpointAddressesListIterator>(
          EndpointAddressesList());
    } else {
      update_args.addresses = it->second;
    }
  } else {
    update_args.addresses = priority_policy_->addresses_.status();
  }
  update_args.resolution_note = priority_policy_->resolution_note_;
  update_args.args = priority_policy_->args_;
  // Update the policy.
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << priority_policy_.get() << "] child " << name_
      << " (" << this << "): updating child policy handler "
      << child_policy_.get();
  return child_policy_->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy>
PriorityLb::ChildPriority::CreateChildPolicyLocked(const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = priority_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &priority_lb_trace);
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << priority_policy_.get() << "] child " << name_
      << " (" << this << "): created new child policy handler "
      << lb_policy.get();
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
    RefCountedPtr<SubchannelPicker> picker) {
  GRPC_TRACE_LOG(priority_lb, INFO)
      << "[priority_lb " << priority_policy_.get() << "] child " << name_
      << " (" << this << "): state update: " << ConnectivityStateName(state)
      << " (" << status << ") picker " << picker.get();
  // Store the state and picker.
  connectivity_state_ = state;
  connectivity_status_ = status;
  // When the failover timer fires, this method will be called with picker
  // set to null, because we want to consider the child to be in
  // TRANSIENT_FAILURE, but we have no new picker to report.  In that case,
  // just keep using the old picker, in case we wind up delegating to this
  // child when all priorities are failing.
  if (picker != nullptr) picker_ = std::move(picker);
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
  // Call the LB policy's ChoosePriorityLocked() to choose a priority to
  // use based on the updated state of this child.
  //
  // Note that if we're in the process of propagating an update from our
  // parent to our children, we skip this, because we don't want to
  // choose a new priority based on inconsistent state.  Instead, the
  // policy will choose a new priority once the update has been seen by
  // all children.
  if (!priority_policy_->update_in_progress_) {
    priority_policy_->ChoosePriorityLocked();
  }
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

void PriorityLb::ChildPriority::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
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

//
// factory
//

const JsonLoaderInterface* PriorityLbConfig::PriorityLbChild::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<PriorityLbChild>()
          // Note: The "config" field requires custom parsing, so it's
          // handled in JsonPostLoad() instead of here.
          .OptionalField("ignore_reresolution_requests",
                         &PriorityLbChild::ignore_reresolution_requests)
          .Finish();
  return loader;
}

void PriorityLbConfig::PriorityLbChild::JsonPostLoad(const Json& json,
                                                     const JsonArgs&,
                                                     ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".config");
  auto it = json.object().find("config");
  if (it == json.object().end()) {
    errors->AddError("field not present");
    return;
  }
  auto lb_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          it->second);
  if (!lb_config.ok()) {
    errors->AddError(lb_config.status().message());
    return;
  }
  config = std::move(*lb_config);
}

const JsonLoaderInterface* PriorityLbConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<PriorityLbConfig>()
          .Field("children", &PriorityLbConfig::children_)
          .Field("priorities", &PriorityLbConfig::priorities_)
          .Finish();
  return loader;
}

void PriorityLbConfig::JsonPostLoad(const Json& /*json*/, const JsonArgs&,
                                    ValidationErrors* errors) {
  std::set<std::string> unknown_priorities;
  for (const std::string& priority : priorities_) {
    if (children_.find(priority) == children_.end()) {
      unknown_priorities.insert(priority);
    }
  }
  if (!unknown_priorities.empty()) {
    errors->AddError(absl::StrCat("unknown priorit(ies): [",
                                  absl::StrJoin(unknown_priorities, ", "),
                                  "]"));
  }
}

class PriorityLbFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<PriorityLb>(std::move(args));
  }

  absl::string_view name() const override { return kPriority; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<PriorityLbConfig>>(
        json, JsonArgs(), "errors validating priority LB policy config");
  }
};

}  // namespace

void RegisterPriorityLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<PriorityLbFactory>());
}

}  // namespace grpc_core
