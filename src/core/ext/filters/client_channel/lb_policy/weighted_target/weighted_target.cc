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
#include <string.h>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

TraceFlag grpc_lb_weighted_target_trace(false, "weighted_target_lb");

namespace {

constexpr char kWeightedTarget[] = "weighted_target_experimental";

// How long we keep a child around for after it has been removed from
// the config.
constexpr int kChildRetentionIntervalMs = 15 * 60 * 1000;

// Config for weighted_target LB policy.
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

// weighted_target LB policy.
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
    PickResult Pick(PickArgs args) { return picker_->Pick(args); }

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
    using PickerList = absl::InlinedVector<
        std::pair<uint32_t, RefCountedPtr<ChildPickerWrapper>>, 1>;

    explicit WeightedPicker(PickerList pickers)
        : pickers_(std::move(pickers)) {}

    PickResult Pick(PickArgs args) override;

   private:
    PickerList pickers_;
  };

  // Each WeightedChild holds a ref to its parent WeightedTargetLb.
  class WeightedChild : public InternallyRefCounted<WeightedChild> {
   public:
    WeightedChild(RefCountedPtr<WeightedTargetLb> weighted_target_policy,
                  const std::string& name);
    ~WeightedChild() override;

    void Orphan() override;

    void UpdateLocked(const WeightedTargetLbConfig::ChildConfig& config,
                      absl::StatusOr<ServerAddressList> addresses,
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

      ~Helper() override { weighted_child_.reset(DEBUG_LOCATION, "Helper"); }

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          ServerAddress address, const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      absl::string_view GetAuthority() override;
      void AddTraceEvent(TraceSeverity severity,
                         absl::string_view message) override;

     private:
      RefCountedPtr<WeightedChild> weighted_child_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const grpc_channel_args* args);

    void OnConnectivityStateUpdateLocked(
        grpc_connectivity_state state, const absl::Status& status,
        std::unique_ptr<SubchannelPicker> picker);

    static void OnDelayedRemovalTimer(void* arg, grpc_error_handle error);
    void OnDelayedRemovalTimerLocked(grpc_error_handle error);

    // The owning LB policy.
    RefCountedPtr<WeightedTargetLb> weighted_target_policy_;

    const std::string name_;

    uint32_t weight_;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    RefCountedPtr<ChildPickerWrapper> picker_wrapper_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_CONNECTING;
    bool seen_failure_since_ready_ = false;

    // States for delayed removal.
    grpc_timer delayed_removal_timer_;
    grpc_closure on_delayed_removal_timer_;
    bool delayed_removal_timer_callback_pending_ = false;
    bool shutdown_ = false;
  };

  ~WeightedTargetLb() override;

  void ShutdownLocked() override;

  void UpdateStateLocked();

  // Current config from the resolver.
  RefCountedPtr<WeightedTargetLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // Children.
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
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] created", this);
  }
}

WeightedTargetLb::~WeightedTargetLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] destroying weighted_target LB policy",
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
  for (const auto& p : targets_) {
    const std::string& name = p.first;
    WeightedChild* child = p.second.get();
    if (config_->target_map().find(name) == config_->target_map().end()) {
      child->DeactivateLocked();
    }
  }
  // Create any children that don't already exist.
  // Note that we add all children before updating any of them, because
  // an update may trigger a child to immediately update its
  // connectivity state (e.g., reporting TRANSIENT_FAILURE immediately when
  // receiving an empty address list), and we don't want to return an
  // overall state with incomplete data.
  for (const auto& p : config_->target_map()) {
    const std::string& name = p.first;
    auto it = targets_.find(name);
    if (it == targets_.end()) {
      targets_.emplace(name, MakeOrphanable<WeightedChild>(
                                 Ref(DEBUG_LOCATION, "WeightedChild"), name));
    }
  }
  // Update all children.
  absl::StatusOr<HierarchicalAddressMap> address_map =
      MakeHierarchicalAddressMap(args.addresses);
  for (const auto& p : config_->target_map()) {
    const std::string& name = p.first;
    const WeightedTargetLbConfig::ChildConfig& config = p.second;
    absl::StatusOr<ServerAddressList> addresses;
    if (address_map.ok()) {
      addresses = std::move((*address_map)[name]);
    } else {
      addresses = address_map.status();
    }
    targets_[name]->UpdateLocked(config, std::move(addresses), args.args);
  }
  UpdateStateLocked();
}

