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

#define GRPC_NON_LEAF_WRR_CHILD_RETENTION_INTERVAL_MS (15 * 60 * 1000)

namespace grpc_core {

TraceFlag grpc_lb_weighted_target_trace(false, "weighted_target_lb");

namespace {

constexpr char kWeightedTarget[] = "weighted_target_experimental";

class WeightedTargetLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct ChildConfig {
    uint32_t weight;
    RefCountedPtr<LoadBalancingPolicy::Config> config;
  };

  using TargetMap = std::map<std::string, ChildConfig>;

  explicit WeightedTargetLbConfig(TargetMap target_map)
      : target_map_(std::move(target_map)) {}

  const char* name() const override { return kWeightedTarget; }

  const TargetMap& target_map() const { return target_map_; }

 private:
  TargetMap target_map_;
};

class WeightedTargetLb : public LoadBalancingPolicy {
 public:
  explicit WeightedTargetLb(Args args);

  const char* name() const override { return kWeightedTarget; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

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
  class WeightedPicker : public SubchannelPicker {
   public:
    // Maintains a weighted list of pickers from each child that is in
    // ready state. The first element in the pair represents the end of a
    // range proportional to the child's weight. The start of the range
    // is the previous value in the vector and is 0 for the first element.
    using PickerList =
        InlinedVector<std::pair<uint32_t,
                                RefCountedPtr<ChildPickerWrapper>>, 1>;

    WeightedPicker(RefCountedPtr<WeightedTargetLb> parent, PickerList pickers)
        : parent_(std::move(parent)), pickers_(std::move(pickers)) {}
    ~WeightedPicker() { parent_.reset(DEBUG_LOCATION, "WeightedPicker"); }

    PickResult Pick(PickArgs args) override;

   private:
    RefCountedPtr<WeightedTargetLb> parent_;
    PickerList pickers_;
  };

  // Each WeightedChild holds a ref to its parent WeightedTargetLb.
  class WeightedChild : public InternallyRefCounted<WeightedChild> {
   public:
    WeightedChild(RefCountedPtr<WeightedTargetLb> weighted_target_policy, std::string name);
    ~WeightedChild();

    void Orphan() override;

    void UpdateLocked(const WeightedTargetLbConfig::ChildConfig& config,
                      const ServerAddressList& addresses,
                      const grpc_channel_args* args);
    void ResetBackoffLocked();
    void DeactivateLocked();

    uint32_t weight() const { return weight_; }
    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }
    RefCountedPtr<ChildPickerWrapper> picker_wrapper() const {
      return picker_wrapper_;
    }

   private:
    class Helper : public ChannelControlHelper {
     public:
      explicit Helper(RefCountedPtr<WeightedChild> weighted_child)
          : weighted_child_(std::move(weighted_child)) {}

      ~Helper() { weighted_child_.reset(DEBUG_LOCATION, "Helper"); }

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

      RefCountedPtr<WeightedChild> weighted_child_;
      LoadBalancingPolicy* child_ = nullptr;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const char* name, const grpc_channel_args* args);

    static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
    static void OnDelayedRemovalTimerLocked(void* arg, grpc_error* error);

    // The owning LB policy.
    RefCountedPtr<WeightedTargetLb> weighted_target_policy_;
    const std::string name_;

    uint32_t weight_;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;
    OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;

    RefCountedPtr<ChildPickerWrapper> picker_wrapper_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;

    // States for delayed removal.
    grpc_timer delayed_removal_timer_;
    grpc_closure on_delayed_removal_timer_;
    bool delayed_removal_timer_callback_pending_ = false;
    bool shutdown_ = false;
  };

  ~WeightedTargetLb();

  void ShutdownLocked() override;

  void UpdateStateLocked();

  const grpc_millis child_retention_interval_ms_;

  // Current config from the resolver.
  RefCountedPtr<WeightedTargetLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // Children.
// FIXME: maybe key this by StringView, with string stored in value obj?
  std::map<std::string, OrphanablePtr<WeightedChild>> targets_;
};

//
// WeightedTargetLb::WeightedPicker
//

WeightedTargetLb::PickResult WeightedTargetLb::WeightedPicker::Pick(
    PickArgs args) {
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
// WeightedTargetLb
//

WeightedTargetLb::WeightedTargetLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      child_retention_interval_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_LOCALITY_RETENTION_INTERVAL_MS,
          {GRPC_NON_LEAF_WRR_CHILD_RETENTION_INTERVAL_MS, 0, INT_MAX})) {}

WeightedTargetLb::~WeightedTargetLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] destroying weighted_target LB policy",
            this);
  }
}

void WeightedTargetLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  targets_.clear();
}

void WeightedTargetLb::ResetBackoffLocked() {
  for (auto& p : targets_) p.second->ResetBackoffLocked();
}

