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

#include "src/core/load_balancing/weighted_target/weighted_target.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
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
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"

// IWYU pragma: no_include <type_traits>

namespace grpc_core {

namespace {

using ::grpc_event_engine::experimental::EventEngine;

constexpr absl::string_view kWeightedTarget = "weighted_target_experimental";

// How long we keep a child around for after it has been removed from
// the config.
constexpr Duration kChildRetentionInterval = Duration::Minutes(15);

// Config for weighted_target LB policy.
class WeightedTargetLbConfig final : public LoadBalancingPolicy::Config {
 public:
  struct ChildConfig {
    uint32_t weight;
    RefCountedPtr<LoadBalancingPolicy::Config> config;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
    void JsonPostLoad(const Json& json, const JsonArgs&,
                      ValidationErrors* errors);
  };

  using TargetMap = std::map<std::string, ChildConfig>;

  WeightedTargetLbConfig() = default;

  WeightedTargetLbConfig(const WeightedTargetLbConfig&) = delete;
  WeightedTargetLbConfig& operator=(const WeightedTargetLbConfig&) = delete;

  WeightedTargetLbConfig(WeightedTargetLbConfig&& other) = delete;
  WeightedTargetLbConfig& operator=(WeightedTargetLbConfig&& other) = delete;

  absl::string_view name() const override { return kWeightedTarget; }

  const TargetMap& target_map() const { return target_map_; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);

 private:
  TargetMap target_map_;
};

// weighted_target LB policy.
class WeightedTargetLb final : public LoadBalancingPolicy {
 public:
  explicit WeightedTargetLb(Args args);

  absl::string_view name() const override { return kWeightedTarget; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // Picks a child using stateless WRR and then delegates to that
  // child's picker.
  class WeightedPicker final : public SubchannelPicker {
   public:
    // Maintains a weighted list of pickers from each child that is in
    // ready state. The first element in the pair represents the end of a
    // range proportional to the child's weight. The start of the range
    // is the previous value in the vector and is 0 for the first element.
    using PickerList =
        std::vector<std::pair<uint64_t, RefCountedPtr<SubchannelPicker>>>;

    explicit WeightedPicker(PickerList pickers)
        : pickers_(std::move(pickers)) {}

    PickResult Pick(PickArgs args) override;

   private:
    PickerList pickers_;
  };

  // Each WeightedChild holds a ref to its parent WeightedTargetLb.
  class WeightedChild final : public InternallyRefCounted<WeightedChild> {
   public:
    WeightedChild(RefCountedPtr<WeightedTargetLb> weighted_target_policy,
                  const std::string& name);
    ~WeightedChild() override;

    void Orphan() override;

    absl::Status UpdateLocked(
        const WeightedTargetLbConfig::ChildConfig& config,
        absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>> addresses,
        const std::string& resolution_note, ChannelArgs args);
    void ResetBackoffLocked();
    void DeactivateLocked();

    uint32_t weight() const { return weight_; }
    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }
    RefCountedPtr<SubchannelPicker> picker() const { return picker_; }

   private:
    class Helper final : public DelegatingChannelControlHelper {
     public:
      explicit Helper(RefCountedPtr<WeightedChild> weighted_child)
          : weighted_child_(std::move(weighted_child)) {}

      ~Helper() override { weighted_child_.reset(DEBUG_LOCATION, "Helper"); }

      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       RefCountedPtr<SubchannelPicker> picker) override;

     private:
      ChannelControlHelper* parent_helper() const override {
        return weighted_child_->weighted_target_policy_
            ->channel_control_helper();
      }

      RefCountedPtr<WeightedChild> weighted_child_;
    };

    class DelayedRemovalTimer final
        : public InternallyRefCounted<DelayedRemovalTimer> {
     public:
      explicit DelayedRemovalTimer(RefCountedPtr<WeightedChild> weighted_child);

      void Orphan() override;

     private:
      void OnTimerLocked();

      RefCountedPtr<WeightedChild> weighted_child_;
      std::optional<EventEngine::TaskHandle> timer_handle_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const ChannelArgs& args);

    void OnConnectivityStateUpdateLocked(
        grpc_connectivity_state state, const absl::Status& status,
        RefCountedPtr<SubchannelPicker> picker);

    // The owning LB policy.
    RefCountedPtr<WeightedTargetLb> weighted_target_policy_;

    const std::string name_;

    uint32_t weight_ = 0;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    RefCountedPtr<SubchannelPicker> picker_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_CONNECTING;

    OrphanablePtr<DelayedRemovalTimer> delayed_removal_timer_;
  };

  ~WeightedTargetLb() override;

  void ShutdownLocked() override;

