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
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/resolver/xds/xds_resolver.h"
#include "src/core/ext/xds/xds_api.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/transport/error_utils.h"

#define GRPC_XDS_CLUSTER_MANAGER_CHILD_RETENTION_INTERVAL_MS (15 * 60 * 1000)

namespace grpc_core {

TraceFlag grpc_xds_cluster_manager_lb_trace(false, "xds_cluster_manager_lb");

namespace {

constexpr char kXdsClusterManager[] = "xds_cluster_manager_experimental";

// Config for xds_cluster_manager LB policy.
class XdsClusterManagerLbConfig : public LoadBalancingPolicy::Config {
 public:
  using ClusterMap =
      std::map<std::string, RefCountedPtr<LoadBalancingPolicy::Config>>;

  XdsClusterManagerLbConfig(ClusterMap cluster_map)
      : cluster_map_(std::move(cluster_map)) {}

  const char* name() const override { return kXdsClusterManager; }

  const ClusterMap& cluster_map() const { return cluster_map_; }

 private:
  ClusterMap cluster_map_;
};

// xds_cluster_manager LB policy.
class XdsClusterManagerLb : public LoadBalancingPolicy {
 public:
  explicit XdsClusterManagerLb(Args args);

  const char* name() const override { return kXdsClusterManager; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // A simple wrapper for ref-counting a picker from the child policy.
  class ChildPickerWrapper : public RefCounted<ChildPickerWrapper> {
   public:
    ChildPickerWrapper(std::string name,
                       std::unique_ptr<SubchannelPicker> picker)
        : name_(std::move(name)), picker_(std::move(picker)) {}
    PickResult Pick(PickArgs args) { return picker_->Pick(args); }

    const std::string& name() const { return name_; }

   private:
    std::string name_;
    std::unique_ptr<SubchannelPicker> picker_;
  };

  // Picks a child using prefix or path matching and then delegates to that
  // child's picker.
  class ClusterPicker : public SubchannelPicker {
   public:
    // Maintains a map of cluster names to pickers.
    // This uses absl::string_view instead of std::string as the key.
    using ClusterMap = std::map<absl::string_view /*cluster_name*/,
                                RefCountedPtr<ChildPickerWrapper>>;

    // It is required that the keys of cluster_map have to live at least as long
    // as the ClusterPick instance.
    ClusterPicker(ClusterMap cluster_map,
                  RefCountedPtr<XdsClusterManagerLbConfig> config)
        : cluster_map_(cluster_map), config_(std::move(config)) {}

    PickResult Pick(PickArgs args) override;

   private:
    ClusterMap cluster_map_;
    std::vector<std::string> cluster_name_storage_;
    // Take a reference to config so that we can use
    // XdsApi::RdsUpdate::RdsRoute::Matchers from it.
    RefCountedPtr<XdsClusterManagerLbConfig> config_;
  };

  // Each XdsClusterManagerChild holds a ref to its parent XdsClusterManagerLb.
  class XdsClusterManagerChild
      : public InternallyRefCounted<XdsClusterManagerChild> {
   public:
    XdsClusterManagerChild(
        RefCountedPtr<XdsClusterManagerLb> xds_cluster_manager_policy,
        const std::string& name);
    ~XdsClusterManagerChild();

    void Orphan() override;

    void UpdateLocked(RefCountedPtr<LoadBalancingPolicy::Config> config,
                      const ServerAddressList& addresses,
                      const grpc_channel_args* args);
    void ExitIdleLocked();
    void ResetBackoffLocked();
    void DeactivateLocked();

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }
    RefCountedPtr<ChildPickerWrapper> picker_wrapper() const {
      return picker_wrapper_;
    }

   private:
    class Helper : public ChannelControlHelper {
     public:
      explicit Helper(
          RefCountedPtr<XdsClusterManagerChild> xds_cluster_manager_child)
          : xds_cluster_manager_child_(std::move(xds_cluster_manager_child)) {}

      ~Helper() { xds_cluster_manager_child_.reset(DEBUG_LOCATION, "Helper"); }

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      void AddTraceEvent(TraceSeverity severity,
                         absl::string_view message) override;

     private:
      RefCountedPtr<XdsClusterManagerChild> xds_cluster_manager_child_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const grpc_channel_args* args);

    static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
    void OnDelayedRemovalTimerLocked(grpc_error* error);

    // The owning LB policy.
    RefCountedPtr<XdsClusterManagerLb> xds_cluster_manager_policy_;

    // Points to the corresponding key in children map.
    const std::string name_;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    RefCountedPtr<ChildPickerWrapper> picker_wrapper_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    bool seen_failure_since_ready_ = false;

    // States for delayed removal.
    grpc_timer delayed_removal_timer_;
    grpc_closure on_delayed_removal_timer_;
    bool delayed_removal_timer_callback_pending_ = false;
    bool shutdown_ = false;
  };

