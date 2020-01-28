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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"
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

#define GRPC_XDS_DEFAULT_FALLBACK_TIMEOUT_MS 10000
#define GRPC_XDS_DEFAULT_LOCALITY_RETENTION_INTERVAL_MS (15 * 60 * 1000)
#define GRPC_XDS_DEFAULT_FAILOVER_TIMEOUT_MS 10000

namespace grpc_core {

TraceFlag grpc_lb_priority_trace(false, "priority_lb");

namespace {

constexpr char kPriority[] = "priority";

class PriorityLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct ChildConfig {
    std::string name;
    RefCountedPtr<LoadBalancingPolicy::Config> config;
  };

  PriorityLbConfig(std::vector<ChildConfig> priorities,
                   grpc_millis failover_timeout,
                   grpc_millis retention_timeout)
      : priorities_(std::move(priorities)),
        failover_timeout_(failover_timeout),
        retention_timeout_(retention_timeout) {}

  const char* name() const override { return kPriority; }

  const std::vector<ChildConfig>& priorities() const { return priorities_; }
  grpc_millis failover_timeout() const { return failover_timeout_; }
  grpc_millis retention_timeout() const { return retention_timeout_; }

 private:
  const std::vector<ChildConfig> priorities_;
  const grpc_millis failover_timeout_;
  const grpc_millis retention_timeout_;
};

class PriorityLb : public LoadBalancingPolicy {
 public:
  explicit PriorityLb(Args args);

  const char* name() const override { return kPriority; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Each Priority holds a ref to the PriorityLb.
// FIXME: rename to Child?
  class Priority : public InternallyRefCounted<Priority> {
   public:
    Priority(RefCountedPtr<PriorityLb> priority_policy, std::string name);

    ~Priority() { priority_policy_.reset(DEBUG_LOCATION, "Priority"); }

    const std::string& name() const { return name_; }

    void Orphan() override;

    void UpdateLocked(RefCountedPtr<LoadBalancingPolicy::Config> update);
    void ResetBackoffLocked();

    std::unique_ptr<SubchannelPicker> GetPicker() {
      return grpc_core::MakeUnique<RefCountedPickerWrapper>(picker_wrapper_);
    }

    void DeactivateLocked();
    // Returns true if this locality map becomes the currently used one (i.e.,
    // its priority is selected) after reactivation.
    bool MaybeReactivateLocked();
    void MaybeCancelFailoverTimerLocked();

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
      explicit Helper(RefCountedPtr<Priority> priority)
          : priority_(std::move(priority)) {}

      ~Helper() { priority_.reset(DEBUG_LOCATION, "Helper"); }

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      void AddTraceEvent(TraceSeverity severity,
                         StringView message) override;

      void set_child(LoadBalancingPolicy* child) { child_ = child; }

     private:
      bool CalledByPendingChild() const;
      bool CalledByCurrentChild() const;

      RefCountedPtr<Priority> priority_;
      LoadBalancingPolicy* child_ = nullptr;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const char* name, const grpc_channel_args* args);
    grpc_channel_args* CreateChildPolicyArgsLocked(
        const grpc_channel_args* args);

    void OnLocalityStateUpdateLocked();
    void UpdateConnectivityStateLocked();
    static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
    static void OnFailoverTimer(void* arg, grpc_error* error);
    static void OnDelayedRemovalTimerLocked(void* arg, grpc_error* error);
    static void OnFailoverTimerLocked(void* arg, grpc_error* error);

    RefCountedPtr<PriorityLb> priority_policy_;
    std::string name_;

    // States for delayed removal.
    grpc_timer delayed_removal_timer_;
    grpc_closure on_delayed_removal_timer_;
    bool delayed_removal_timer_callback_pending_ = false;

    // States of failover.
    grpc_timer failover_timer_;
    grpc_closure on_failover_timer_;
    bool failover_timer_callback_pending_ = false;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;
    OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;

    RefCountedPtr<RefCountedPicker> picker_wrapper_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
  };