  void UpdateStateLocked();

  // Current config from the resolver.
  RefCountedPtr<WeightedTargetLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;
  bool update_in_progress_ = false;

  // Children.
  std::map<std::string, OrphanablePtr<WeightedChild>> targets_;
};

//
// WeightedTargetLb::WeightedPicker
//

WeightedTargetLb::PickResult WeightedTargetLb::WeightedPicker::Pick(
    PickArgs args) {
  // Generate a random number in [0, total weight).
  const uint64_t key =
      absl::Uniform<uint64_t>(SharedBitGen(), 0, pickers_.back().first);
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
  CHECK(pickers_[index].first > key);
  // Delegate to the child picker.
  return pickers_[index].second->Pick(args);
}

//
// WeightedTargetLb
//

WeightedTargetLb::WeightedTargetLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << this << "] created";
}

WeightedTargetLb::~WeightedTargetLb() {
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << this
      << "] destroying weighted_target LB policy";
}

void WeightedTargetLb::ShutdownLocked() {
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << this << "] shutting down";
  shutting_down_ = true;
  targets_.clear();
}

void WeightedTargetLb::ResetBackoffLocked() {
  for (auto& [_, child] : targets_) child->ResetBackoffLocked();
}

absl::Status WeightedTargetLb::UpdateLocked(UpdateArgs args) {
  if (shutting_down_) return absl::OkStatus();
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << this << "] received update";
  update_in_progress_ = true;
  // Update config.
  config_ = args.config.TakeAsSubclass<WeightedTargetLbConfig>();
  // Deactivate the targets not in the new config.
  for (const auto& [name, child] : targets_) {
    if (config_->target_map().find(name) == config_->target_map().end()) {
      child->DeactivateLocked();
    }
  }
  // Update all children.
  absl::StatusOr<HierarchicalAddressMap> address_map =
      MakeHierarchicalAddressMap(args.addresses);
  std::vector<std::string> errors;
  for (const auto& [name, config] : config_->target_map()) {
    auto& target = targets_[name];
    // Create child if it does not already exist.
    if (target == nullptr) {
      target = MakeOrphanable<WeightedChild>(
          RefAsSubclass<WeightedTargetLb>(DEBUG_LOCATION, "WeightedChild"),
          name);
    }
    absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>> addresses;
    if (address_map.ok()) {
      auto it = address_map->find(name);
      if (it == address_map->end()) {
        addresses = std::make_shared<EndpointAddressesListIterator>(
            EndpointAddressesList());
      } else {
        addresses = std::move(it->second);
      }
    } else {
      addresses = address_map.status();
    }
    absl::Status status = target->UpdateLocked(config, std::move(addresses),
                                               args.resolution_note, args.args);
    if (!status.ok()) {
      errors.emplace_back(
          absl::StrCat("child ", name, ": ", status.ToString()));
    }
  }
  update_in_progress_ = false;
  if (config_->target_map().empty()) {
    absl::Status status = absl::UnavailableError(absl::StrCat(
        "no children in weighted_target policy (", args.resolution_note, ")"));
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return absl::OkStatus();
  }
  UpdateStateLocked();
  // Return status.
  if (!errors.empty()) {
    return absl::UnavailableError(absl::StrCat(
        "errors from children: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

void WeightedTargetLb::UpdateStateLocked() {
  // If we're in the process of propagating an update from our parent to
  // our children, ignore any updates that come from the children.  We
  // will instead return a new picker once the update has been seen by
  // all children.  This avoids unnecessary picker churn while an update
  // is being propagated to our children.
  if (update_in_progress_) return;
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << this
      << "] scanning children to determine connectivity state";
  // Construct lists of child pickers with associated weights, one for
  // children that are in state READY and another for children that are
  // in state TRANSIENT_FAILURE.  Each child is represented by a portion of
  // the range proportional to its weight, such that the total range is the
  // sum of the weights of all children.
  WeightedPicker::PickerList ready_picker_list;
  uint64_t ready_end = 0;
  WeightedPicker::PickerList tf_picker_list;
  uint64_t tf_end = 0;
  // Also count the number of children in CONNECTING and IDLE, to determine
  // the aggregated state.
  size_t num_connecting = 0;
  size_t num_idle = 0;
  for (const auto& [child_name, child] : targets_) {
    // Skip the targets that are not in the latest update.
    if (config_->target_map().find(child_name) == config_->target_map().end()) {
      continue;
    }
    auto child_picker = child->picker();
    GRPC_TRACE_LOG(weighted_target_lb, INFO)
        << "[weighted_target_lb " << this << "]   child=" << child_name
        << " state=" << ConnectivityStateName(child->connectivity_state())
        << " weight=" << child->weight() << " picker=" << child_picker.get();
    switch (child->connectivity_state()) {
      case GRPC_CHANNEL_READY: {
        CHECK_GT(child->weight(), 0u);
        ready_end += child->weight();
        ready_picker_list.emplace_back(ready_end, std::move(child_picker));
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
        CHECK_GT(child->weight(), 0u);
        tf_end += child->weight();
        tf_picker_list.emplace_back(tf_end, std::move(child_picker));
        break;
      }
      default:
        GPR_UNREACHABLE_CODE(return);
    }
  }
  // Determine aggregated connectivity state.
  grpc_connectivity_state connectivity_state;
  if (!ready_picker_list.empty()) {
    connectivity_state = GRPC_CHANNEL_READY;
  } else if (num_connecting > 0) {
    connectivity_state = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
    connectivity_state = GRPC_CHANNEL_IDLE;
  } else {
    connectivity_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << this << "] connectivity changed to "
      << ConnectivityStateName(connectivity_state);
  RefCountedPtr<SubchannelPicker> picker;
  absl::Status status;
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY:
      picker = MakeRefCounted<WeightedPicker>(std::move(ready_picker_list));
      break;
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
      picker = MakeRefCounted<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker"));
      break;
    default:
      picker = MakeRefCounted<WeightedPicker>(std::move(tf_picker_list));
  }
  channel_control_helper()->UpdateState(connectivity_state, status,
                                        std::move(picker));
}

//
// WeightedTargetLb::WeightedChild::DelayedRemovalTimer
//

WeightedTargetLb::WeightedChild::DelayedRemovalTimer::DelayedRemovalTimer(
    RefCountedPtr<WeightedTargetLb::WeightedChild> weighted_child)
    : weighted_child_(std::move(weighted_child)) {
  timer_handle_ =
      weighted_child_->weighted_target_policy_->channel_control_helper()
          ->GetEventEngine()
          ->RunAfter(kChildRetentionInterval, [self = Ref()]() mutable {
            ExecCtx exec_ctx;
            auto* self_ptr = self.get();  // Avoid use-after-move problem.
            self_ptr->weighted_child_->weighted_target_policy_
                ->work_serializer()
                ->Run([self = std::move(self)] { self->OnTimerLocked(); });
          });
}

void WeightedTargetLb::WeightedChild::DelayedRemovalTimer::Orphan() {
  if (timer_handle_.has_value()) {
    GRPC_TRACE_LOG(weighted_target_lb, INFO)
        << "[weighted_target_lb "
        << weighted_child_->weighted_target_policy_.get() << "] WeightedChild "
        << weighted_child_.get() << " " << weighted_child_->name_
        << ": cancelling delayed removal timer";
    weighted_child_->weighted_target_policy_->channel_control_helper()
        ->GetEventEngine()
        ->Cancel(*timer_handle_);
  }
  Unref();
}

void WeightedTargetLb::WeightedChild::DelayedRemovalTimer::OnTimerLocked() {
  CHECK(timer_handle_.has_value());
  timer_handle_.reset();
  weighted_child_->weighted_target_policy_->targets_.erase(
      weighted_child_->name_);
}

//
// WeightedTargetLb::WeightedChild
//

WeightedTargetLb::WeightedChild::WeightedChild(
    RefCountedPtr<WeightedTargetLb> weighted_target_policy,
    const std::string& name)
    : weighted_target_policy_(std::move(weighted_target_policy)),
      name_(name),
      picker_(MakeRefCounted<QueuePicker>(nullptr)) {
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << weighted_target_policy_.get()
      << "] created WeightedChild " << this << " for " << name_;
}

WeightedTargetLb::WeightedChild::~WeightedChild() {
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << weighted_target_policy_.get()
      << "] WeightedChild " << this << " " << name_ << ": destroying child";
  weighted_target_policy_.reset(DEBUG_LOCATION, "WeightedChild");
}

void WeightedTargetLb::WeightedChild::Orphan() {
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << weighted_target_policy_.get()
      << "] WeightedChild " << this << " " << name_ << ": shutting down child";
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(
      child_policy_->interested_parties(),
      weighted_target_policy_->interested_parties());
  child_policy_.reset();
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_.reset();
  delayed_removal_timer_.reset();
  Unref();
}

OrphanablePtr<LoadBalancingPolicy>
WeightedTargetLb::WeightedChild::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = weighted_target_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &weighted_target_lb_trace);
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << weighted_target_policy_.get()
      << "] WeightedChild " << this << " " << name_
      << ": created new child policy handler " << lb_policy.get();
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(
      lb_policy->interested_parties(),
      weighted_target_policy_->interested_parties());
  return lb_policy;
}

absl::Status WeightedTargetLb::WeightedChild::UpdateLocked(
    const WeightedTargetLbConfig::ChildConfig& config,
    absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>> addresses,
    const std::string& resolution_note, ChannelArgs args) {
  if (weighted_target_policy_->shutting_down_) return absl::OkStatus();
  // Update child weight.
  if (weight_ != config.weight && GRPC_TRACE_FLAG_ENABLED(weighted_target_lb)) {
    LOG(INFO) << "[weighted_target_lb " << weighted_target_policy_.get()
              << "] WeightedChild " << this << " " << name_
              << ": weight=" << config.weight;
  }
  weight_ = config.weight;
  // Reactivate if needed.
  if (delayed_removal_timer_ != nullptr) {
    GRPC_TRACE_LOG(weighted_target_lb, INFO)
        << "[weighted_target_lb " << weighted_target_policy_.get()
        << "] WeightedChild " << this << " " << name_ << ": reactivating";
    delayed_removal_timer_.reset();
  }
  // Create child policy if needed.
  args = args.Set(GRPC_ARG_LB_WEIGHTED_TARGET_CHILD, name_);
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = config.config;
  update_args.addresses = std::move(addresses);
  update_args.resolution_note = resolution_note;
  update_args.args = std::move(args);
  // Update the policy.
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << weighted_target_policy_.get()
      << "] WeightedChild " << this << " " << name_
      << ": updating child policy handler " << child_policy_.get();
  return child_policy_->UpdateLocked(std::move(update_args));
}

void WeightedTargetLb::WeightedChild::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
}

void WeightedTargetLb::WeightedChild::OnConnectivityStateUpdateLocked(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  // Cache the picker in the WeightedChild.
  picker_ = std::move(picker);
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << weighted_target_policy_.get()
      << "] WeightedChild " << this << " " << name_
      << ": connectivity state update: state=" << ConnectivityStateName(state)
      << " (" << status << ") picker=" << picker_.get();
  // If the child reports IDLE, immediately tell it to exit idle.
  if (state == GRPC_CHANNEL_IDLE) child_policy_->ExitIdleLocked();
  // Decide what state to report for aggregation purposes.
  // If the last recorded state was TRANSIENT_FAILURE and the new state
  // is something other than READY, don't change the state.
  if (connectivity_state_ != GRPC_CHANNEL_TRANSIENT_FAILURE ||
      state == GRPC_CHANNEL_READY) {
    connectivity_state_ = state;
  }
  // Update the LB policy's state if this child is not deactivated.
  if (weight_ != 0) weighted_target_policy_->UpdateStateLocked();
}

void WeightedTargetLb::WeightedChild::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (weight_ == 0) return;
  GRPC_TRACE_LOG(weighted_target_lb, INFO)
      << "[weighted_target_lb " << weighted_target_policy_.get()
      << "] WeightedChild " << this << " " << name_ << ": deactivating";
  // Set the child weight to 0 so that future picker won't contain this child.
  weight_ = 0;
  // Start a timer to delete the child.
  delayed_removal_timer_ = MakeOrphanable<DelayedRemovalTimer>(
      Ref(DEBUG_LOCATION, "DelayedRemovalTimer"));
}