void WeightedTargetLb::UpdateLocked(UpdateArgs args) {
  if (shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] Received update", this);
  }
  // Update config.
  config_ = std::move(args.config);
  // Deactivate the targets not in the new config.
  for (auto iter = targets_.begin(); iter != targets_.end();) {
    const std::string& name = iter->first;
    WeightedChild* child = iter->second.get();
    if (config_->target_map().find(name) != config_->target_map().end()) {
      ++iter;
      continue;
    }
    if (child_retention_interval_ms_ == 0) {
      iter = targets_.erase(iter);
    } else {
      child->DeactivateLocked();
      ++iter;
    }
  }
  // Add or update the targets in the new config.
  for (const auto& p : config_->target_map()) {
    const std::string& name = p.first;
    const WeightedTargetLbConfig::ChildConfig& config = p.second;
    OrphanablePtr<WeightedChild>& child = targets_[name];
    if (child == nullptr) {
      child = MakeOrphanable<WeightedChild>(
          Ref(DEBUG_LOCATION, "WeightedChild"), name);
    }
    child->UpdateLocked(config, args.addresses, args.args);
  }
}

void WeightedTargetLb::UpdateStateLocked() {
  // Construct a new picker which maintains a map of all child pickers
  // that are ready. Each child is represented by a portion of the range
  // proportional to its weight, such that the total range is the sum of the
  // weights of all children.
  WeightedPicker::PickerList picker_list;
  uint32_t end = 0;
  // Also count the number of children in each state, to determine the
  // overall state.
  size_t num_connecting = 0;
  size_t num_idle = 0;
  size_t num_transient_failures = 0;
  for (const auto& p : targets_) {
    const auto& child_name = p.first;
    const WeightedChild* child = p.second.get();
    // Skip the targets that are not in the latest update.
    if (config_->target_map().find(child_name) == config_->target_map().end()) {
      continue;
    }
    switch (child->connectivity_state()) {
      case GRPC_CHANNEL_READY: {
        end += child->weight();
        picker_list.push_back(std::make_pair(end, child->picker_wrapper()));
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
  // Determine aggregated connectivity state.
// FIXME: change to new use semantics for TF
  grpc_connectivity_state connectivity_state;
  if (picker_list.size() > 0) {
    connectivity_state = GRPC_CHANNEL_READY;
  } else if (num_connecting > 0) {
    connectivity_state = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
// FIXME: implement ExitIdleLocked()
    connectivity_state = GRPC_CHANNEL_IDLE;
  } else {
    connectivity_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] connectivity changed to %s",
            this, ConnectivityStateName(connectivity_state));
  }
  std::unique_ptr<SubchannelPicker> picker;
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY:
      picker = absl::make_unique<WeightedPicker>(
          Ref(DEBUG_LOCATION, "WeightedPicker"), std::move(picker_list));
      break;
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
      picker = absl::make_unique<QueuePicker>(
          Ref(DEBUG_LOCATION, "QueuePicker"));
      break;
    default:
      picker = absl::make_unique<TransientFailurePicker>(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "weighted_target: all children report state TRANSIENT_FAILURE"));
  }
  channel_control_helper()->UpdateState(connectivity_state, std::move(picker));
}

//
// WeightedTargetLb::WeightedChild
//

WeightedTargetLb::WeightedChild::WeightedChild(
    RefCountedPtr<WeightedTargetLb> weighted_target_policy, std::string name)
    : weighted_target_policy_(std::move(weighted_target_policy)),
      name_(std::move(name)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] created WeightedChild %p for %s",
            weighted_target_policy_.get(), this, name_.c_str());
  }
}

WeightedTargetLb::WeightedChild::~WeightedChild() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: destroying child",
            weighted_target_policy_.get(), this, name_.c_str());
  }
  weighted_target_policy_.reset(DEBUG_LOCATION, "WeightedChild");
}

void WeightedTargetLb::WeightedChild::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: shutting down child",
            weighted_target_policy_.get(), this, name_.c_str());
  }
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   weighted_target_policy_->interested_parties());
  child_policy_.reset();
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(),
        weighted_target_policy_->interested_parties());
    pending_child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_wrapper_.reset();
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  shutdown_ = true;
  Unref();
}

OrphanablePtr<LoadBalancingPolicy>
WeightedTargetLb::WeightedChild::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = new Helper(this->Ref(DEBUG_LOCATION, "Helper"));
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = weighted_target_policy_->combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::unique_ptr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR,
            "[weighted_target_lb %p] WeightedChild %p %s: failure creating child "
            "policy %s",
            weighted_target_policy_.get(), this, name_.c_str(), name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: Created new child policy "
            "%s (%p)",
            weighted_target_policy_.get(), this, name_.c_str(), name,
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   weighted_target_policy_->interested_parties());
  return lb_policy;
}