void WeightedTargetLb::UpdateStateLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] scanning children to determine "
            "connectivity state",
            this);
  }
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
    const std::string& child_name = p.first;
    const WeightedChild* child = p.second.get();
    // Skip the targets that are not in the latest update.
    if (config_->target_map().find(child_name) == config_->target_map().end()) {
      continue;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
      gpr_log(GPR_INFO,
              "[weighted_target_lb %p]   child=%s state=%s weight=%d picker=%p",
              this, child_name.c_str(),
              ConnectivityStateName(child->connectivity_state()),
              child->weight(), child->picker_wrapper().get());
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
  grpc_connectivity_state connectivity_state;
  if (!picker_list.empty()) {
    connectivity_state = GRPC_CHANNEL_READY;
  } else if (num_connecting > 0) {
    connectivity_state = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
    connectivity_state = GRPC_CHANNEL_IDLE;
  } else {
    connectivity_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] connectivity changed to %s",
            this, ConnectivityStateName(connectivity_state));
  }
  std::unique_ptr<SubchannelPicker> picker;
  absl::Status status;
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY:
      picker = absl::make_unique<WeightedPicker>(std::move(picker_list));
      break;
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
      picker =
          absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker"));
      break;
    default:
      status = absl::UnavailableError(
          "weighted_target: all children report state TRANSIENT_FAILURE");
      picker = absl::make_unique<TransientFailurePicker>(status);
  }
  channel_control_helper()->UpdateState(connectivity_state, status,
                                        std::move(picker));
}

//
// WeightedTargetLb::WeightedChild
//

WeightedTargetLb::WeightedChild::WeightedChild(
    RefCountedPtr<WeightedTargetLb> weighted_target_policy,
    const std::string& name)
    : weighted_target_policy_(std::move(weighted_target_policy)), name_(name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO, "[weighted_target_lb %p] created WeightedChild %p for %s",
            weighted_target_policy_.get(), this, name_.c_str());
  }
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
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
  grpc_pollset_set_del_pollset_set(
      child_policy_->interested_parties(),
      weighted_target_policy_->interested_parties());
  child_policy_.reset();
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_wrapper_.reset();
  if (delayed_removal_timer_callback_pending_) {
    delayed_removal_timer_callback_pending_ = false;
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  shutdown_ = true;
  Unref();
}

OrphanablePtr<LoadBalancingPolicy>
WeightedTargetLb::WeightedChild::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = weighted_target_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_lb_weighted_target_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: Created new child "
            "policy handler %p",
            weighted_target_policy_.get(), this, name_.c_str(),
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(
      lb_policy->interested_parties(),
      weighted_target_policy_->interested_parties());
  return lb_policy;
}

void WeightedTargetLb::WeightedChild::UpdateLocked(
    const WeightedTargetLbConfig::ChildConfig& config,
    absl::StatusOr<ServerAddressList> addresses,
    const grpc_channel_args* args) {
  if (weighted_target_policy_->shutting_down_) return;
  // Update child weight.
  weight_ = config.weight;
  // Reactivate if needed.
  if (delayed_removal_timer_callback_pending_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
      gpr_log(GPR_INFO,
              "[weighted_target_lb %p] WeightedChild %p %s: reactivating",
              weighted_target_policy_.get(), this, name_.c_str());
    }
    delayed_removal_timer_callback_pending_ = false;
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = config.config;
  update_args.addresses = std::move(addresses);
  update_args.args = grpc_channel_args_copy(args);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: Updating child "
            "policy handler %p",
            weighted_target_policy_.get(), this, name_.c_str(),
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

void WeightedTargetLb::WeightedChild::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
}

void WeightedTargetLb::WeightedChild::OnConnectivityStateUpdateLocked(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  // Cache the picker in the WeightedChild.
  picker_wrapper_ = MakeRefCounted<ChildPickerWrapper>(std::move(picker));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: connectivity "
            "state update: state=%s (%s) picker_wrapper=%p",
            weighted_target_policy_.get(), this, name_.c_str(),
            ConnectivityStateName(state), status.ToString().c_str(),
            picker_wrapper_.get());
  }
  // If the child reports IDLE, immediately tell it to exit idle.
  if (state == GRPC_CHANNEL_IDLE) child_policy_->ExitIdleLocked();
  // Decide what state to report for aggregation purposes.
  // If we haven't seen a failure since the last time we were in state
  // READY, then we report the state change as-is.  However, once we do see
  // a failure, we report TRANSIENT_FAILURE and ignore any subsequent state
  // changes until we go back into state READY.
  if (!seen_failure_since_ready_) {
    if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      seen_failure_since_ready_ = true;
    }
  } else {
    if (state != GRPC_CHANNEL_READY) return;
    seen_failure_since_ready_ = false;
  }
  connectivity_state_ = state;
  // Notify the LB policy.
  weighted_target_policy_->UpdateStateLocked();
}