  ~PriorityLb();

  void ShutdownLocked() override;

  uint32_t FindPriorityForChild(const std::string& child_name);
  void UpdatePriorityPickerLocked();
  void MaybeCreatePriorityLocked(uint32_t priority);
  void FailoverOnConnectionFailureLocked(const std::string& child_name);
  void FailoverOnDisconnectionLocked(uint32_t failed_priority);
  void SwitchToHigherPriorityLocked(uint32_t priority);
  void DeactivatePrioritiesLowerThan(uint32_t priority);
  // Callers should make sure the priority list is non-empty.
  uint32_t LowestPriority() const {
    return static_cast<uint32_t>(priorities_.size()) - 1;
  }

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<PriorityLbConfig> config_;

  // True if we are in the process of shutting down.
  bool shutting_down_ = false;
  // A map of children by name.
// FIXME: maybe key this by StringView, with actual string stored inside
// of value object?
  std::map<std::string /* name */, OrphanablePtr<Priority>> children_;
  // The priority that is currently being used.
  uint32_t current_priority_ = UINT32_MAX;
};

//
// PriorityLb
//

PriorityLb::PriorityLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] created priority LB policy", this);
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
  priorities_.clear();
}

void PriorityLb::ResetBackoffLocked() {
  for (const auto& priority : priorities_) {
    priority->ResetBackoffLocked();
  }
}

bool UpdateContainsChild(
    const std::vector<PriorityLbConfig::ChildConfig>& update,
    const std::string& name) {
  for (const auto& child : update) {
    if (child.name == name) return true;
  }
  return false;
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
// FIXME: get this logic right
// (maybe we don't need the priorities_ data member at all?)
  // Deactivate all current children that are not present in the update.
  for (auto it = children_.begin(); it != children_.end();) {
    if (!UpdateContainsChild(config_->priorities(), it->first)) {
      if (locality_retention_interval_ms_ == 0) {
        it = children_.erase(it);
      } else {
        it->second->DeactivateLocked();
      }
    }
  }
  // Update all existing priorities.
  current_priority_ = UINT32_MAX;
  uint32_t first_missing = UINT32_MAX;
  for (uint32_t i = 0; i < config_->priorities().size(); ++i) {
    PriorityLbConfig::Child& child = config->priorities().at(i);
    auto it = children_.find(child.name);
    if (it != children_.end()) {
      it->second->UpdateLocked(child.config);
      // If we have not yet found a READY priority and this child is
      // ready, make it the current priority.
      if (current_priority_ == UINT32_MAX &&
          it->second->connectivity_state() == GRPC_CHANNEL_READY) {
        current_priority_ = i;
      }
    } else if (first_missing == UINT32_MAX) {
      first_missing = i;
    }
  }
  // If we haven't found a current priority but we found a missing one,
  // create it.
  if (current_priority_ == UINT32_MAX && first_missing != UINT32_MAX) {
    // Create a new priority.  Note that in some rare cases (e.g., the
    // priority reports TRANSIENT_FAILURE synchronously due to subchannel
    // sharing), the following invocation may result in multiple priorities
    // to be created.
    MaybeCreatePriorityLocked(first_missing);
  }

#if 0
  // 1. Remove from the priority list the priorities that are not in the update.
  DeactivatePrioritiesLowerThan(config_->priorities().size() - 1);
  // 2. Update all the existing priorities.
  for (uint32_t priority = 0; priority < priorities_.size(); ++priority) {
    Priority* locality_map = priorities_[priority].get();
    const auto* locality_map_update = priority_list_update.Find(priority);
    // Propagate locality_map_update.
    // TODO(juanlishen): Find a clean way to skip duplicate update for a
    // priority.
    locality_map->UpdateLocked(*locality_map_update);
  }
  // 3. Only create a new locality map if all the existing ones have failed.
  if (priorities_.empty() ||
      !priorities_[priorities_.size() - 1]->failover_timer_callback_pending()) {
    const uint32_t new_priority = static_cast<uint32_t>(priorities_.size());
    // Create a new locality map. Note that in some rare cases (e.g., the
    // locality map reports TRANSIENT_FAILURE synchronously due to subchannel
    // sharing), the following invocation may result in multiple locality maps
    // to be created.
    MaybeCreatePriorityLocked(new_priority);
  }
#endif

}

