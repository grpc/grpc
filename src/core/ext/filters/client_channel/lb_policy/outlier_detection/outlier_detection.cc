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

#include <inttypes.h>
#include <limits.h>

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
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

TraceFlag grpc_lb_outlier_detection_trace(false, "outlier_detection_lb");

constexpr char kOutlierDetection[] = "outlier_detection_experimental";

// OutlierDetectionLbConfig
const char* OutlierDetectionLbConfig::name() const { return kOutlierDetection; }

//
// OutlierDetectionLb
//

OutlierDetectionLb::OutlierDetectionLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] created", this);
  }
}

const char* OutlierDetectionLb::name() const { return kOutlierDetection; }

OutlierDetectionLb::~OutlierDetectionLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] destroying outlier_detection LB policy",
            this);
  }
  grpc_channel_args_destroy(args_);
}

void OutlierDetectionLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] shutting down", this);
  }
  shutting_down_ = true;
}

void OutlierDetectionLb::ExitIdleLocked() { child_->ExitIdleLocked(); }

void OutlierDetectionLb::ResetBackoffLocked() { child_->ResetBackoffLocked(); }

void OutlierDetectionLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] received update", this);
  }
  // Update config.
  config_ = std::move(args.config);
  // Update args.
  grpc_channel_args_destroy(args_);
  args_ = args.args;
  args.args = nullptr;
  update_in_progress_ = true;
  if (child_ == nullptr) {
    child_ = MakeOrphanable<ChildOutlierDetection>(
        Ref(DEBUG_LOCATION, "ChildOutlierDetection"),
        "outlier_detection_child");
  }
  child_->UpdateLocked(std::move(args.addresses), config_->child().config,
                       config_->child().ignore_reresolution_requests);
  update_in_progress_ = false;
}

void OutlierDetectionLb::HandleChildConnectivityStateChangeLocked(
    ChildOutlierDetection* child) {
  if (update_in_progress_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(
        GPR_INFO,
        "[outlier_detection_lb %p] state update for outlier_detection child %s",
        this, child->name().c_str());
  }
  channel_control_helper()->UpdateState(child->connectivity_state(),
                                        child->connectivity_status(),
                                        child->GetPicker());
}

//
// OutlierDetectionLb::ChildOutlierDetection
//

OutlierDetectionLb::ChildOutlierDetection::ChildOutlierDetection(
    RefCountedPtr<OutlierDetectionLb> outlier_detection_policy,
    std::string name)
    : outlier_detection_policy_(std::move(outlier_detection_policy)),
      name_(std::move(name)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] creating child %s (%p)",
            outlier_detection_policy_.get(), name_.c_str(), this);
  }
}

void OutlierDetectionLb::ChildOutlierDetection::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] child %s (%p): orphaned",
            outlier_detection_policy_.get(), name_.c_str(), this);
  }
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(
      child_policy_->interested_parties(),
      outlier_detection_policy_->interested_parties());
  child_policy_.reset();
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_wrapper_.reset();
  Unref(DEBUG_LOCATION, "ChildOutlierDetection+Orphan");
}

void OutlierDetectionLb::ChildOutlierDetection::UpdateLocked(
    absl::StatusOr<ServerAddressList> addresses,
    RefCountedPtr<LoadBalancingPolicy::Config> config,
    bool ignore_reresolution_requests) {
  if (outlier_detection_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] child %s (%p): start update",
            outlier_detection_policy_.get(), name_.c_str(), this);
  }
  ignore_reresolution_requests_ = ignore_reresolution_requests;
  // Create policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(outlier_detection_policy_->args_);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.addresses = std::move(addresses);
  update_args.config = std::move(config);
  update_args.args = grpc_channel_args_copy(outlier_detection_policy_->args_);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] child %s (%p): updating child policy "
            "handler %p",
            outlier_detection_policy_.get(), name_.c_str(), this,
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

OrphanablePtr<LoadBalancingPolicy>
OutlierDetectionLb::ChildOutlierDetection::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = outlier_detection_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_lb_outlier_detection_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] child %s (%p): created new child policy "
            "handler %p",
            outlier_detection_policy_.get(), name_.c_str(), this,
            lb_policy.get());
  }
  // Add the parent's interested_parties pollset_set to that of the newly
  // created child policy. This will make the child policy progress upon
  // activity on the parent LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(
      lb_policy->interested_parties(),
      outlier_detection_policy_->interested_parties());
  return lb_policy;
}

