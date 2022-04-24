//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.h"

#include <atomic>
#include <set>

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/json/json_util.h"

namespace grpc_core {

TraceFlag grpc_outlier_detection_lb_trace(false, "outlier_detection_lb");

namespace {

constexpr char kOutlierDetection[] = "outlier_detection_experimental";

// Config for xDS Cluster Impl LB policy.
class OutlierDetectionLbConfig : public LoadBalancingPolicy::Config {
 public:
  OutlierDetectionLbConfig(
      OutlierDetectionConfig outlier_detection_config,
      RefCountedPtr<LoadBalancingPolicy::Config> child_policy)
      : outlier_detection_config_(outlier_detection_config),
        child_policy_(std::move(child_policy)) {}

  const char* name() const override { return kOutlierDetection; }

  const OutlierDetectionConfig& outlier_detection_config() const {
    return outlier_detection_config_;
  }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

 private:
  OutlierDetectionConfig outlier_detection_config_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

// xDS Cluster Impl LB policy.
class OutlierDetectionLb : public LoadBalancingPolicy {
 public:
  explicit OutlierDetectionLb(Args args);

  const char* name() const override { return kOutlierDetection; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class SubchannelState;
  class SubchannelWrapper : public DelegatingSubchannel {
   public:
    SubchannelWrapper(RefCountedPtr<SubchannelState> subchannel_state,
                      RefCountedPtr<SubchannelInterface> subchannel)
        : DelegatingSubchannel(std::move(subchannel)),
          subchannel_state_(std::move(subchannel_state)),
          ejected_(false) {}

    void Eject();

    void Uneject();

    grpc_connectivity_state CheckConnectivityState() override;

    void WatchConnectivityState(
        grpc_connectivity_state initial_state,
        std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override;

    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface* watcher) override;

    RefCountedPtr<SubchannelState> subchannel_state() const {
      return subchannel_state_;
    }

   private:
    class WatcherWrapper
        : public SubchannelInterface::ConnectivityStateWatcherInterface {
     public:
      WatcherWrapper(std::unique_ptr<
                         SubchannelInterface::ConnectivityStateWatcherInterface>
                         watcher,
                     bool ejected)
          : watcher_(std::move(watcher)), ejected_(ejected) {}

      void Eject() {
        ejected_ = true;
        if (last_seen_state_ != GRPC_CHANNEL_TRANSIENT_FAILURE) {
          watcher_->OnConnectivityStateChange(GRPC_CHANNEL_TRANSIENT_FAILURE);
        }
      }

      void Uneject() {
        ejected_ = false;
        if (last_seen_state_ != GRPC_CHANNEL_TRANSIENT_FAILURE) {
          watcher_->OnConnectivityStateChange(last_seen_state_);
        }
      }

      void OnConnectivityStateChange(
          grpc_connectivity_state new_state) override {
        last_seen_state_ = new_state;
        if (!ejected_) {
          watcher_->OnConnectivityStateChange(new_state);
        }
      }

      grpc_pollset_set* interested_parties() override {
        return watcher_->interested_parties();
      }

     private:
      std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
          watcher_;
      grpc_connectivity_state last_seen_state_;
      bool ejected_;
    };

    RefCountedPtr<SubchannelState> subchannel_state_;
    bool ejected_;
    std::map<SubchannelInterface::ConnectivityStateWatcherInterface*,
             WatcherWrapper*>
        watchers_;
  };

  class SubchannelState : public RefCounted<SubchannelState> {
   public:
    struct Bucket {
      struct CallCounters {
        std::atomic<uint64_t> successes;
        std::atomic<uint64_t> failures;
      };
      Bucket() {
        current_bucket = absl::make_unique<CallCounters>();
        backup_bucket = absl::make_unique<CallCounters>();
      }
      std::unique_ptr<CallCounters> current_bucket;
      std::unique_ptr<CallCounters> backup_bucket;
      // The bucket used to update call counts.
      // Points to either current_bucket or active_bucket.
      std::atomic<Bucket*> active_bucket;
    };
    Bucket bucket;
    absl::optional<Timestamp> ejection_time;
    uint32_t mutiplier;
    absl::InlinedVector<RefCountedPtr<SubchannelWrapper>, 1> subchannels;
  };

  // A simple wrapper for ref-counting a picker from the child policy.
  class RefCountedPicker : public RefCounted<RefCountedPicker> {
   public:
    explicit RefCountedPicker(std::unique_ptr<SubchannelPicker> picker)
        : picker_(std::move(picker)) {}
    PickResult Pick(PickArgs args) { return picker_->Pick(args); }

   private:
    std::unique_ptr<SubchannelPicker> picker_;
  };

  // A picker that wraps the picker from the child to perform outlier detection.
  class Picker : public SubchannelPicker {
   public:
    Picker(OutlierDetectionLb* outlier_detection_lb,
           RefCountedPtr<RefCountedPicker> picker);

    PickResult Pick(PickArgs args) override;

   private:
    class SubchannelCallTracker;
    RefCountedPtr<RefCountedPicker> picker_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<OutlierDetectionLb> outlier_detection_policy)
        : outlier_detection_policy_(std::move(outlier_detection_policy)) {}

    ~Helper() override {
      outlier_detection_policy_.reset(DEBUG_LOCATION, "Helper");
    }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    absl::string_view GetAuthority() override;
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<OutlierDetectionLb> outlier_detection_policy_;
  };