void PriorityLb::UpdatePriorityPickerLocked() {
  if (current_priority_ == UINT32_MAX) {
    grpc_error* error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("no ready locality map"),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    priority_policy_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        grpc_core::MakeUnique<TransientFailurePicker>(error));
    return;
  }
  priority_policy_->channel_control_helper()->UpdateState(
      GRPC_CHANNEL_READY,
      children_[config_->priorities().at(current_priority_)]->GetPicker());
}

void PriorityLb::MaybeCreatePriorityLocked(uint32_t priority) {
  if (priority >= config_->priorities().size()) return;
  PriorityLbConfig::Child& config = config_->priorities().at(priority);
  if (children_.find(config.name) != children_.end()) {
    MaybeReactivateLocked();
  } else {
    Priority* new_child =
        new Priority(Ref(DEBUG_LOCATION, "Priority"), config.name);
    children_[config.name] = OrphanablePtr<Priority>(new_child);
    new_child->UpdateLocked(config.config);
  }
}

uint32_t PriorityLb::FindPriorityForChild(const std::string& child_name) {
  // This simple linear search is probably fine, since we don't expect
  // a large number of priorities here.  If it becomes a problem, we can
  // optimize it via (e.g.) maintaining a map from name to priority.
  for (uint32_t i = 0; i < config_->priorities().size(); ++i) {
    if (config_->priorities().at(i).name == child_name) return i;
  }
  return UINT32_MAX;
}

void PriorityLb::FailoverOnConnectionFailureLocked(
    const std::string& child_name) {
  const uint32_t failed_priority = FindPriorityForChild(child_name);
  // If the failed priority is not in the map, do nothing.  This could
  // happen if we're in the middle of applying an update that removes
  // a priority when the child for that priority has a connection failure.
// FIXME: maybe return a queue picker in this case, while we try to
// connect to another child?
  if (failed_priority == UINT32_MAX) return;
  // If we're failing over from the lowest priority, report TRANSIENT_FAILURE.
  if (failed_priority == config_->priorities().size() - 1) {
    UpdatePriorityPickerLocked();
  }
  MaybeCreatePriorityLocked(failed_priority + 1);
}

void PriorityLb::FailoverOnDisconnectionLocked(
    uint32_t failed_priority) {
  current_priority_ = UINT32_MAX;
  for (uint32_t next_priority = failed_priority + 1;
       next_priority <= priority_list_update().LowestPriority();
       ++next_priority) {
    if (!Contains(next_priority)) {
      MaybeCreatePriorityLocked(next_priority);
      return;
    }
    if (priorities_[next_priority]->MaybeReactivateLocked()) return;
  }
}

void PriorityLb::SwitchToHigherPriorityLocked(uint32_t priority) {
  current_priority_ = priority;
  DeactivatePrioritiesLowerThan(current_priority_);
  UpdatePriorityPickerLocked();
}

void PriorityLb::DeactivatePrioritiesLowerThan(
    uint32_t priority) {
  if (priorities_.empty()) return;
  // Deactivate the locality maps from the lowest priority.
  for (uint32_t p = LowestPriority(); p > priority; --p) {
    if (priority_policy_->locality_retention_interval_ms_ == 0) {
      priorities_.pop_back();
    } else {
      priorities_[p]->DeactivateLocked();
    }
  }
}

//
// PriorityLb::Priority
//

