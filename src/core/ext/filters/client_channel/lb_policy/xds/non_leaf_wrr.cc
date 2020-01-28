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

TraceFlag grpc_lb_non_leaf_wrr_trace(false, "non_leaf_wrr_lb");

namespace {

constexpr char kNonLeafWrr[] = "non_leaf_wrr";

class NonLeafWrrLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct ChildConfig {
    uint32_t weight;
    RefCountedPtr<LoadBalancingPolicy::Config> config;
  };

  using WeightMap = std::map<std::string, ChildConfig>;

  explicit NonLeafWrrLbConfig(WeightMap weight_map)
      : weight_map_(std::move(weight_map)) {}

  const char* name() const override { return kNonLeafWrr; }

  const WeightMap& weight_map() const { return weight_map_; }

 private:
  WeightMap weight_map_;
};

class NonLeafWrrLb : public LoadBalancingPolicy {
 public:
  explicit NonLeafWrrLb(Args args);

  const char* name() const override { return kNonLeafWrr; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Each LocalityMap holds a ref to the NonLeafWrrLb.
  class LocalityMap : public InternallyRefCounted<LocalityMap> {
   public:
    // Each Locality holds a ref to the LocalityMap it is in.
    class Locality : public InternallyRefCounted<Locality> {
     public:
      Locality(RefCountedPtr<LocalityMap> locality_map,
               RefCountedPtr<XdsLocalityName> name);
      ~Locality();

      void UpdateLocked(uint32_t locality_weight, ServerAddressList serverlist);
      void ShutdownLocked();
      void ResetBackoffLocked();
      void DeactivateLocked();
      void Orphan() override;

      grpc_connectivity_state connectivity_state() const {
        return connectivity_state_;
      }
      uint32_t weight() const { return weight_; }
      RefCountedPtr<EndpointPickerWrapper> picker_wrapper() const {
        return picker_wrapper_;
      }

      void set_locality_map(RefCountedPtr<LocalityMap> locality_map) {
        locality_map_ = std::move(locality_map);
      }

     private:
      // A simple wrapper for ref-counting a picker from the child policy.
      class ChildPickerWrapper : public RefCounted<ChildPickerWrapper> {
       public:
        explicit ChildPickerWrapper(std::unique_ptr<SubchannelPicker> picker)
            : picker_(std::move(picker)) {}
        PickResult Pick(PickArgs args) {
          return picker_->Pick(std::move(args));
        }
       private:
        std::unique_ptr<SubchannelPicker> picker_;
      };

      // Picks a child using stateless WRR and then delegates to that
      // child's picker.
      class LocalityPicker : public SubchannelPicker {
       public:
        // Maintains a weighted list of pickers from each locality that is in
        // ready state. The first element in the pair represents the end of a
        // range proportional to the locality's weight. The start of the range
        // is the previous value in the vector and is 0 for the first element.
        using PickerList =
            InlinedVector<std::pair<uint32_t,
                                    RefCountedPtr<ChildPickerWrapper>>, 1>;

        LocalityPicker(RefCountedPtr<NonLeafWrrLb> parent, PickerList pickers)
            : parent_(std::move(parent)), pickers_(std::move(pickers)) {}
        ~LocalityPicker() { parent_.reset(DEBUG_LOCATION, "LocalityPicker"); }

        PickResult Pick(PickArgs args) override;

       private:
        RefCountedPtr<NonLeafWrrLb> parent_;
        PickerList pickers_;
      };

      class Helper : public ChannelControlHelper {
       public:
        explicit Helper(RefCountedPtr<Locality> locality)
            : locality_(std::move(locality)) {}

        ~Helper() { locality_.reset(DEBUG_LOCATION, "Helper"); }

        RefCountedPtr<SubchannelInterface> CreateSubchannel(
            const grpc_channel_args& args) override;
        void UpdateState(grpc_connectivity_state state,
                         std::unique_ptr<SubchannelPicker> picker) override;
// FIXME: implement this
        // This is a no-op, because we get the addresses from the xds
        // client, which is a watch-based API.
        void RequestReresolution() override {}
        void AddTraceEvent(TraceSeverity severity, StringView message) override;
        void set_child(LoadBalancingPolicy* child) { child_ = child; }

       private:
        bool CalledByPendingChild() const;
        bool CalledByCurrentChild() const;

        RefCountedPtr<Locality> locality_;
        LoadBalancingPolicy* child_ = nullptr;
      };

      // Methods for dealing with the child policy.
      OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
          const char* name, const grpc_channel_args* args);
      grpc_channel_args* CreateChildPolicyArgsLocked(
          const grpc_channel_args* args);

      static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
      static void OnDelayedRemovalTimerLocked(void* arg, grpc_error* error);

      NonLeafWrrLb* non_leaf_wrr_policy() const { return locality_map_->non_leaf_wrr_policy(); }

      // The owning locality map.
      RefCountedPtr<LocalityMap> locality_map_;

      RefCountedPtr<XdsLocalityName> name_;
      OrphanablePtr<LoadBalancingPolicy> child_policy_;
      OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
      RefCountedPtr<EndpointPickerWrapper> picker_wrapper_;
      grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
      uint32_t weight_;

      // States for delayed removal.
      grpc_timer delayed_removal_timer_;
      grpc_closure on_delayed_removal_timer_;
      bool delayed_removal_timer_callback_pending_ = false;
      bool shutdown_ = false;
    };

    LocalityMap(RefCountedPtr<NonLeafWrrLb> non_leaf_wrr_policy, uint32_t priority);

    ~LocalityMap() { non_leaf_wrr_policy_.reset(DEBUG_LOCATION, "LocalityMap"); }

    void UpdateLocked(
        const XdsPriorityListUpdate::LocalityMap& locality_map_update);
    void ResetBackoffLocked();
    void UpdateXdsPickerLocked();

    void Orphan() override;

    NonLeafWrrLb* non_leaf_wrr_policy() const {
      return non_leaf_wrr_policy_.get();
    }
    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

   private:
    void OnLocalityStateUpdateLocked();
    void UpdateConnectivityStateLocked();

    const XdsPriorityListUpdate& priority_list_update() const {
      return non_leaf_wrr_policy_->priority_list_update_;
    }
    const XdsPriorityListUpdate::LocalityMap* locality_map_update() const {
      return non_leaf_wrr_policy_->priority_list_update_.Find(priority_);
    }

    RefCountedPtr<NonLeafWrrLb> non_leaf_wrr_policy_;

    std::map<RefCountedPtr<XdsLocalityName>, OrphanablePtr<Locality>,
             XdsLocalityName::Less>
        localities_;
    const uint32_t priority_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
  };