  ~XdsClusterManagerLb();

  void ShutdownLocked() override;

  void UpdateStateLocked();

  // Current config from the resolver.
  RefCountedPtr<XdsClusterManagerLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // Children.
  std::map<std::string, OrphanablePtr<XdsClusterManagerChild>> children_;
};

//
// XdsClusterManagerLb::ClusterPicker
//
XdsClusterManagerLb::PickResult XdsClusterManagerLb::ClusterPicker::Pick(
    PickArgs args) {
  auto cluster = cluster_map_.find(
      args.call_state->ExperimentalGetCallAttribute(kXdsClusterAttribute));
  if (cluster != cluster_map_.end()) {
    return cluster->second->Pick(args);
  }
  PickResult result;
  result.type = PickResult::PICK_FAILED;
  result.error =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                             "xds cluster_manager picker: no matching route"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL);
  return result;
}

//
// XdsClusterManagerLb
//

XdsClusterManagerLb::XdsClusterManagerLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {}

XdsClusterManagerLb::~XdsClusterManagerLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(
        GPR_INFO,
        "[xds_cluster_manager_lb %p] destroying xds_cluster_manager LB policy",
        this);
  }
}

void XdsClusterManagerLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_manager_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  children_.clear();
}

void XdsClusterManagerLb::ExitIdleLocked() {
  for (auto& p : children_) p.second->ExitIdleLocked();
}

void XdsClusterManagerLb::ResetBackoffLocked() {
  for (auto& p : children_) p.second->ResetBackoffLocked();
}

void XdsClusterManagerLb::UpdateLocked(UpdateArgs args) {
  if (shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_manager_lb %p] Received update", this);
  }
  // Update config.
  config_ = std::move(args.config);
  // Deactivate the children not in the new config.
  for (const auto& p : children_) {
    const std::string& name = p.first;
    XdsClusterManagerChild* child = p.second.get();
    if (config_->cluster_map().find(name) == config_->cluster_map().end()) {
      child->DeactivateLocked();
    }
  }
  // Add or update the children in the new config.
  for (const auto& p : config_->cluster_map()) {
    const std::string& name = p.first;
    const RefCountedPtr<LoadBalancingPolicy::Config>& config = p.second;
    auto it = children_.find(name);
    if (it == children_.end()) {
      it = children_
               .emplace(name, MakeOrphanable<XdsClusterManagerChild>(
                                  Ref(DEBUG_LOCATION, "XdsClusterManagerChild"),
                                  name))
               .first;
    }
    it->second->UpdateLocked(config, args.addresses, args.args);
  }
}

void XdsClusterManagerLb::UpdateStateLocked() {
  // Also count the number of children in each state, to determine the
  // overall state.
  size_t num_ready = 0;
  size_t num_connecting = 0;
  size_t num_idle = 0;
  size_t num_transient_failures = 0;
  for (const auto& p : children_) {
    const auto& child_name = p.first;
    const XdsClusterManagerChild* child = p.second.get();
    // Skip the children that are not in the latest update.
    if (config_->cluster_map().find(child_name) ==
        config_->cluster_map().end()) {
      continue;
    }
    switch (child->connectivity_state()) {
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
  // Determine aggregated connectivity state.
  grpc_connectivity_state connectivity_state;
  if (num_ready > 0) {
    connectivity_state = GRPC_CHANNEL_READY;
  } else if (num_connecting > 0) {
    connectivity_state = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
    connectivity_state = GRPC_CHANNEL_IDLE;
  } else {
    connectivity_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_cluster_manager_lb %p] connectivity changed to %s",
            this, ConnectivityStateName(connectivity_state));
  }
  std::unique_ptr<SubchannelPicker> picker;
  absl::Status status;
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY: {
      ClusterPicker::ClusterMap cluster_map;
      for (const auto& p : config_->cluster_map()) {
        RefCountedPtr<ChildPickerWrapper>& child_picker = cluster_map[p.first];
        child_picker = children_[p.first]->picker_wrapper();
        if (child_picker == nullptr) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
            gpr_log(
                GPR_INFO,
                "[xds_cluster_manager_lb %p] child %s has not yet returned a "
                "picker; creating a QueuePicker.",
                this, p.first.c_str());
          }
          child_picker = MakeRefCounted<ChildPickerWrapper>(
              p.first, absl::make_unique<QueuePicker>(
                           Ref(DEBUG_LOCATION, "QueuePicker")));
        }
      }
      picker =
          absl::make_unique<ClusterPicker>(std::move(cluster_map), config_);
      break;
    }
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
      picker =
          absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker"));
      break;
    default:
      grpc_error* error = grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "TRANSIENT_FAILURE from XdsClusterManagerLb"),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
      status = grpc_error_to_absl_status(error);
      picker = absl::make_unique<TransientFailurePicker>(error);
  }
  channel_control_helper()->UpdateState(connectivity_state, status,
                                        std::move(picker));
}