PriorityLb::Priority::Priority(
    RefCountedPtr<PriorityLb> priority_policy, uint32_t priority)
    : priority_policy_(std::move(priority_policy)), priority_(priority) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] Creating priority %" PRIu32,
            priority_policy_.get(), priority_);
  }
  GRPC_CLOSURE_INIT(&on_failover_timer_, OnFailoverTimer, this,
                    grpc_schedule_on_exec_ctx);
  // Start the failover timer.
  Ref(DEBUG_LOCATION, "Priority+OnFailoverTimerLocked").release();
  grpc_timer_init(
      &failover_timer_,
      ExecCtx::Get()->Now() + priority_policy_->locality_map_failover_timeout_ms_,
      &on_failover_timer_);
  failover_timer_callback_pending_ = true;
  // This is the first locality map ever created, report CONNECTING.
  if (priority_ == 0) {
    priority_policy_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING,
        grpc_core::MakeUnique<QueuePicker>(
            priority_policy_->Ref(DEBUG_LOCATION, "QueuePicker")));
  }
}

void PriorityLb::Priority::UpdateLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> config) {
  if (priority_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] Start Updating priority %" PRIu32,
            priority_policy(), priority_);
  }
// FIXME: update child

}

bool PriorityLb::Priority::MaybeReactivateLocked() {
  // Don't reactivate a priority that is not higher than the current one.
  if (priority_ >= priority_list()->current_priority()) return false;
  // Reactivate this priority by cancelling deletion timer.
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Switch to this higher priority if it's READY.
  if (connectivity_state_ != GRPC_CHANNEL_READY) return false;
  priority_list()->SwitchToHigherPriorityLocked(priority_);
  return true;
}

void PriorityLb::Priority::MaybeCancelFailoverTimerLocked() {
  if (failover_timer_callback_pending_) grpc_timer_cancel(&failover_timer_);
}

void PriorityLb::Priority::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] Priority %" PRIu32 " orphaned.", priority_policy(),
            priority_);
  }
  MaybeCancelFailoverTimerLocked();
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  localities_.clear();
  Unref(DEBUG_LOCATION, "Priority+Orphan");
}

void PriorityLb::Priority::OnLocalityStateUpdateLocked() {
  UpdateConnectivityStateLocked();
  // Ignore priorities not in priority_list_update.
  if (!priority_list_update().Contains(priority_)) return;
  const uint32_t current_priority = priority_list()->current_priority();
  // Ignore lower-than-current priorities.
  if (priority_ > current_priority) return;
  // Maybe update fallback state.
  if (connectivity_state_ == GRPC_CHANNEL_READY) {
    priority_policy_->MaybeCancelFallbackAtStartupChecks();
    priority_policy_->MaybeExitFallbackMode();
  }
  // Update is for a higher-than-current priority. (Special case: update is for
  // any active priority if there is no current priority.)
  if (priority_ < current_priority) {
    if (connectivity_state_ == GRPC_CHANNEL_READY) {
      MaybeCancelFailoverTimerLocked();
      // If a higher-than-current priority becomes READY, switch to use it.
      priority_list()->SwitchToHigherPriorityLocked(priority_);
    } else if (connectivity_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // If a higher-than-current priority becomes TRANSIENT_FAILURE, only
      // handle it if it's the priority that is still in failover timeout.
      if (failover_timer_callback_pending_) {
        MaybeCancelFailoverTimerLocked();
        priority_list()->FailoverOnConnectionFailureLocked(name_);
      }
    }
    return;
  }
  // Update is for current priority.
  if (connectivity_state_ != GRPC_CHANNEL_READY) {
    // Fail over if it's no longer READY.
    priority_list()->FailoverOnDisconnectionLocked(priority_);
  }
  // At this point, one of the following things has happened to the current
  // priority.
  // 1. It remained the same (but received picker update from its localities).
  // 2. It changed to a lower priority due to failover.
  // 3. It became invalid because failover didn't yield a READY priority.
  // In any case, update the xds picker.
  priority_list()->UpdatePriorityPickerLocked();
}