  ~NonLeafWrrLb();

  void ShutdownLocked() override;

  void UpdateXdsPickerLocked();

  const grpc_millis child_retention_interval_ms_;

  // Current channel args and config from the resolver.
  const grpc_channel_args* args_ = nullptr;
  RefCountedPtr<NonLeafWrrLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

// FIXME: merge LocalityMap functionality into parent class
  OrphanablePtr<LocalityMap> locality_map_;
};

//
// NonLeafWrrLb::LocalityPicker
//

NonLeafWrrLb::PickResult NonLeafWrrLb::LocalityPicker::Pick(PickArgs args) {
  // Handle drop.
  const std::string* drop_category;
  if (drop_config_->ShouldDrop(&drop_category)) {
    non_leaf_wrr_policy_->client_stats_.AddCallDropped(*drop_category);
    PickResult result;
    result.type = PickResult::PICK_COMPLETE;
    return result;
  }
  // Generate a random number in [0, total weight).
  const uint32_t key = rand() % pickers_[pickers_.size() - 1].first;
  // Find the index in pickers_ corresponding to key.
  size_t mid = 0;
  size_t start_index = 0;
  size_t end_index = pickers_.size() - 1;
  size_t index = 0;
  while (end_index > start_index) {
    mid = (start_index + end_index) / 2;
    if (pickers_[mid].first > key) {
      end_index = mid;
    } else if (pickers_[mid].first < key) {
      start_index = mid + 1;
    } else {
      index = mid + 1;
      break;
    }
  }
  if (index == 0) index = start_index;
  GPR_ASSERT(pickers_[index].first > key);
  // Delegate to the child picker.
  return pickers_[index].second->Pick(args);
}