  static std::string MakeKeyForAddress(const ServerAddress& address);

  ~OutlierDetectionLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);

  void MaybeUpdatePickerLocked();

  // Current config from the resolver.
  RefCountedPtr<OutlierDetectionLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  absl::Status status_;
  RefCountedPtr<RefCountedPicker> picker_;
  std::map<std::string, RefCountedPtr<SubchannelState>> subchannel_state_map_;
};

//
// OutlierDetectionLb::SubchannelWrapper
//

void OutlierDetectionLb::SubchannelWrapper::Eject() {
  ejected_ = true;
  for (auto& watcher : watchers_) {
    watcher.second->Eject();
  }
}

void OutlierDetectionLb::SubchannelWrapper::Uneject() {
  ejected_ = false;
  for (auto& watcher : watchers_) {
    watcher.second->Uneject();
  }
}

grpc_connectivity_state
OutlierDetectionLb::SubchannelWrapper::CheckConnectivityState() {
  if (ejected_) return GRPC_CHANNEL_TRANSIENT_FAILURE;
  return wrapped_subchannel()->CheckConnectivityState();
}

void OutlierDetectionLb::SubchannelWrapper::WatchConnectivityState(
    grpc_connectivity_state initial_state,
    std::unique_ptr<ConnectivityStateWatcherInterface> watcher) {
  ConnectivityStateWatcherInterface* watcher_ptr = watcher.get();
  auto watcher_wrapper =
      absl::make_unique<WatcherWrapper>(std::move(watcher), ejected_);
  watchers_.emplace(watcher_ptr, watcher_wrapper.get());
  wrapped_subchannel()->WatchConnectivityState(initial_state,
                                               std::move(watcher_wrapper));
}

void OutlierDetectionLb::SubchannelWrapper::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  auto it = watchers_.find(watcher);
  if (it == watchers_.end()) return;
  wrapped_subchannel()->CancelConnectivityStateWatch(it->second);
  watchers_.erase(it);
}

//
// OutlierDetectionLb::Picker::SubchannelCallTracker
//

class OutlierDetectionLb::Picker::SubchannelCallTracker
    : public LoadBalancingPolicy::SubchannelCallTrackerInterface {
 public:
  SubchannelCallTracker(
      std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
          original_subchannel_call_tracker,
      RefCountedPtr<SubchannelState> subchannel_state)
      : original_subchannel_call_tracker_(
            std::move(original_subchannel_call_tracker)),
        subchannel_state_(std::move(subchannel_state)) {}

  ~SubchannelCallTracker() override {
    subchannel_state_.reset(DEBUG_LOCATION, "SubchannelCallTracker");
    GPR_DEBUG_ASSERT(!started_);
  }

  void Start() override {
    // This tracker does not care about started calls only finished calls.
    // Delegate if needed.
    if (original_subchannel_call_tracker_ != nullptr) {
      original_subchannel_call_tracker_->Start();
    }
#ifndef NDEBUG
    started_ = true;
#endif
  }

  void Finish(FinishArgs args) override {
    // Delegate if needed.
    if (original_subchannel_call_tracker_ != nullptr) {
      original_subchannel_call_tracker_->Finish(args);
    }
    // Record call completion based on status for outlier detection
    // calculations.
    if (subchannel_state_ != nullptr) {
      if (args.status.ok()) {
        gpr_log(GPR_INFO, "donna report success");
        subchannel_state_->bucket.current_bucket->successes.fetch_add(1);
      } else {
        gpr_log(GPR_INFO, "donna report failure");
        subchannel_state_->bucket.current_bucket->failures.fetch_add(1);
      }
    }
#ifndef NDEBUG
    started_ = false;
#endif
  }

 private:
  std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
      original_subchannel_call_tracker_;
  // TODO@donnadionne: RefCountedPtr
  RefCountedPtr<SubchannelState> subchannel_state_;
#ifndef NDEBUG
  bool started_ = false;
#endif
};

//
// OutlierDetectionLb::Picker
//

OutlierDetectionLb::Picker::Picker(OutlierDetectionLb* outlier_detection_lb,
                                   RefCountedPtr<RefCountedPicker> picker)
    : picker_(std::move(picker)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] constructed new picker %p",
            outlier_detection_lb, this);
  }
}