void PriorityLb::Priority::UpdateConnectivityStateLocked() {
  size_t num_ready = 0;
  size_t num_connecting = 0;
  size_t num_idle = 0;
  size_t num_transient_failures = 0;
  for (const auto& p : localities_) {
    const auto& locality_name = p.first;
    const Locality* locality = p.second.get();
    // Skip the localities that are not in the latest locality map update.
    if (!locality_map_update()->Contains(locality_name)) continue;
    switch (locality->connectivity_state()) {
      case GRPC_CHANNEL_READY: {
        ++num_ready;
        break;
      }
      case GRPC_CHANNEL_CONNECTING: {
        ++num_connecting;
        break;
      }
      case GRPC_CHANNEL_IDLE: {
        ++num_idle;
        break;
      }
      case GRPC_CHANNEL_TRANSIENT_FAILURE: {
        ++num_transient_failures;
        break;
      }
      default:
        GPR_UNREACHABLE_CODE(return );
    }
  }
  if (num_ready > 0) {
    connectivity_state_ = GRPC_CHANNEL_READY;
  } else if (num_connecting > 0) {
    connectivity_state_ = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
    connectivity_state_ = GRPC_CHANNEL_IDLE;
  } else {
    connectivity_state_ = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] Priority %" PRIu32 " (%p) connectivity changed to %s",
            priority_policy(), priority_, this,
            ConnectivityStateName(connectivity_state_));
  }
}

void PriorityLb::Priority::OnDelayedRemovalTimer(
    void* arg, grpc_error* error) {
  Priority* self = static_cast<Priority*>(arg);
  self->priority_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_delayed_removal_timer_,
                        OnDelayedRemovalTimerLocked, self, nullptr),
      GRPC_ERROR_REF(error));
}

void PriorityLb::Priority::OnDelayedRemovalTimerLocked(
    void* arg, grpc_error* error) {
  Priority* self = static_cast<Priority*>(arg);
  self->delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->priority_policy_->shutting_down_) {
    auto* priority_list = self->priority_list();
    const bool keep = self->priority_list_update().Contains(self->priority_) &&
                      self->priority_ <= priority_list->current_priority();
    if (!keep) {
      // This check is to make sure we always delete the locality maps from
      // the lowest priority even if the closures of the back-to-back timers
      // are not run in FIFO order.
      // TODO(juanlishen): Eliminate unnecessary maintenance overhead for some
      // deactivated locality maps when out-of-order closures are run.
      // TODO(juanlishen): Check the timer implementation to see if this
      // defense is necessary.
      if (self->priority_ == priority_list->LowestPriority()) {
        priority_list->priorities_.pop_back();
      } else {
        gpr_log(GPR_ERROR,
                "[priority_lb %p] Priority %" PRIu32
                " is not the lowest priority (highest numeric value) but is "
                "attempted to be deleted.",
                self->priority_policy(), self->priority_);
      }
    }
  }
  self->Unref(DEBUG_LOCATION, "Priority+timer");
}

void PriorityLb::Priority::OnFailoverTimer(void* arg,
                                                            grpc_error* error) {
  Priority* self = static_cast<Priority*>(arg);
  self->priority_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_failover_timer_, OnFailoverTimerLocked, self,
                        nullptr),
      GRPC_ERROR_REF(error));
}

void PriorityLb::Priority::OnFailoverTimerLocked(
    void* arg, grpc_error* error) {
  Priority* self = static_cast<Priority*>(arg);
  self->failover_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->priority_policy_->shutting_down_) {
    self->priority_list()->FailoverOnConnectionFailureLocked(self->name_);
  }
  self->Unref(DEBUG_LOCATION, "Priority+OnFailoverTimerLocked");
}

//
// PriorityLb::Priority::Locality
//

grpc_channel_args*
PriorityLb::Priority::Locality::CreateChildPolicyArgsLocked(
    const grpc_channel_args* args_in) {
  const grpc_arg args_to_add[] = {
      // A channel arg indicating if the target is a backend inferred from a
      // grpclb load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_XDS_LOAD_BALANCER),
          1),
      // Inhibit client-side health checking, since the balancer does
      // this for us.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1),
  };
  return grpc_channel_args_copy_and_add(args_in, args_to_add,
                                        GPR_ARRAY_SIZE(args_to_add));
}