//
// ctor and dtor
//

NonLeafWrrLb::NonLeafWrrLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      child_retention_interval_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_LOCALITY_RETENTION_INTERVAL_MS,
          {GRPC_XDS_DEFAULT_LOCALITY_RETENTION_INTERVAL_MS, 0, INT_MAX})) {}

NonLeafWrrLb::~NonLeafWrrLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] destroying non_leaf_wrr LB policy",
            this);
  }
  grpc_channel_args_destroy(args_);
}

void NonLeafWrrLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  priorities_.clear();
}

//
// public methods
//

void NonLeafWrrLb::ResetBackoffLocked() {
  for (size_t i = 0; i < priorities_.size(); ++i) {
    priorities_[i]->ResetBackoffLocked();
  }
}

void NonLeafWrrLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] Received update", this);
  }
  // Update config.
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  // Update priority list.
  UpdatePrioritiesLocked();
}

//
// priority list-related methods
//

void NonLeafWrrLb::UpdatePrioritiesLocked() {
  // 1. Remove from the priority list the priorities that are not in the update.
  DeactivatePrioritiesLowerThan(priority_list_update_.LowestPriority());
  // 2. Update all the existing priorities.
  for (uint32_t priority = 0; priority < priorities_.size(); ++priority) {
    LocalityMap* locality_map = priorities_[priority].get();
    const auto* locality_map_update = priority_list_update_.Find(priority);
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
    MaybeCreateLocalityMapLocked(new_priority);
  }
}

void NonLeafWrrLb::UpdateXdsPickerLocked() {
  if (current_priority_ == UINT32_MAX) {
    grpc_error* error = grpc_error_set_int(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("no ready locality map"),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        grpc_core::MakeUnique<TransientFailurePicker>(error));
    return;
  }
  priorities_[current_priority_]->UpdateXdsPickerLocked();
}

void NonLeafWrrLb::MaybeCreateLocalityMapLocked(uint32_t priority) {
  // Exhausted priorities in the update.
  if (!priority_list_update_.Contains(priority)) return;
  auto new_locality_map =
      new LocalityMap(Ref(DEBUG_LOCATION, "LocalityMap"), priority);
  priorities_.emplace_back(OrphanablePtr<LocalityMap>(new_locality_map));
  new_locality_map->UpdateLocked(*priority_list_update_.Find(priority));
}

void NonLeafWrrLb::FailoverOnConnectionFailureLocked() {
  const uint32_t failed_priority = LowestPriority();
  // If we're failing over from the lowest priority, report TRANSIENT_FAILURE.
  if (failed_priority == priority_list_update_.LowestPriority()) {
    UpdateXdsPickerLocked();
  }
  MaybeCreateLocalityMapLocked(failed_priority + 1);
}

void NonLeafWrrLb::FailoverOnDisconnectionLocked(uint32_t failed_priority) {
  current_priority_ = UINT32_MAX;
  for (uint32_t next_priority = failed_priority + 1;
       next_priority <= priority_list_update_.LowestPriority();
       ++next_priority) {
    if (!Contains(next_priority)) {
      MaybeCreateLocalityMapLocked(next_priority);
      return;
    }
    if (priorities_[next_priority]->MaybeReactivateLocked()) return;
  }
}