//
// WeightedTargetLb::WeightedChild::Helper
//

void WeightedTargetLb::WeightedChild::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  if (weighted_child_->weighted_target_policy_->shutting_down_) return;
  weighted_child_->OnConnectivityStateUpdateLocked(state, status,
                                                   std::move(picker));
}

//
// factory
//

const JsonLoaderInterface* WeightedTargetLbConfig::ChildConfig::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<ChildConfig>()
          // Note: The config field requires custom parsing, so it's
          // handled in JsonPostLoad() instead.
          .Field("weight", &ChildConfig::weight)
          .Finish();
  return loader;
}

void WeightedTargetLbConfig::ChildConfig::JsonPostLoad(
    const Json& json, const JsonArgs&, ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".childPolicy");
  auto it = json.object().find("childPolicy");
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

const JsonLoaderInterface* WeightedTargetLbConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<WeightedTargetLbConfig>()
          .Field("targets", &WeightedTargetLbConfig::target_map_)
          .Finish();
  return loader;
}

class WeightedTargetLbFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<WeightedTargetLb>(std::move(args));
  }

  absl::string_view name() const override { return kWeightedTarget; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<WeightedTargetLbConfig>>(
        json, JsonArgs(), "errors validating weighted_target LB policy config");
  }
};

}  // namespace

void RegisterWeightedTargetLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<WeightedTargetLbFactory>());
}

}  // namespace grpc_core