OrphanablePtr<LoadBalancingPolicy>
PriorityLb::Priority::Locality::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = new Helper(this->Ref(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = priority_policy()->combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR,
            "[priority_lb %p] Locality %p %s: failure creating child policy %s",
            priority_policy(), this, name_->AsHumanReadableString(), name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO,
            "[priority_lb %p] Locality %p %s: Created new child policy %s (%p)",
            priority_policy(), this, name_->AsHumanReadableString(), name,
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   priority_policy()->interested_parties());
  return lb_policy;
}

void PriorityLb::Priority::Locality::UpdateLocked(
    uint32_t locality_weight, ServerAddressList serverlist) {
  if (priority_policy()->shutting_down_) return;
  // Update locality weight.
  weight_ = locality_weight;
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(serverlist);
  update_args.config = priority_policy()->config_->child_policy();
  update_args.args = CreateChildPolicyArgsLocked(priority_policy()->args_);
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
  //    have not yet received a serverlist from the balancer or gone
  //    into fallback mode; in this case, both child_policy_ and
  //    pending_child_policy_ are null).  In this case, we create a
  //    new child policy and store it in child_policy_.
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
  // TODO(juanlishen): If the child policy is not configured via service config,
  // use whatever algorithm is specified by the balancer.
  const char* child_policy_name = update_args.config == nullptr
                                      ? "round_robin"
                                      : update_args.config->name();
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
              "[priority_lb %p] Locality %p %s: Creating new %schild policy %s",
              priority_policy(), this, name_->AsHumanReadableString(),
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
    gpr_log(GPR_INFO, "[priority_lb %p] Locality %p %s: Updating %schild policy %p",
            priority_policy(), this, name_->AsHumanReadableString(),
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

void PriorityLb::Priority::Locality::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
    gpr_log(GPR_INFO, "[priority_lb %p] Locality %p %s: shutting down locality",
            priority_policy(), this, name_->AsHumanReadableString());
  }
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   priority_policy()->interested_parties());
  child_policy_.reset();
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(),
        priority_policy()->interested_parties());
    pending_child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_wrapper_.reset();
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  shutdown_ = true;
}

void PriorityLb::Priority::Locality::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void PriorityLb::Priority::Locality::Orphan() {
  ShutdownLocked();
  Unref();
}

void PriorityLb::Priority::Locality::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (weight_ == 0) return;
  // Set the locality weight to 0 so that future xds picker won't contain this
  // locality.
  weight_ = 0;
  // Start a timer to delete the locality.
  Ref(DEBUG_LOCATION, "Locality+timer").release();
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(
      &delayed_removal_timer_,
      ExecCtx::Get()->Now() + priority_policy()->locality_retention_interval_ms_,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

void PriorityLb::Priority::Locality::OnDelayedRemovalTimer(
    void* arg, grpc_error* error) {
  Locality* self = static_cast<Locality*>(arg);
  self->priority_policy()->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_delayed_removal_timer_,
                        OnDelayedRemovalTimerLocked, self, nullptr),
      GRPC_ERROR_REF(error));
}

void PriorityLb::Priority::Locality::
    OnDelayedRemovalTimerLocked(void* arg, grpc_error* error) {
  Locality* self = static_cast<Locality*>(arg);
  self->delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->shutdown_ && self->weight_ == 0) {
    self->locality_map_->localities_.erase(self->name_);
  }
  self->Unref(DEBUG_LOCATION, "Locality+timer");
}

//
// PriorityLb::Locality::Helper
//

bool PriorityLb::Priority::Locality::Helper::
    CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == locality_->pending_child_policy_.get();
}

bool PriorityLb::Priority::Locality::Helper::
    CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == locality_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface>
PriorityLb::Priority::Locality::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (locality_->priority_policy()->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return locality_->priority_policy()->channel_control_helper()->CreateSubchannel(
      args);
}