void NonLeafWrrLb::SwitchToHigherPriorityLocked(uint32_t priority) {
  current_priority_ = priority;
  DeactivatePrioritiesLowerThan(current_priority_);
  UpdateXdsPickerLocked();
}

void NonLeafWrrLb::DeactivatePrioritiesLowerThan(uint32_t priority) {
  if (priorities_.empty()) return;
  // Deactivate the locality maps from the lowest priority.
  for (uint32_t p = LowestPriority(); p > priority; --p) {
    if (child_retention_interval_ms_ == 0) {
      priorities_.pop_back();
    } else {
      priorities_[p]->DeactivateLocked();
    }
  }
}

//
// NonLeafWrrLb::LocalityMap
//

NonLeafWrrLb::LocalityMap::LocalityMap(RefCountedPtr<NonLeafWrrLb> non_leaf_wrr_policy,
                                uint32_t priority)
    : non_leaf_wrr_policy_(std::move(non_leaf_wrr_policy)), priority_(priority) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] Creating priority %" PRIu32,
            non_leaf_wrr_policy_.get(), priority_);
  }
  GRPC_CLOSURE_INIT(&on_failover_timer_, OnFailoverTimer, this,
                    grpc_schedule_on_exec_ctx);
  // Start the failover timer.
  Ref(DEBUG_LOCATION, "LocalityMap+OnFailoverTimerLocked").release();
  grpc_timer_init(
      &failover_timer_,
      ExecCtx::Get()->Now() + non_leaf_wrr_policy_->locality_map_failover_timeout_ms_,
      &on_failover_timer_);
  failover_timer_callback_pending_ = true;
  // This is the first locality map ever created, report CONNECTING.
  if (priority_ == 0) {
    non_leaf_wrr_policy_->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING,
        grpc_core::MakeUnique<QueuePicker>(
            non_leaf_wrr_policy_->Ref(DEBUG_LOCATION, "QueuePicker")));
  }
}

void NonLeafWrrLb::LocalityMap::UpdateLocked(
    const XdsPriorityListUpdate::LocalityMap& locality_map_update) {
  if (non_leaf_wrr_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] Start Updating priority %" PRIu32,
            non_leaf_wrr_policy(), priority_);
  }
  // Maybe reactivate the locality map in case all the active locality maps have
  // failed.
  MaybeReactivateLocked();
  // Remove (later) the localities not in locality_map_update.
  for (auto iter = localities_.begin(); iter != localities_.end();) {
    const auto& name = iter->first;
    Locality* locality = iter->second.get();
    if (locality_map_update.Contains(name)) {
      ++iter;
      continue;
    }
    if (non_leaf_wrr_policy()->child_retention_interval_ms_ == 0) {
      iter = localities_.erase(iter);
    } else {
      locality->DeactivateLocked();
      ++iter;
    }
  }
  // Add or update the localities in locality_map_update.
  for (const auto& p : locality_map_update.localities) {
    const auto& name = p.first;
    const auto& locality_update = p.second;
    OrphanablePtr<Locality>& locality = localities_[name];
    if (locality == nullptr) {
      // Move from another locality map if possible.
      locality = non_leaf_wrr_policy_->ExtractLocalityLocked(name, priority_);
      if (locality != nullptr) {
        locality->set_locality_map(
            Ref(DEBUG_LOCATION, "LocalityMap+Locality_move"));
      } else {
        locality = MakeOrphanable<Locality>(
            Ref(DEBUG_LOCATION, "LocalityMap+Locality"), name);
      }
    }
    // Keep a copy of serverlist in the update so that we can compare it
    // with the future ones.
    locality->UpdateLocked(locality_update.lb_weight,
                           locality_update.serverlist);
  }
}

void NonLeafWrrLb::LocalityMap::ResetBackoffLocked() {
  for (auto& p : localities_) p.second->ResetBackoffLocked();
}