void OutlierDetectionLb::ChildOutlierDetection::ExitIdleLocked() {
  child_policy_->ExitIdleLocked();
}

void OutlierDetectionLb::ChildOutlierDetection::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
}

void OutlierDetectionLb::ChildOutlierDetection::OnConnectivityStateUpdateLocked(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_outlier_detection_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] child %s (%p): state update: %s (%s) "
            "picker %p",
            outlier_detection_policy_.get(), name_.c_str(), this,
            ConnectivityStateName(state), status.ToString().c_str(),
            picker.get());
  }
  // Store the state and picker.
  connectivity_state_ = state;
  connectivity_status_ = status;
  picker_wrapper_ = MakeRefCounted<RefCountedPicker>(std::move(picker));
  // Notify the parent policy.
  outlier_detection_policy_->HandleChildConnectivityStateChangeLocked(this);
}

//
// OutlierDetectionLb::ChildOutlierDetection::Helper
//

RefCountedPtr<SubchannelInterface>
OutlierDetectionLb::ChildOutlierDetection::Helper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (outlier_detection_->outlier_detection_policy_->shutting_down_)
    return nullptr;
  return outlier_detection_->outlier_detection_policy_->channel_control_helper()
      ->CreateSubchannel(std::move(address), args);
}

void OutlierDetectionLb::ChildOutlierDetection::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (outlier_detection_->outlier_detection_policy_->shutting_down_) return;
  // Notify the outlier_detection.
  outlier_detection_->OnConnectivityStateUpdateLocked(state, status,
                                                      std::move(picker));
}

void OutlierDetectionLb::ChildOutlierDetection::Helper::RequestReresolution() {
  if (outlier_detection_->outlier_detection_policy_->shutting_down_) return;
  if (outlier_detection_->ignore_reresolution_requests_) {
    return;
  }
  outlier_detection_->outlier_detection_policy_->channel_control_helper()
      ->RequestReresolution();
}

absl::string_view
OutlierDetectionLb::ChildOutlierDetection::Helper::GetAuthority() {
  return outlier_detection_->outlier_detection_policy_->channel_control_helper()
      ->GetAuthority();
}

void OutlierDetectionLb::ChildOutlierDetection::Helper::AddTraceEvent(
    TraceSeverity severity, absl::string_view message) {
  if (outlier_detection_->outlier_detection_policy_->shutting_down_) return;
  outlier_detection_->outlier_detection_policy_->channel_control_helper()
      ->AddTraceEvent(severity, message);
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
      // outlier_detection was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:outlier_detection policy requires "
          "configuration. Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error_handle> error_list;
    // Child policy.
    OutlierDetectionLbConfig::OutlierDetectionLbChild child;
    auto it = json.object_value().find("childPolicy");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:childPolicy error:required field missing"));
    } else {
      grpc_error_handle parse_error = GRPC_ERROR_NONE;
      auto child_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
          it->second, &parse_error);
      bool ignore_reresolution_requests = false;
      // If present, ignore_reresolution_requests must be of type
      // boolean.
      auto ignore = json.object_value().find("ignore_reresolution_requests");
      if (ignore != json.object_value().end()) {
        if (ignore->second.type() == Json::Type::JSON_TRUE) {
          ignore_reresolution_requests = true;
        } else if (ignore->second.type() != Json::Type::JSON_FALSE) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
              absl::StrCat("child field:ignore_reresolution_requests:should "
                           "be type boolean")));
        }
      }
      if (child_policy == nullptr) {
        GPR_DEBUG_ASSERT(parse_error != GRPC_ERROR_NONE);
        std::vector<grpc_error_handle> child_errors;
        child_errors.push_back(parse_error);
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_VECTOR("field:childPolicy", &child_errors));
      }
      child.config = std::move(child_policy);
      child.ignore_reresolution_requests = ignore_reresolution_requests;
    }
    // TODO@donnadionne more parsing of outlier detection here
    std::string interval;
    it = json.object_value().find("interval");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:interval error:required field missing"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:interval error:type should be string"));
    } else {
      interval = it->second.string_value();
    }
    return MakeRefCounted<OutlierDetectionLbConfig>(std::move(child));
  }
};

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