//
// XdsClusterManagerLb::XdsClusterManagerChild
//

XdsClusterManagerLb::XdsClusterManagerChild::XdsClusterManagerChild(
    RefCountedPtr<XdsClusterManagerLb> xds_cluster_manager_policy,
    const std::string& name)
    : xds_cluster_manager_policy_(std::move(xds_cluster_manager_policy)),
      name_(name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(
        GPR_INFO,
        "[xds_cluster_manager_lb %p] created XdsClusterManagerChild %p for %s",
        xds_cluster_manager_policy_.get(), this, name_.c_str());
  }
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
}

XdsClusterManagerLb::XdsClusterManagerChild::~XdsClusterManagerChild() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_manager_lb %p] XdsClusterManagerChild %p: destroying "
            "child",
            xds_cluster_manager_policy_.get(), this);
  }
  xds_cluster_manager_policy_.reset(DEBUG_LOCATION, "XdsClusterManagerChild");
}

void XdsClusterManagerLb::XdsClusterManagerChild::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_manager_lb %p] XdsClusterManagerChild %p %s: "
            "shutting down child",
            xds_cluster_manager_policy_.get(), this, name_.c_str());
  }
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(
      child_policy_->interested_parties(),
      xds_cluster_manager_policy_->interested_parties());
  child_policy_.reset();
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
XdsClusterManagerLb::XdsClusterManagerChild::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer =
      xds_cluster_manager_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_xds_cluster_manager_lb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_manager_lb %p] XdsClusterManagerChild %p %s: Created "
            "new child "
            "policy handler %p",
            xds_cluster_manager_policy_.get(), this, name_.c_str(),
            lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(
      lb_policy->interested_parties(),
      xds_cluster_manager_policy_->interested_parties());
  return lb_policy;
}

void XdsClusterManagerLb::XdsClusterManagerChild::UpdateLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> config,
    const ServerAddressList& addresses, const grpc_channel_args* args) {
  if (xds_cluster_manager_policy_->shutting_down_) return;
  // Update child weight.
  // Reactivate if needed.
  if (delayed_removal_timer_callback_pending_) {
    delayed_removal_timer_callback_pending_ = false;
    grpc_timer_cancel(&delayed_removal_timer_);
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = std::move(config);
  update_args.addresses = addresses;
  update_args.args = grpc_channel_args_copy(args);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_cluster_manager_lb %p] XdsClusterManagerChild %p %s: "
            "Updating child "
            "policy handler %p",
            xds_cluster_manager_policy_.get(), this, name_.c_str(),
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

void XdsClusterManagerLb::XdsClusterManagerChild::ExitIdleLocked() {
  child_policy_->ExitIdleLocked();
}

void XdsClusterManagerLb::XdsClusterManagerChild::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
}

void XdsClusterManagerLb::XdsClusterManagerChild::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (delayed_removal_timer_callback_pending_ == true) return;
  // Set the child weight to 0 so that future picker won't contain this child.
  // Start a timer to delete the child.
  Ref(DEBUG_LOCATION, "XdsClusterManagerChild+timer").release();
  grpc_timer_init(&delayed_removal_timer_,
                  ExecCtx::Get()->Now() +
                      GRPC_XDS_CLUSTER_MANAGER_CHILD_RETENTION_INTERVAL_MS,
                  &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

void XdsClusterManagerLb::XdsClusterManagerChild::OnDelayedRemovalTimer(
    void* arg, grpc_error* error) {
  XdsClusterManagerChild* self = static_cast<XdsClusterManagerChild*>(arg);
  GRPC_ERROR_REF(error);  // Ref owned by the lambda
  self->xds_cluster_manager_policy_->work_serializer()->Run(
      [self, error]() { self->OnDelayedRemovalTimerLocked(error); },
      DEBUG_LOCATION);
}

void XdsClusterManagerLb::XdsClusterManagerChild::OnDelayedRemovalTimerLocked(
    grpc_error* error) {
  delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !shutdown_) {
    xds_cluster_manager_policy_->children_.erase(name_);
  }
  Unref(DEBUG_LOCATION, "XdsClusterManagerChild+timer");
  GRPC_ERROR_UNREF(error);
}

//
// XdsClusterManagerLb::XdsClusterManagerChild::Helper
//