void NonLeafWrrLb::LocalityMap::UpdateXdsPickerLocked() {
  // Construct a new xds picker which maintains a map of all locality pickers
  // that are ready. Each locality is represented by a portion of the range
  // proportional to its weight, such that the total range is the sum of the
  // weights of all localities.
  LocalityPicker::PickerList picker_list;
  uint32_t end = 0;
  for (const auto& p : localities_) {
    const auto& locality_name = p.first;
    const Locality* locality = p.second.get();
    // Skip the localities that are not in the latest locality map update.
    if (!locality_map_update()->Contains(locality_name)) continue;
    if (locality->connectivity_state() != GRPC_CHANNEL_READY) continue;
    end += locality->weight();
    picker_list.push_back(std::make_pair(end, locality->picker_wrapper()));
  }
  non_leaf_wrr_policy()->channel_control_helper()->UpdateState(
      GRPC_CHANNEL_READY,
      grpc_core::MakeUnique<LocalityPicker>(
          non_leaf_wrr_policy_->Ref(DEBUG_LOCATION, "LocalityPicker"),
          std::move(picker_list)));
}

void NonLeafWrrLb::LocalityMap::DeactivateLocked() {
  // If already deactivated, don't do it again.
  if (delayed_removal_timer_callback_pending_) return;
  MaybeCancelFailoverTimerLocked();
  // Start a timer to delete the locality.
  Ref(DEBUG_LOCATION, "LocalityMap+timer").release();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[non_leaf_wrr_lb %p] Will remove priority %" PRIu32 " in %" PRId64 " ms.",
            non_leaf_wrr_policy(), priority_,
            non_leaf_wrr_policy()->child_retention_interval_ms_);
  }
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(
      &delayed_removal_timer_,
      ExecCtx::Get()->Now() + non_leaf_wrr_policy()->child_retention_interval_ms_,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

bool NonLeafWrrLb::LocalityMap::MaybeReactivateLocked() {
  // Don't reactivate a priority that is not higher than the current one.
  if (priority_ >= non_leaf_wrr_policy_->current_priority_) return false;
  // Reactivate this priority by cancelling deletion timer.
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Switch to this higher priority if it's READY.
  if (connectivity_state_ != GRPC_CHANNEL_READY) return false;
  non_leaf_wrr_policy_->SwitchToHigherPriorityLocked(priority_);
  return true;
}

void NonLeafWrrLb::LocalityMap::MaybeCancelFailoverTimerLocked() {
  if (failover_timer_callback_pending_) grpc_timer_cancel(&failover_timer_);
}

void NonLeafWrrLb::LocalityMap::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] Priority %" PRIu32 " orphaned.", non_leaf_wrr_policy(),
            priority_);
  }
  MaybeCancelFailoverTimerLocked();
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  localities_.clear();
  Unref(DEBUG_LOCATION, "LocalityMap+Orphan");
}

void NonLeafWrrLb::LocalityMap::OnLocalityStateUpdateLocked() {
  UpdateConnectivityStateLocked();
  // Ignore priorities not in priority_list_update.
  if (!priority_list_update().Contains(priority_)) return;
  const uint32_t current_priority = non_leaf_wrr_policy_->current_priority_;
  // Ignore lower-than-current priorities.
  if (priority_ > current_priority) return;
  // Update is for a higher-than-current priority. (Special case: update is for
  // any active priority if there is no current priority.)
  if (priority_ < current_priority) {
    if (connectivity_state_ == GRPC_CHANNEL_READY) {
      MaybeCancelFailoverTimerLocked();
      // If a higher-than-current priority becomes READY, switch to use it.
      non_leaf_wrr_policy_->SwitchToHigherPriorityLocked(priority_);
    } else if (connectivity_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // If a higher-than-current priority becomes TRANSIENT_FAILURE, only
      // handle it if it's the priority that is still in failover timeout.
      if (failover_timer_callback_pending_) {
        MaybeCancelFailoverTimerLocked();
        non_leaf_wrr_policy_->FailoverOnConnectionFailureLocked();
      }
    }
    return;
  }
  // Update is for current priority.
  if (connectivity_state_ != GRPC_CHANNEL_READY) {
    // Fail over if it's no longer READY.
    non_leaf_wrr_policy_->FailoverOnDisconnectionLocked(priority_);
  }
  // At this point, one of the following things has happened to the current
  // priority.
  // 1. It remained the same (but received picker update from its localities).
  // 2. It changed to a lower priority due to failover.
  // 3. It became invalid because failover didn't yield a READY priority.
  // In any case, update the xds picker.
  non_leaf_wrr_policy_->UpdateXdsPickerLocked();
}