void PriorityLb::Priority::Locality::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (locality_->priority_policy()->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_priority_trace)) {
      gpr_log(GPR_INFO,
              "[priority_lb %p helper %p] pending child policy %p reports state=%s",
              locality_->priority_policy(), this,
              locality_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        locality_->child_policy_->interested_parties(),
        locality_->priority_policy()->interested_parties());
    locality_->child_policy_ = std::move(locality_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Cache the picker and its state in the locality.
  // TODO(roth): If load reporting is not configured, we should ideally
  // pass a null LocalityStats ref to the RefCountedPicker and have it
  // not collect any stats, since they're not going to be used.  This would
  // require recreating all of the pickers whenever we get a config update.
  locality_->picker_wrapper_ = MakeRefCounted<RefCountedPicker>(
      std::move(picker),
      locality_->priority_policy()->client_stats_.FindLocalityStats(
          locality_->name_));
  locality_->connectivity_state_ = state;
  // Notify the locality map.
  locality_->locality_map_->OnLocalityStateUpdateLocked();
}

void PriorityLb::Priority::Locality::Helper::AddTraceEvent(
    TraceSeverity severity, StringView message) {
  if (locality_->priority_policy()->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  locality_->priority_policy()->channel_control_helper()->AddTraceEvent(severity,
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
      // xds was mentioned as a policy in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:xds policy requires configuration. "
          "Please use loadBalancingConfig field of service config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // Priorities.
    std::vector<PriorityLbConfig::ChildConfig> priorities;
    auto it = json.object_value().find("priorities");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:priorities error:required field missing"));
    } else if (it->second.type() != Json::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:priorities error:type should be array"));
    } else {
      const Json::Array& array = it->second.array_value();
      for (size_t i = 0; i < array.size(); ++i) {
        const Json& element = array[i];
        if (element.type() != Json::OBJECT) {
          char* msg;
          gpr_asprintf(&msg,
                       "field:priorities element:%" PRIdPTR
                       " error:should be type object",
                       i);
          error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
          gpr_free(msg);
        } else {
          PriorityLbConfig::ChildConfig child;
          auto it2 = element.object_value().find("name");
          if (it2 == element.object_value().end()) {
            char* msg;
            gpr_asprintf(&msg,
                         "field:priorities element:%" PRIdPTR
                         " error:missing 'name' field",
                         i);
            error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
            gpr_free(msg);
          } else {
            child.name = it2->second.string_value();
          }
          it2 = element.object_value().find("config");
          if (it2 == element.object_value().end()) {
            char* msg;
            gpr_asprintf(&msg,
                         "field:priorities element:%" PRIdPTR
                         " error:missing 'config' field",
                         i);
            error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
            gpr_free(msg);
          } else {
            grpc_error* parse_error = GRPC_ERROR_NONE;
            child.config =
                LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
                    it2->second, &parse_error);
            if (child.config == nullptr) {
              GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
              error_list.push_back(parse_error);
            }
          }
        }
      }
    }
    // Failover timeout.
    grpc_millis failover_timeout = 10000;
    it = json.object_value().find("failoverTimeout");
    if (it != json.object_value().end()) {
      if (!ParseDuration(it->second, &failover_timeout)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:failoverTimeout error:Failed to parse"));
      } else if (failover_timeout == 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:failoverTimeout error:must be greater than 0"));
      }
    }
    // Retention timeout.
    grpc_millis retention_timeout = 10000;
    it = json.object_value().find("retentionTimeout");
    if (it != json.object_value().end()) {
      if (!ParseDuration(it->second, &retention_timeout)) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retentionTimeout error:Failed to parse"));
      } else if (failover_timeout == 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:retentionTimeout error:must be greater than 0"));
      }
    }
    if (error_list.empty()) {
      return MakeRefCounted<PriorityLbConfig>(
          std::move(priorities), failover_timeout, retention_timeout);
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("Priority Parser", &error_list);
      return nullptr;
    }
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_xds_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::MakeUnique<grpc_core::PriorityLbFactory>());
}

void grpc_lb_policy_xds_shutdown() {}