RefCountedPtr<SubchannelInterface>
XdsClusterManagerLb::XdsClusterManagerChild::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (xds_cluster_manager_child_->xds_cluster_manager_policy_->shutting_down_)
    return nullptr;
  return xds_cluster_manager_child_->xds_cluster_manager_policy_
      ->channel_control_helper()
      ->CreateSubchannel(args);
}

void XdsClusterManagerLb::XdsClusterManagerChild::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_cluster_manager_lb_trace)) {
    gpr_log(
        GPR_INFO,
        "[xds_cluster_manager_lb %p] child %s: received update: state=%s (%s) "
        "picker=%p",
        xds_cluster_manager_child_->xds_cluster_manager_policy_.get(),
        xds_cluster_manager_child_->name_.c_str(), ConnectivityStateName(state),
        status.ToString().c_str(), picker.get());
  }
  if (xds_cluster_manager_child_->xds_cluster_manager_policy_->shutting_down_)
    return;
  // Cache the picker in the XdsClusterManagerChild.
  xds_cluster_manager_child_->picker_wrapper_ =
      MakeRefCounted<ChildPickerWrapper>(xds_cluster_manager_child_->name_,
                                         std::move(picker));
  // Decide what state to report for aggregation purposes.
  // If we haven't seen a failure since the last time we were in state
  // READY, then we report the state change as-is.  However, once we do see
  // a failure, we report TRANSIENT_FAILURE and ignore any subsequent state
  // changes until we go back into state READY.
  if (!xds_cluster_manager_child_->seen_failure_since_ready_) {
    if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      xds_cluster_manager_child_->seen_failure_since_ready_ = true;
    }
  } else {
    if (state != GRPC_CHANNEL_READY) return;
    xds_cluster_manager_child_->seen_failure_since_ready_ = false;
  }
  xds_cluster_manager_child_->connectivity_state_ = state;
  // Notify the LB policy.
  xds_cluster_manager_child_->xds_cluster_manager_policy_->UpdateStateLocked();
}

void XdsClusterManagerLb::XdsClusterManagerChild::Helper::
    RequestReresolution() {
  if (xds_cluster_manager_child_->xds_cluster_manager_policy_->shutting_down_)
    return;
  xds_cluster_manager_child_->xds_cluster_manager_policy_
      ->channel_control_helper()
      ->RequestReresolution();
}

void XdsClusterManagerLb::XdsClusterManagerChild::Helper::AddTraceEvent(
    TraceSeverity severity, absl::string_view message) {
  if (xds_cluster_manager_child_->xds_cluster_manager_policy_->shutting_down_)
    return;
  xds_cluster_manager_child_->xds_cluster_manager_policy_
      ->channel_control_helper()
      ->AddTraceEvent(severity, message);
}

//
// factory
//

class XdsClusterManagerLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<XdsClusterManagerLb>(std::move(args));
  }

  const char* name() const override { return kXdsClusterManager; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // xds_cluster_manager was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:xds_cluster_manager policy requires "
          "configuration.  Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    XdsClusterManagerLbConfig::ClusterMap cluster_map;
    std::set<std::string /*cluster_name*/> clusters_to_be_used;
    auto it = json.object_value().find("children");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:children error:required field not present"));
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:children error:type should be object"));
    } else {
      for (const auto& p : it->second.object_value()) {
        if (p.first.empty()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:children element error: name cannot be empty"));
          continue;
        }
        RefCountedPtr<LoadBalancingPolicy::Config> child_config;
        std::vector<grpc_error*> child_errors =
            ParseChildConfig(p.second, &child_config);
        if (!child_errors.empty()) {
          // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
          // string is not static in this case.
          grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:children name:", p.first).c_str());
          for (grpc_error* child_error : child_errors) {
            error = grpc_error_add_child(error, child_error);
          }
          error_list.push_back(error);
        } else {
          cluster_map[p.first] = std::move(child_config);
          clusters_to_be_used.insert(p.first);
        }
      }
    }
    if (cluster_map.empty()) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no valid children configured"));
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "xds_cluster_manager_experimental LB policy config", &error_list);
      return nullptr;
    }
    return MakeRefCounted<XdsClusterManagerLbConfig>(std::move(cluster_map));
  }

 private:
  static std::vector<grpc_error*> ParseChildConfig(
      const Json& json,
      RefCountedPtr<LoadBalancingPolicy::Config>* child_config) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    auto it = json.object_value().find("childPolicy");
    if (it == json.object_value().end()) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("did not find childPolicy"));
    } else {
      grpc_error* parse_error = GRPC_ERROR_NONE;
      *child_config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
          it->second, &parse_error);
      if (*child_config == nullptr) {
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

void grpc_lb_policy_xds_cluster_manager_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::XdsClusterManagerLbFactory>());
}

void grpc_lb_policy_xds_cluster_manager_shutdown() {}