void NonLeafWrrLb::LocalityMap::UpdateConnectivityStateLocked() {
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[non_leaf_wrr_lb %p] Priority %" PRIu32 " (%p) connectivity changed to %s",
            non_leaf_wrr_policy(), priority_, this,
            ConnectivityStateName(connectivity_state_));
  }
}

void NonLeafWrrLb::LocalityMap::OnDelayedRemovalTimer(void* arg, grpc_error* error) {
  LocalityMap* self = static_cast<LocalityMap*>(arg);
  self->non_leaf_wrr_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_delayed_removal_timer_,
                        OnDelayedRemovalTimerLocked, self, nullptr),
      GRPC_ERROR_REF(error));
}

void NonLeafWrrLb::LocalityMap::OnDelayedRemovalTimerLocked(void* arg,
                                                     grpc_error* error) {
  LocalityMap* self = static_cast<LocalityMap*>(arg);
  self->delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->non_leaf_wrr_policy_->shutting_down_) {
    const bool keep = self->priority_list_update().Contains(self->priority_) &&
                      self->priority_ <= self->non_leaf_wrr_policy_->current_priority_;
    if (!keep) {
      // This check is to make sure we always delete the locality maps from
      // the lowest priority even if the closures of the back-to-back timers
      // are not run in FIFO order.
      // TODO(juanlishen): Eliminate unnecessary maintenance overhead for some
      // deactivated locality maps when out-of-order closures are run.
      // TODO(juanlishen): Check the timer implementation to see if this
      // defense is necessary.
      if (self->priority_ == self->non_leaf_wrr_policy_->LowestPriority()) {
        self->non_leaf_wrr_policy_->priorities_.pop_back();
      } else {
        gpr_log(GPR_ERROR,
                "[non_leaf_wrr_lb %p] Priority %" PRIu32
                " is not the lowest priority (highest numeric value) but is "
                "attempted to be deleted.",
                self->non_leaf_wrr_policy(), self->priority_);
      }
    }
  }
  self->Unref(DEBUG_LOCATION, "LocalityMap+timer");
}

void NonLeafWrrLb::LocalityMap::OnFailoverTimer(void* arg, grpc_error* error) {
  LocalityMap* self = static_cast<LocalityMap*>(arg);
  self->non_leaf_wrr_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_failover_timer_, OnFailoverTimerLocked, self,
                        nullptr),
      GRPC_ERROR_REF(error));
}

void NonLeafWrrLb::LocalityMap::OnFailoverTimerLocked(void* arg, grpc_error* error) {
  LocalityMap* self = static_cast<LocalityMap*>(arg);
  self->failover_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->non_leaf_wrr_policy_->shutting_down_) {
    self->non_leaf_wrr_policy_->FailoverOnConnectionFailureLocked();
  }
  self->Unref(DEBUG_LOCATION, "LocalityMap+OnFailoverTimerLocked");
}

//
// NonLeafWrrLb::LocalityMap::Locality
//

NonLeafWrrLb::LocalityMap::Locality::Locality(RefCountedPtr<LocalityMap> locality_map,
                                       RefCountedPtr<XdsLocalityName> name)
    : locality_map_(std::move(locality_map)), name_(std::move(name)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] created Locality %p for %s", non_leaf_wrr_policy(),
            this, name_->AsHumanReadableString());
  }
}