LoadBalancingPolicy::PickResult OutlierDetectionLb::Picker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  if (picker_ == nullptr) {  // Should never happen.
    return PickResult::Fail(absl::InternalError(
        "outlier_detection picker not given any child picker"));
  }
  // Delegate to child picker
  PickResult result = picker_->Pick(args);
  auto* complete_pick = absl::get_if<PickResult::Complete>(&result.result);
  // TODO@donnadionne: I think this is where we increment the call result
  // counter: complete_pick = success; otherwise = failure
  // But how do I index into my subchannel_state_map_ which is keyed by
  // subchannel address and each entry can have a list of subchannel wrappers
  // like the one found here.
  if (complete_pick != nullptr) {
    // Unwrap subchannel to pass back up the stack.
    auto* subchannel_wrapper =
        static_cast<SubchannelWrapper*>(complete_pick->subchannel.get());
    complete_pick->subchannel = subchannel_wrapper->wrapped_subchannel();
    // Inject subchannel call tracker to record call completion.
    complete_pick->subchannel_call_tracker =
        absl::make_unique<SubchannelCallTracker>(
            std::move(complete_pick->subchannel_call_tracker),
            subchannel_wrapper->subchannel_state());
  }
  return result;
}

//
// OutlierDetectionLb
//

std::string OutlierDetectionLb::MakeKeyForAddress(
    const ServerAddress& address) {
  // Strip off attributes to construct the key.
  return ServerAddress(address.address(), nullptr).ToString();
}

OutlierDetectionLb::OutlierDetectionLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] created", this);
  }
}

OutlierDetectionLb::~OutlierDetectionLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] destroying outlier_detection LB policy",
            this);
  }
}

void OutlierDetectionLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_.reset();
}

void OutlierDetectionLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void OutlierDetectionLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void OutlierDetectionLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] Received update", this);
  }
  // Update config.
  config_ = std::move(args.config);
  // Create policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
  }
  std::set<std::string> current_addresses;
  if (args.addresses.ok()) {
    for (ServerAddress address : *args.addresses) {
      std::string address_key = MakeKeyForAddress(address);
      subchannel_state_map_[address_key] = MakeRefCounted<SubchannelState>();
      gpr_log(GPR_INFO, "donna print address %s", address_key.c_str());
      current_addresses.emplace(address_key);
    }
    for (auto it = subchannel_state_map_.begin();
         it != subchannel_state_map_.end();) {
      if (current_addresses.find(it->first) == current_addresses.end()) {
        // remove each map entry for a subchannel address not in the updated
        // address list.
        gpr_log(GPR_INFO, "donna erased %s", it->first.c_str());
        it = subchannel_state_map_.erase(it);
      }
      ++it;
    }
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(args.addresses);
  update_args.config = config_->child_policy();
  update_args.args = grpc_channel_args_copy(args.args);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] Updating child policy handler %p", this,
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

void OutlierDetectionLb::MaybeUpdatePickerLocked() {
  if (picker_ != nullptr) {
    auto outlier_detection_picker = absl::make_unique<Picker>(this, picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO,
              "[outlier_detection_lb %p] updating connectivity: state=%s "
              "status=(%s) picker=%p",
              this, ConnectivityStateName(state_), status_.ToString().c_str(),
              outlier_detection_picker.get());
    }
    channel_control_helper()->UpdateState(state_, status_,
                                          std::move(outlier_detection_picker));
  }
}

OrphanablePtr<LoadBalancingPolicy> OutlierDetectionLb::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_outlier_detection_lb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] Created new child policy handler %p",
            this, lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

//
// OutlierDetectionLb::Helper
//

RefCountedPtr<SubchannelInterface> OutlierDetectionLb::Helper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (outlier_detection_policy_->shutting_down_) return nullptr;
  gpr_log(GPR_INFO, "donna address string to search %s",
          address.ToString().c_str());
  auto& ejection_map = outlier_detection_policy_->subchannel_state_map_;
  auto ejection_entry = ejection_map.find(MakeKeyForAddress(address));
  if (ejection_entry == ejection_map.end()) {
    gpr_log(GPR_INFO, "donna did not insert an entry");
  } else {
    gpr_log(GPR_INFO, "donna did insert an entry");
  }
  // TODO@donnadionne: subchannel created with multiple addresses case?
  RefCountedPtr<SubchannelWrapper> subchannel =
      MakeRefCounted<SubchannelWrapper>(
          (ejection_entry != ejection_map.end()) ? ejection_entry->second
                                                 : nullptr,
          outlier_detection_policy_->channel_control_helper()->CreateSubchannel(
              std::move(address), args));
  if (ejection_entry != ejection_map.end()) {
    ejection_entry->second->subchannels.emplace_back(subchannel);
    if (ejection_entry->second->ejection_time.has_value()) {
      // currently ejected
      subchannel->Eject();
    }
  }
  return std::move(subchannel);
}

void OutlierDetectionLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (outlier_detection_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] child connectivity state update: "
            "state=%s (%s) "
            "picker=%p",
            outlier_detection_policy_.get(), ConnectivityStateName(state),
            status.ToString().c_str(), picker.get());
  }
  // Save the state and picker.
  outlier_detection_policy_->state_ = state;
  outlier_detection_policy_->status_ = status;
  outlier_detection_policy_->picker_ =
      MakeRefCounted<RefCountedPicker>(std::move(picker));
  // Wrap the picker and return it to the channel.
  outlier_detection_policy_->MaybeUpdatePickerLocked();
}

void OutlierDetectionLb::Helper::RequestReresolution() {
  if (outlier_detection_policy_->shutting_down_) return;
  outlier_detection_policy_->channel_control_helper()->RequestReresolution();
}

absl::string_view OutlierDetectionLb::Helper::GetAuthority() {
  return outlier_detection_policy_->channel_control_helper()->GetAuthority();
}

void OutlierDetectionLb::Helper::AddTraceEvent(TraceSeverity severity,
                                               absl::string_view message) {
  if (outlier_detection_policy_->shutting_down_) return;
  outlier_detection_policy_->channel_control_helper()->AddTraceEvent(severity,
                                                                     message);
}

//
// factory
//

class OutlierDetectionLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<OutlierDetectionLb>(std::move(args));
  }

  const char* name() const override { return kOutlierDetection; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error_handle* error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // This policy was configured in the deprecated loadBalancingPolicy
      // field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:outlier_detection policy requires "
          "configuration. Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error_handle> error_list;
    // Outlier detection config
    OutlierDetectionConfig outlier_detection_config;
    ParseJsonObjectFieldAsDuration(json.object_value(), "interval",
                                   &outlier_detection_config.interval,
                                   &error_list);
    ParseJsonObjectFieldAsDuration(json.object_value(), "baseEjectionTime",
                                   &outlier_detection_config.base_ejection_time,
                                   &error_list);
    ParseJsonObjectFieldAsDuration(json.object_value(), "maxEjectionTime",
                                   &outlier_detection_config.max_ejection_time,
                                   &error_list);
    ParseJsonObjectField(json.object_value(), "maxEjectionPercent",
                         &outlier_detection_config.max_ejection_percent,
                         &error_list);
    auto it = json.object_value().find("successRateEjection");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:successRateEjection error:type must be object"));
      } else {
        const Json::Object& object = it->second.object_value();
        ParseJsonObjectField(
            object, "stdevFactor",
            &outlier_detection_config.success_rate_ejection->stdev_factor,
            &error_list);
        ParseJsonObjectField(object, "enforcementPercentage",
                             &outlier_detection_config.success_rate_ejection
                                  ->enforcement_percentage,
                             &error_list);
        ParseJsonObjectField(
            object, "minimumHosts",
            &outlier_detection_config.success_rate_ejection->minimum_hosts,
            &error_list);
        ParseJsonObjectField(
            object, "requestVolume",
            &outlier_detection_config.success_rate_ejection->request_volume,
            &error_list);
      }
    }
    it = json.object_value().find("failurePercentageEjection");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:successRateEjection error:type must be object"));
      } else {
        const Json::Object& object = it->second.object_value();
        ParseJsonObjectField(
            object, "threshold",
            &outlier_detection_config.failure_percentage_ejection->threshold,
            &error_list);
        ParseJsonObjectField(
            object, "enforcementPercentage",
            &outlier_detection_config.failure_percentage_ejection
                 ->enforcement_percentage,
            &error_list);
        ParseJsonObjectField(object, "minimumHosts",
                             &outlier_detection_config
                                  .failure_percentage_ejection->minimum_hosts,
                             &error_list);
        ParseJsonObjectField(object, "requestVolume",
                             &outlier_detection_config
                                  .failure_percentage_ejection->request_volume,
                             &error_list);
      }
    }
    // Child policy.
    RefCountedPtr<LoadBalancingPolicy::Config> child_policy;
    it = json.object_value().find("childPolicy");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:childPolicy error:required field missing"));
    } else {
      grpc_error_handle parse_error = GRPC_ERROR_NONE;
      child_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
          it->second, &parse_error);
      if (child_policy == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        std::vector<grpc_error_handle> child_errors;
        child_errors.push_back(parse_error);
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_VECTOR("field:childPolicy", &child_errors));
      }
    }
    return MakeRefCounted<OutlierDetectionLbConfig>(
        std::move(outlier_detection_config), std::move(child_policy));
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_lb_policy_outlier_detection_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::OutlierDetectionLbFactory>());
}

void grpc_lb_policy_outlier_detection_shutdown() {}
