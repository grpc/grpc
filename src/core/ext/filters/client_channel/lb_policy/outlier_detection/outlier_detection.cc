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

#include <atomic>

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_endpoint.h"
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

//
// LB policy
//

constexpr char kOutlierDetection[] = "outlier_detection_experimental";

// Config for xDS Cluster Impl LB policy.
class OutlierDetectionLbConfig : public LoadBalancingPolicy::Config {
 public:
  OutlierDetectionLbConfig(
      XdsClusterResource::OutlierDetection outlier_detection_info,
      RefCountedPtr<LoadBalancingPolicy::Config> child_policy)
      : outlier_detection_info_(outlier_detection_info),
        child_policy_(std::move(child_policy)) {}

  const char* name() const override { return kOutlierDetection; }

  const XdsClusterResource::OutlierDetection& outlier_detection_info() const {
    return outlier_detection_info_;
  }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

 private:
  XdsClusterResource::OutlierDetection outlier_detection_info_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

// xDS Cluster Impl LB policy.
class OutlierDetectionLb : public LoadBalancingPolicy {
 public:
  OutlierDetectionLb(RefCountedPtr<XdsClient> xds_client, Args args);

  const char* name() const override { return kOutlierDetection; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class SubchannelWrapper : public SubchannelInterface {
   public:
    SubchannelWrapper(RefCountedPtr<SubchannelInterface> subchannel)
        : subchannel_(std::move(subchannel)), eject_(false) {}

    RefCountedPtr<SubchannelInterface> subchannel() const {
      return subchannel_;
    }

    void eject();

    void uneject();

    grpc_connectivity_state CheckConnectivityState() override;

    void WatchConnectivityState(
        grpc_connectivity_state initial_state,
        std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override;

    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface* watcher) override;

    void AttemptToConnect() override { subchannel_->AttemptToConnect(); }
    void ResetBackoff() override { subchannel_->ResetBackoff(); }
    const grpc_channel_args* channel_args() override {
      return subchannel_->channel_args();
    }

    class WatcherWrapper
        : public SubchannelInterface::ConnectivityStateWatcherInterface {
     public:
      WatcherWrapper(std::unique_ptr<
                     SubchannelInterface::ConnectivityStateWatcherInterface>
                         watcher)
          : watcher_(std::move(watcher)) {}

      ~WatcherWrapper() override {}

      void eject() {
        eject_ = true;
        watcher_->OnConnectivityStateChange(GRPC_CHANNEL_TRANSIENT_FAILURE);
      }

      void uneject() {
        eject_ = false;
        watcher_->OnConnectivityStateChange(last_seen_state_);
      }

      void OnConnectivityStateChange(
          grpc_connectivity_state new_state) override {
        void* trace[256];
        int n = absl::GetStackTrace(trace, 256, 0);
        for (int i = 0; i <= n; ++i) {
          char tmp[1024];
          const char* symbol = "(unknown)";
          if (absl::Symbolize(trace[i], tmp, sizeof(tmp))) {
            symbol = tmp;
          }
          gpr_log(GPR_ERROR,
                  "Watcher OnConnectivityStateChange stack state %d %p %s",
                  new_state, trace[i], symbol);
        }
        last_seen_state_ = new_state;
        if (eject_) {
          watcher_->OnConnectivityStateChange(GRPC_CHANNEL_TRANSIENT_FAILURE);
        } else {
          watcher_->OnConnectivityStateChange(new_state);
        }
      }

      grpc_pollset_set* interested_parties() override {
        SubchannelInterface::ConnectivityStateWatcherInterface* watcher =
            watcher_.get();
        return watcher->interested_parties();
      }
      grpc_connectivity_state last_seen_state() const {
        return last_seen_state_;
      }

     private:
      std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
          watcher_;
      grpc_connectivity_state last_seen_state_;
      bool eject_ = false;
    };

   private:
    RefCountedPtr<SubchannelInterface> subchannel_;
    bool eject_;
    // SubchannelInterface::ConnectivityStateWatcherInterface* watcher_;
    WatcherWrapper* watcher_;
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

  // A picker that wraps the picker from the child to perform drops.
  class Picker : public SubchannelPicker {
   public:
    Picker(OutlierDetectionLb* outlier_detection_lb,
           RefCountedPtr<RefCountedPicker> picker);

    PickResult Pick(PickArgs args) override;

   private:
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

  ~OutlierDetectionLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const grpc_channel_args* args);

  void MaybeUpdatePickerLocked();

  // Current config from the resolver.
  RefCountedPtr<OutlierDetectionLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // The xds client.
  RefCountedPtr<XdsClient> xds_client_;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  absl::Status status_;
  RefCountedPtr<RefCountedPicker> picker_;
};

///
/// OutlierDetectionLb::SubchannelWrapper
///

void OutlierDetectionLb::SubchannelWrapper::eject() {
  eject_ = true;
  if (watcher_ != nullptr) {
    watcher_->eject();
  }
}

void OutlierDetectionLb::SubchannelWrapper::uneject() {
  eject_ = false;
  if (watcher_ != nullptr) {
    watcher_->uneject();
  }
}

grpc_connectivity_state
OutlierDetectionLb::SubchannelWrapper::CheckConnectivityState() {
  void* trace[256];
  int n = absl::GetStackTrace(trace, 256, 0);
  for (int i = 0; i <= n; ++i) {
    char tmp[1024];
    const char* symbol = "(unknown)";
    if (absl::Symbolize(trace[i], tmp, sizeof(tmp))) {
      symbol = tmp;
    }
    gpr_log(GPR_ERROR, "CheckConnectivityState stack %p %s", trace[i], symbol);
  }
  if (eject_) return GRPC_CHANNEL_TRANSIENT_FAILURE;
  return subchannel_->CheckConnectivityState();
}

void OutlierDetectionLb::SubchannelWrapper::WatchConnectivityState(
    grpc_connectivity_state initial_state,
    std::unique_ptr<ConnectivityStateWatcherInterface> watcher) {
  void* trace[256];
  int n = absl::GetStackTrace(trace, 256, 0);
  for (int i = 0; i <= n; ++i) {
    char tmp[1024];
    const char* symbol = "(unknown)";
    if (absl::Symbolize(trace[i], tmp, sizeof(tmp))) {
      symbol = tmp;
    }
    gpr_log(GPR_ERROR, "WatchConnectivityState stack %p %s", trace[i], symbol);
  }
  watcher_ = new SubchannelWrapper::WatcherWrapper(std::move(watcher));
  subchannel_->WatchConnectivityState(
      initial_state,
      std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>(
          watcher_));
}

void OutlierDetectionLb::SubchannelWrapper::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  void* trace[256];
  int n = absl::GetStackTrace(trace, 256, 0);
  for (int i = 0; i <= n; ++i) {
    char tmp[1024];
    const char* symbol = "(unknown)";
    if (absl::Symbolize(trace[i], tmp, sizeof(tmp))) {
      symbol = tmp;
    }
    gpr_log(GPR_ERROR, "CancelConnectivityStateWatch stack %p %s", trace[i],
            symbol);
  }
  subchannel_->CancelConnectivityStateWatch(watcher_);
  watcher_ = nullptr;
}

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
  if (complete_pick != nullptr) {
    gpr_log(GPR_INFO, "donna unwrapping");
    // Unwrap subchannel to pass back up the stack.
    auto* subchannel_wrapper =
        static_cast<SubchannelWrapper*>(complete_pick->subchannel.get());
    complete_pick->subchannel = subchannel_wrapper->subchannel();
  }
  return result;
}

//
// OutlierDetectionLb
//

OutlierDetectionLb::OutlierDetectionLb(RefCountedPtr<XdsClient> xds_client,
                                       Args args)
    : LoadBalancingPolicy(std::move(args)), xds_client_(std::move(xds_client)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] created -- using xds client %p", this,
            xds_client_.get());
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
  xds_client_.reset();
}

void OutlierDetectionLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void OutlierDetectionLb::ResetBackoffLocked() {
  // The XdsClient will have its backoff reset by the xds resolver, so we
  // don't need to do it here.
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
              "status=(%s) "
              "picker=%p",
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
  return MakeRefCounted<SubchannelWrapper>(
      outlier_detection_policy_->channel_control_helper()->CreateSubchannel(
          std::move(address), args));
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
    RefCountedPtr<XdsClient> xds_client =
        XdsClient::GetFromChannelArgs(*args.args);
    if (xds_client == nullptr) {
      gpr_log(GPR_ERROR,
              "XdsClient not present in channel args -- cannot instantiate "
              "outlier_detection LB policy");
      return nullptr;
    }
    return MakeOrphanable<OutlierDetectionLb>(std::move(xds_client),
                                              std::move(args));
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
    // Outlier detection policy
    XdsClusterResource::OutlierDetection outlier_detection_policy;
    Duration temp_duration;
    if (ParseJsonObjectFieldAsDuration(json.object_value(), "interval",
                                       &temp_duration, &error_list)) {
      outlier_detection_policy.interval = temp_duration;
    }
    if (ParseJsonObjectFieldAsDuration(json.object_value(), "baseEjectionTime",
                                       &temp_duration, &error_list)) {
      outlier_detection_policy.base_ejection_time = temp_duration;
    }
    if (ParseJsonObjectFieldAsDuration(json.object_value(), "maxEjectionTime",
                                       &temp_duration, &error_list)) {
      outlier_detection_policy.max_ejection_time = temp_duration;
    }
    ParseJsonObjectField(json.object_value(), "maxEjectionPercent",
                         &outlier_detection_policy.max_ejection_percent,
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
            &outlier_detection_policy.success_rate_ejection->stdev_factor,
            &error_list);
        ParseJsonObjectField(object, "enforcementPercentage",
                             &outlier_detection_policy.success_rate_ejection
                                  ->enforcement_percentage,
                             &error_list);
        ParseJsonObjectField(
            object, "minimumHosts",
            &outlier_detection_policy.success_rate_ejection->minimum_hosts,
            &error_list);
        ParseJsonObjectField(
            object, "requestVolume",
            &outlier_detection_policy.success_rate_ejection->request_volume,
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
            &outlier_detection_policy.failure_percentage_ejection->threshold,
            &error_list);
        ParseJsonObjectField(
            object, "enforcementPercentage",
            &outlier_detection_policy.failure_percentage_ejection
                 ->enforcement_percentage,
            &error_list);
        ParseJsonObjectField(object, "minimumHosts",
                             &outlier_detection_policy
                                  .failure_percentage_ejection->minimum_hosts,
                             &error_list);
        ParseJsonObjectField(object, "requestVolume",
                             &outlier_detection_policy
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
        std::move(outlier_detection_policy), std::move(child_policy));
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