NonLeafWrrLb::LocalityMap::Locality::~Locality() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] Locality %p %s: destroying locality",
            non_leaf_wrr_policy(), this, name_->AsHumanReadableString());
  }
  locality_map_.reset(DEBUG_LOCATION, "Locality");
}

grpc_channel_args* NonLeafWrrLb::LocalityMap::Locality::CreateChildPolicyArgsLocked(
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
NonLeafWrrLb::LocalityMap::Locality::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = new Helper(this->Ref(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = non_leaf_wrr_policy()->combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR,
            "[non_leaf_wrr_lb %p] Locality %p %s: failure creating child policy %s",
            non_leaf_wrr_policy(), this, name_->AsHumanReadableString(), name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO,
            "[non_leaf_wrr_lb %p] Locality %p %s: Created new child policy %s (%p)",
            non_leaf_wrr_policy(), this, name_->AsHumanReadableString(), name,
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   non_leaf_wrr_policy()->interested_parties());
  return lb_policy;
}

void NonLeafWrrLb::LocalityMap::Locality::UpdateLocked(uint32_t locality_weight,
                                                ServerAddressList serverlist) {
  if (non_leaf_wrr_policy()->shutting_down_) return;
  // Update locality weight.
  weight_ = locality_weight;
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(serverlist);
  update_args.config = non_leaf_wrr_policy()->config_->child_policy();
  update_args.args = CreateChildPolicyArgsLocked(non_leaf_wrr_policy()->args_);
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[non_leaf_wrr_lb %p] Locality %p %s: Creating new %schild policy %s",
              non_leaf_wrr_policy(), this, name_->AsHumanReadableString(),
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] Locality %p %s: Updating %schild policy %p",
            non_leaf_wrr_policy(), this, name_->AsHumanReadableString(),
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

void NonLeafWrrLb::LocalityMap::Locality::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
    gpr_log(GPR_INFO, "[non_leaf_wrr_lb %p] Locality %p %s: shutting down locality",
            non_leaf_wrr_policy(), this, name_->AsHumanReadableString());
  }
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   non_leaf_wrr_policy()->interested_parties());
  child_policy_.reset();
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(),
        non_leaf_wrr_policy()->interested_parties());
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

void NonLeafWrrLb::LocalityMap::Locality::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void NonLeafWrrLb::LocalityMap::Locality::Orphan() {
  ShutdownLocked();
  Unref();
}

void NonLeafWrrLb::LocalityMap::Locality::DeactivateLocked() {
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
      ExecCtx::Get()->Now() + non_leaf_wrr_policy()->child_retention_interval_ms_,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

void NonLeafWrrLb::LocalityMap::Locality::OnDelayedRemovalTimer(void* arg,
                                                         grpc_error* error) {
  Locality* self = static_cast<Locality*>(arg);
  self->non_leaf_wrr_policy()->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_delayed_removal_timer_,
                        OnDelayedRemovalTimerLocked, self, nullptr),
      GRPC_ERROR_REF(error));
}

void NonLeafWrrLb::LocalityMap::Locality::OnDelayedRemovalTimerLocked(
    void* arg, grpc_error* error) {
  Locality* self = static_cast<Locality*>(arg);
  self->delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->shutdown_ && self->weight_ == 0) {
    self->locality_map_->localities_.erase(self->name_);
  }
  self->Unref(DEBUG_LOCATION, "Locality+timer");
}

//
// NonLeafWrrLb::Locality::Helper
//

bool NonLeafWrrLb::LocalityMap::Locality::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == locality_->pending_child_policy_.get();
}

bool NonLeafWrrLb::LocalityMap::Locality::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == locality_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface>
NonLeafWrrLb::LocalityMap::Locality::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (locality_->non_leaf_wrr_policy()->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return locality_->non_leaf_wrr_policy()->channel_control_helper()->CreateSubchannel(
      args);
}