void WeightedTargetLb::WeightedChild::UpdateLocked(
    const WeightedTargetLbConfig::ChildConfig& config,
    const ServerAddressList& addresses, const grpc_channel_args* args) {
  if (weighted_target_policy_->shutting_down_) return;
  // Update child weight.
  weight_ = config.weight;
  // Reactivate if needed.
  if (delayed_removal_timer_callback_pending_) {
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = config.config;
  update_args.addresses = addresses;
  update_args.args = grpc_channel_args_copy(args);
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
      gpr_log(GPR_INFO,
              "[weighted_target_lb %p] WeightedChild %p %s: Creating new %schild "
              "policy %s",
              weighted_target_policy_.get(), this, name_.c_str(),
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: Updating %schild policy %p",
            weighted_target_policy_.get(), this, name_.c_str(),
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

void WeightedTargetLb::WeightedChild::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void WeightedTargetLb::WeightedChild::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (weight_ == 0) return;
  // Set the child weight to 0 so that future picker won't contain this child.
  weight_ = 0;
  // Start a timer to delete the child.
  Ref(DEBUG_LOCATION, "WeightedChild+timer").release();
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(
      &delayed_removal_timer_,
      ExecCtx::Get()->Now() +
          weighted_target_policy_->child_retention_interval_ms_,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

void WeightedTargetLb::WeightedChild::OnDelayedRemovalTimer(void* arg,
                                                   grpc_error* error) {
  WeightedChild* self = static_cast<WeightedChild*>(arg);
  self->weighted_target_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_delayed_removal_timer_,
                        OnDelayedRemovalTimerLocked, self, nullptr),
      GRPC_ERROR_REF(error));
}

void WeightedTargetLb::WeightedChild::OnDelayedRemovalTimerLocked(
    void* arg, grpc_error* error) {
  WeightedChild* self = static_cast<WeightedChild*>(arg);
  self->delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->shutdown_ && self->weight_ == 0) {
    self->weighted_target_policy_->targets_.erase(self->name_);
  }
  self->Unref(DEBUG_LOCATION, "WeightedChild+timer");
}

//
// WeightedTargetLb::WeightedChild::Helper
//

bool WeightedTargetLb::WeightedChild::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == weighted_child_->pending_child_policy_.get();
}

bool WeightedTargetLb::WeightedChild::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == weighted_child_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface>
WeightedTargetLb::WeightedChild::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (weighted_child_->weighted_target_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return weighted_child_->weighted_target_policy_->channel_control_helper()->CreateSubchannel(
      args);
}

void WeightedTargetLb::WeightedChild::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (weighted_child_->weighted_target_policy_->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
      gpr_log(GPR_INFO,
              "[weighted_target_lb %p helper %p] pending child policy %p reports "
              "state=%s",
              weighted_child_->weighted_target_policy_.get(), this,
              weighted_child_->pending_child_policy_.get(),
              ConnectivityStateName(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        weighted_child_->child_policy_->interested_parties(),
        weighted_child_->weighted_target_policy_->interested_parties());
    weighted_child_->child_policy_ =
        std::move(weighted_child_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Cache the picker and its state in the WeightedChild.
  weighted_child_->picker_wrapper_ =
      MakeRefCounted<ChildPickerWrapper>(std::move(picker));
  weighted_child_->connectivity_state_ = state;
  // Notify the LB policy.
  weighted_child_->weighted_target_policy_->UpdateStateLocked();
}

void WeightedTargetLb::WeightedChild::Helper::RequestReresolution() {
  if (weighted_child_->weighted_target_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  weighted_child_->weighted_target_policy_->channel_control_helper()->RequestReresolution();
}

void WeightedTargetLb::WeightedChild::Helper::AddTraceEvent(TraceSeverity severity,
                                                        StringView message) {
  if (weighted_child_->weighted_target_policy_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  weighted_child_->weighted_target_policy_->channel_control_helper()->AddTraceEvent(
      severity, message);
}

//
// factory
//

class WeightedTargetLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<WeightedTargetLb>(std::move(args));
  }

  const char* name() const override { return kWeightedTarget; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // weighted_target was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:weighted_target policy requires "
          "configuration.  Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // Weight map.
    WeightedTargetLbConfig::TargetMap target_map;
    auto it = json.object_value().find("targets");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:targets error:required field not present"));
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:targets error:type should be object"));
    } else {
      for (const auto& p : it->second.object_value()) {
        WeightedTargetLbConfig::ChildConfig child_config;
        std::vector<grpc_error*> child_errors =
            ParseChildConfig(p.second, &child_config);
        if (!child_errors.empty()) {
          // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
          // string is not static in this case.
          char* msg;
          gpr_asprintf(&msg, "field:targets key:%s", p.first.c_str());
          grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
          gpr_free(msg);
          for (grpc_error* child_error : child_errors) {
            error = grpc_error_add_child(error, child_error);
          }
          error_list.push_back(error);
        } else {
          target_map[p.first] = std::move(child_config);
        }
      }
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "weighted_target_experimental LB policy config", &error_list);
      return nullptr;
    }
    return MakeRefCounted<WeightedTargetLbConfig>(std::move(target_map));
  }

 private:
  static std::vector<grpc_error*> ParseChildConfig(
      const Json& json, WeightedTargetLbConfig::ChildConfig* child_config) {
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
          "required field \"weight\" not specified"));
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
      if (child_config->config == nullptr) {
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

void grpc_lb_policy_weighted_target_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::WeightedTargetLbFactory>());
}

void grpc_lb_policy_weighted_target_shutdown() {}