void WeightedTargetLb::WeightedChild::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (weight_ == 0) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_weighted_target_trace)) {
    gpr_log(GPR_INFO,
            "[weighted_target_lb %p] WeightedChild %p %s: deactivating",
            weighted_target_policy_.get(), this, name_.c_str());
  }
  // Set the child weight to 0 so that future picker won't contain this child.
  weight_ = 0;
  // Start a timer to delete the child.
  Ref(DEBUG_LOCATION, "WeightedChild+timer").release();
  delayed_removal_timer_callback_pending_ = true;
  grpc_timer_init(&delayed_removal_timer_,
                  ExecCtx::Get()->Now() + kChildRetentionIntervalMs,
                  &on_delayed_removal_timer_);
}

void WeightedTargetLb::WeightedChild::OnDelayedRemovalTimer(
    void* arg, grpc_error_handle error) {
  WeightedChild* self = static_cast<WeightedChild*>(arg);
  (void)GRPC_ERROR_REF(error);  // ref owned by lambda
  self->weighted_target_policy_->work_serializer()->Run(
      [self, error]() { self->OnDelayedRemovalTimerLocked(error); },
      DEBUG_LOCATION);
}

void WeightedTargetLb::WeightedChild::OnDelayedRemovalTimerLocked(
    grpc_error_handle error) {
  if (error == GRPC_ERROR_NONE && delayed_removal_timer_callback_pending_ &&
      !shutdown_ && weight_ == 0) {
    delayed_removal_timer_callback_pending_ = false;
    weighted_target_policy_->targets_.erase(name_);
  }
  Unref(DEBUG_LOCATION, "WeightedChild+timer");
  GRPC_ERROR_UNREF(error);
}

//
// WeightedTargetLb::WeightedChild::Helper
//

RefCountedPtr<SubchannelInterface>
WeightedTargetLb::WeightedChild::Helper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (weighted_child_->weighted_target_policy_->shutting_down_) return nullptr;
  return weighted_child_->weighted_target_policy_->channel_control_helper()
      ->CreateSubchannel(std::move(address), args);
}

void WeightedTargetLb::WeightedChild::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (weighted_child_->weighted_target_policy_->shutting_down_) return;
  weighted_child_->OnConnectivityStateUpdateLocked(state, status,
                                                   std::move(picker));
}

void WeightedTargetLb::WeightedChild::Helper::RequestReresolution() {
  if (weighted_child_->weighted_target_policy_->shutting_down_) return;
  weighted_child_->weighted_target_policy_->channel_control_helper()
      ->RequestReresolution();
}

absl::string_view WeightedTargetLb::WeightedChild::Helper::GetAuthority() {
  return weighted_child_->weighted_target_policy_->channel_control_helper()
      ->GetAuthority();
}

void WeightedTargetLb::WeightedChild::Helper::AddTraceEvent(
    TraceSeverity severity, absl::string_view message) {
  if (weighted_child_->weighted_target_policy_->shutting_down_) return;
  weighted_child_->weighted_target_policy_->channel_control_helper()
      ->AddTraceEvent(severity, message);
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
      const Json& json, grpc_error_handle* error) const override {
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
    std::vector<grpc_error_handle> error_list;
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
        std::vector<grpc_error_handle> child_errors =
            ParseChildConfig(p.second, &child_config);
        if (!child_errors.empty()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
              absl::StrCat("field:targets key:", p.first), &child_errors));
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
  static std::vector<grpc_error_handle> ParseChildConfig(
      const Json& json, WeightedTargetLbConfig::ChildConfig* child_config) {
    std::vector<grpc_error_handle> error_list;
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
      int weight = gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (weight == -1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:weight error:unparseable value"));
      } else if (weight == 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:weight error:value must be greater than zero"));
      } else {
        child_config->weight = weight;
      }
    }
    // Child policy.
    it = json.object_value().find("childPolicy");
    if (it != json.object_value().end()) {
      grpc_error_handle parse_error = GRPC_ERROR_NONE;
      child_config->config =
          LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(it->second,
                                                                &parse_error);
      if (child_config->config == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        std::vector<grpc_error_handle> child_errors;
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