void NonLeafWrrLb::LocalityMap::Locality::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (locality_->non_leaf_wrr_policy()->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_non_leaf_wrr_trace)) {
      gpr_log(GPR_INFO,
              "[non_leaf_wrr_lb %p helper %p] pending child policy %p reports state=%s",
              locality_->non_leaf_wrr_policy(), this,
              locality_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        locality_->child_policy_->interested_parties(),
        locality_->non_leaf_wrr_policy()->interested_parties());
    locality_->child_policy_ = std::move(locality_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Cache the picker and its state in the locality.
  // TODO(roth): If load reporting is not configured, we should ideally
  // pass a null LocalityStats ref to the EndpointPickerWrapper and have it
  // not collect any stats, since they're not going to be used.  This would
  // require recreating all of the pickers whenever we get a config update.
  locality_->picker_wrapper_ = MakeRefCounted<EndpointPickerWrapper>(
      std::move(picker),
      locality_->non_leaf_wrr_policy()->client_stats_.FindLocalityStats(
          locality_->name_));
  locality_->connectivity_state_ = state;
  // Notify the locality map.
  locality_->locality_map_->OnLocalityStateUpdateLocked();
}

void NonLeafWrrLb::LocalityMap::Locality::Helper::AddTraceEvent(TraceSeverity severity,
                                                         StringView message) {
  if (locality_->non_leaf_wrr_policy()->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  locality_->non_leaf_wrr_policy()->channel_control_helper()->AddTraceEvent(severity,
                                                                   message);
}

//
// factory
//

class NonLeafWrrLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<NonLeafWrrLb>(std::move(args));
  }

  const char* name() const override { return kNonLeafWrr; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // non_leaf_wrr was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:non_leaf_wrr policy requires "
          "configuration.  Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // Weight map.
    NonLeafWrrLbConfig::WeightMap weight_map;
    auto it = json.object_value().find("weights");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:weights error:required field not present"));
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:weights error:type should be object"));
    } else {
      for (const auto& p : it->second.object_value()) {
        NonLeafWrrLbConfig::ChildConfig child_config;
        std::vector<grpc_error*> child_errors =
            ParseChildConfig(it->second, &child_config);
        if (!child_errors.empty()) {
          // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
          // string is not static in this case.
          char* msg;
          gpr_asprintf(&msg, "field:weights key:%s", p.first.c_str());
          grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
          gpr_free(msg);
          for (grpc_error* child_error : child_errors) {
            error = grpc_error_add_child(error, child_error);
          }
          error_list.push_back(error);
        } else {
          weight_map[p.first] = std::move(child_config);
        }
      }
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("PriorityLb Parser", &error_list);
      return nullptr;
    }
    return MakeRefCounted<NonLeafWrrLbConfig>(std::move(weight_map));
  }

 private:
  static std::vector<grpc_error*> ParseChildConfig(
      const Json& json, NonLeafWrrLbConfig::ChildConfig* child_config) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    // Weight.
    auto it = json.object_value().find("weight");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "require field \"weight\" not specified"));
    } else if (it->second.type() != Json::Type::NUMBER) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:weight error:must be of type number"));
    } else {
      child_config->weight =
          gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (child_config->weight == -1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:weight error:unparseable value"));
      } else if (child_config->weight == 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:weight error:value must be greater than zero"));
      }
    }
    // Child policy.
    it = json.object_value().find("childPolicy");
    if (it != json.object_value().end()) {
      grpc_error* parse_error = GRPC_ERROR_NONE;
      child_config->config =
          LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(it->second,
                                                                &parse_error);
      if (child_policy == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        std::vector<grpc_error*> child_errors;
        child_errors.push_back(parse_error);
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_VECTOR("field:childPolicy", &child_errors));
      }
    }
    return error_list;
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_non_leaf_wrr_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::MakeUnique<grpc_core::NonLeafWrrLbFactory>());
}

void grpc_lb_policy_non_leaf_wrr_shutdown() {}
