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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"

#define GRPC_XDS_ROUTING_CHILD_RETENTION_INTERVAL_MS (15 * 60 * 1000)

namespace grpc_core {

TraceFlag grpc_xds_routing_lb_trace(false, "xds_routing_lb");

namespace {

constexpr char kXdsRouting[] = "xds_routing_experimental";

// Config for xds_routing LB policy.
class XdsRoutingLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct Matcher {
    std::string service;
    std::string method;
  };
  struct Route {
    Matcher matcher;
    std::string action;
  };
  using RouteTable = std::vector<Route>;
  using ActionMap =
      std::map<std::string, RefCountedPtr<LoadBalancingPolicy::Config>>;

  XdsRoutingLbConfig(ActionMap action_map, RouteTable route_table)
      : action_map_(std::move(action_map)),
        route_table_(std::move(route_table)) {}

  const char* name() const override { return kXdsRouting; }

  const ActionMap& action_map() const { return action_map_; }

  const RouteTable& route_table() const { return route_table_; }

 private:
  ActionMap action_map_;
  RouteTable route_table_;
};

// xds_routing LB policy.
class XdsRoutingLb : public LoadBalancingPolicy {
 public:
  explicit XdsRoutingLb(Args args);

  const char* name() const override { return kXdsRouting; }

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
  class RoutePicker : public SubchannelPicker {
   public:
    struct Route {
      XdsRoutingLbConfig::Matcher matcher;
      RefCountedPtr<ChildPickerWrapper> picker;
    };

    // Maintains an ordered xds route table as provided by RDS response.
    using RouteTable = std::vector<Route>;

    explicit RoutePicker(RouteTable route_table)
        : route_table_(std::move(route_table)) {}

    PickResult Pick(PickArgs args) override;

   private:
    RouteTable route_table_;
  };

  // Each XdsRoutingChild holds a ref to its parent XdsRoutingLb.
  class XdsRoutingChild : public InternallyRefCounted<XdsRoutingChild> {
   public:
    XdsRoutingChild(RefCountedPtr<XdsRoutingLb> xds_routing_policy,
                    const std::string& name);
    ~XdsRoutingChild();

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
      explicit Helper(RefCountedPtr<XdsRoutingChild> xds_routing_child)
          : xds_routing_child_(std::move(xds_routing_child)) {}

      ~Helper() { xds_routing_child_.reset(DEBUG_LOCATION, "Helper"); }

      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      void AddTraceEvent(TraceSeverity severity,
                         absl::string_view message) override;

     private:
      RefCountedPtr<XdsRoutingChild> xds_routing_child_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const grpc_channel_args* args);

    static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
    void OnDelayedRemovalTimerLocked(grpc_error* error);

    // The owning LB policy.
    RefCountedPtr<XdsRoutingLb> xds_routing_policy_;

    // Points to the corresponding key in XdsRoutingLb::actions_.
    const std::string& name_;

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

  ~XdsRoutingLb();

  void ShutdownLocked() override;

  void UpdateStateLocked();

  // Current config from the resolver.
  RefCountedPtr<XdsRoutingLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  // Children.
  std::map<std::string, OrphanablePtr<XdsRoutingChild>> actions_;
};

//
// XdsRoutingLb::RoutePicker
//

XdsRoutingLb::PickResult XdsRoutingLb::RoutePicker::Pick(PickArgs args) {
  absl::string_view path;
  // TODO(roth): Using const auto& here trigger a warning in a macos or windows
  // build:
  //*(args.initial_metadata) is returning values not references.
  for (const auto p : *(args.initial_metadata)) {
    if (p.first == ":path") {
      path = p.second;
      break;
    }
  }
  std::vector<absl::string_view> path_elements =
      absl::StrSplit(path.substr(1), '/');
  for (const Route& route : route_table_) {
    if ((path_elements[0] == route.matcher.service &&
         (path_elements[1] == route.matcher.method ||
          route.matcher.method.empty())) ||
        (route.matcher.service.empty() && route.matcher.method.empty())) {
      return route.picker->Pick(args);
    }
  }
  PickResult result;
  result.type = PickResult::PICK_FAILED;
  result.error =
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                             "xds routing picker: no matching route"),
                         GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL);
  return result;
}

//
// XdsRoutingLb
//

XdsRoutingLb::XdsRoutingLb(Args args) : LoadBalancingPolicy(std::move(args)) {}

XdsRoutingLb::~XdsRoutingLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_routing_lb %p] destroying xds_routing LB policy",
            this);
  }
}

void XdsRoutingLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_routing_lb %p] shutting down", this);
  }
  shutting_down_ = true;
  actions_.clear();
}

void XdsRoutingLb::ExitIdleLocked() {
  for (auto& p : actions_) p.second->ExitIdleLocked();
}

void XdsRoutingLb::ResetBackoffLocked() {
  for (auto& p : actions_) p.second->ResetBackoffLocked();
}

void XdsRoutingLb::UpdateLocked(UpdateArgs args) {
  if (shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_routing_lb %p] Received update", this);
  }
  // Update config.
  config_ = std::move(args.config);
  // Deactivate the actions not in the new config.
  for (const auto& p : actions_) {
    const std::string& name = p.first;
    XdsRoutingChild* child = p.second.get();
    if (config_->action_map().find(name) == config_->action_map().end()) {
      child->DeactivateLocked();
    }
  }
  // Add or update the actions in the new config.
  for (const auto& p : config_->action_map()) {
    const std::string& name = p.first;
    const RefCountedPtr<LoadBalancingPolicy::Config>& config = p.second;
    auto it = actions_.find(name);
    if (it == actions_.end()) {
      it = actions_.emplace(std::make_pair(name, nullptr)).first;
      it->second = MakeOrphanable<XdsRoutingChild>(
          Ref(DEBUG_LOCATION, "XdsRoutingChild"), it->first);
    }
    it->second->UpdateLocked(config, args.addresses, args.args);
  }
}

void XdsRoutingLb::UpdateStateLocked() {
  // Also count the number of children in each state, to determine the
  // overall state.
  size_t num_ready = 0;
  size_t num_connecting = 0;
  size_t num_idle = 0;
  size_t num_transient_failures = 0;
  for (const auto& p : actions_) {
    const auto& child_name = p.first;
    const XdsRoutingChild* child = p.second.get();
    // Skip the actions that are not in the latest update.
    if (config_->action_map().find(child_name) == config_->action_map().end()) {
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_routing_lb %p] connectivity changed to %s", this,
            ConnectivityStateName(connectivity_state));
  }
  std::unique_ptr<SubchannelPicker> picker;
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY: {
      RoutePicker::RouteTable route_table;
      for (const auto& config_route : config_->route_table()) {
        RoutePicker::Route route;
        route.matcher = config_route.matcher;
        route.picker = actions_[config_route.action]->picker_wrapper();
        if (route.picker == nullptr) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
            gpr_log(GPR_INFO,
                    "[xds_routing_lb %p] child %s has not yet returned a "
                    "picker; creating a QueuePicker.",
                    this, config_route.action.c_str());
          }
          route.picker = MakeRefCounted<ChildPickerWrapper>(
              config_route.action, absl::make_unique<QueuePicker>(
                                       Ref(DEBUG_LOCATION, "QueuePicker")));
        }
        route_table.push_back(std::move(route));
      }
      picker = absl::make_unique<RoutePicker>(std::move(route_table));
      break;
    }
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
      picker =
          absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker"));
      break;
    default:
      picker = absl::make_unique<TransientFailurePicker>(grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "TRANSIENT_FAILURE from XdsRoutingLb"),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
  }
  channel_control_helper()->UpdateState(connectivity_state, std::move(picker));
}

//
// XdsRoutingLb::XdsRoutingChild
//

XdsRoutingLb::XdsRoutingChild::XdsRoutingChild(
    RefCountedPtr<XdsRoutingLb> xds_routing_policy, const std::string& name)
    : xds_routing_policy_(std::move(xds_routing_policy)), name_(name) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO, "[xds_routing_lb %p] created XdsRoutingChild %p for %s",
            xds_routing_policy_.get(), this, name_.c_str());
  }
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
}

XdsRoutingLb::XdsRoutingChild::~XdsRoutingChild() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_routing_lb %p] XdsRoutingChild %p: destroying child",
            xds_routing_policy_.get(), this);
  }
  xds_routing_policy_.reset(DEBUG_LOCATION, "XdsRoutingChild");
}

void XdsRoutingLb::XdsRoutingChild::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_routing_lb %p] XdsRoutingChild %p %s: shutting down child",
            xds_routing_policy_.get(), this, name_.c_str());
  }
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                   xds_routing_policy_->interested_parties());
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
XdsRoutingLb::XdsRoutingChild::CreateChildPolicyLocked(
    const grpc_channel_args* args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = xds_routing_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      absl::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_xds_routing_lb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_routing_lb %p] XdsRoutingChild %p %s: Created new child "
            "policy handler %p",
            xds_routing_policy_.get(), this, name_.c_str(), lb_policy.get());
  }
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   xds_routing_policy_->interested_parties());
  return lb_policy;
}

void XdsRoutingLb::XdsRoutingChild::UpdateLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> config,
    const ServerAddressList& addresses, const grpc_channel_args* args) {
  if (xds_routing_policy_->shutting_down_) return;
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_routing_lb %p] XdsRoutingChild %p %s: Updating child "
            "policy handler %p",
            xds_routing_policy_.get(), this, name_.c_str(),
            child_policy_.get());
  }
  child_policy_->UpdateLocked(std::move(update_args));
}

void XdsRoutingLb::XdsRoutingChild::ExitIdleLocked() {
  child_policy_->ExitIdleLocked();
}

void XdsRoutingLb::XdsRoutingChild::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
}

void XdsRoutingLb::XdsRoutingChild::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (delayed_removal_timer_callback_pending_ == true) return;
  // Set the child weight to 0 so that future picker won't contain this child.
  // Start a timer to delete the child.
  Ref(DEBUG_LOCATION, "XdsRoutingChild+timer").release();
  grpc_timer_init(
      &delayed_removal_timer_,
      ExecCtx::Get()->Now() + GRPC_XDS_ROUTING_CHILD_RETENTION_INTERVAL_MS,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

void XdsRoutingLb::XdsRoutingChild::OnDelayedRemovalTimer(void* arg,
                                                          grpc_error* error) {
  XdsRoutingChild* self = static_cast<XdsRoutingChild*>(arg);
  GRPC_ERROR_REF(error);  // Ref owned by the lambda
  self->xds_routing_policy_->work_serializer()->Run(
      [self, error]() { self->OnDelayedRemovalTimerLocked(error); },
      DEBUG_LOCATION);
}

void XdsRoutingLb::XdsRoutingChild::OnDelayedRemovalTimerLocked(
    grpc_error* error) {
  delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !shutdown_) {
    xds_routing_policy_->actions_.erase(name_);
  }
  Unref(DEBUG_LOCATION, "XdsRoutingChild+timer");
  GRPC_ERROR_UNREF(error);
}

//
// XdsRoutingLb::XdsRoutingChild::Helper
//

RefCountedPtr<SubchannelInterface>
XdsRoutingLb::XdsRoutingChild::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (xds_routing_child_->xds_routing_policy_->shutting_down_) return nullptr;
  return xds_routing_child_->xds_routing_policy_->channel_control_helper()
      ->CreateSubchannel(args);
}

void XdsRoutingLb::XdsRoutingChild::Helper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_routing_lb %p] child %s: received update: state=%s picker=%p",
            xds_routing_child_->xds_routing_policy_.get(),
            xds_routing_child_->name_.c_str(), ConnectivityStateName(state),
            picker.get());
  }
  if (xds_routing_child_->xds_routing_policy_->shutting_down_) return;
  // Cache the picker in the XdsRoutingChild.
  xds_routing_child_->picker_wrapper_ = MakeRefCounted<ChildPickerWrapper>(
      xds_routing_child_->name_, std::move(picker));
  // Decide what state to report for aggregation purposes.
  // If we haven't seen a failure since the last time we were in state
  // READY, then we report the state change as-is.  However, once we do see
  // a failure, we report TRANSIENT_FAILURE and ignore any subsequent state
  // changes until we go back into state READY.
  if (!xds_routing_child_->seen_failure_since_ready_) {
    if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      xds_routing_child_->seen_failure_since_ready_ = true;
    }
  } else {
    if (state != GRPC_CHANNEL_READY) return;
    xds_routing_child_->seen_failure_since_ready_ = false;
  }
  xds_routing_child_->connectivity_state_ = state;
  // Notify the LB policy.
  xds_routing_child_->xds_routing_policy_->UpdateStateLocked();
}

void XdsRoutingLb::XdsRoutingChild::Helper::RequestReresolution() {
  if (xds_routing_child_->xds_routing_policy_->shutting_down_) return;
  xds_routing_child_->xds_routing_policy_->channel_control_helper()
      ->RequestReresolution();
}

void XdsRoutingLb::XdsRoutingChild::Helper::AddTraceEvent(
    TraceSeverity severity, absl::string_view message) {
  if (xds_routing_child_->xds_routing_policy_->shutting_down_) return;
  xds_routing_child_->xds_routing_policy_->channel_control_helper()
      ->AddTraceEvent(severity, message);
}

//
// factory
//

class XdsRoutingLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<XdsRoutingLb>(std::move(args));
  }

  const char* name() const override { return kXdsRouting; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json.type() == Json::Type::JSON_NULL) {
      // xds_routing was mentioned as a policy in the deprecated
      // loadBalancingPolicy field or in the client API.
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:loadBalancingPolicy error:xds_routing policy requires "
          "configuration.  Please use loadBalancingConfig field of service "
          "config instead.");
      return nullptr;
    }
    std::vector<grpc_error*> error_list;
    // action map.
    XdsRoutingLbConfig::ActionMap action_map;
    std::set<std::string /*action_name*/> actions_to_be_used;
    auto it = json.object_value().find("actions");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:actions error:required field not present"));
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:actions error:type should be object"));
    } else {
      for (const auto& p : it->second.object_value()) {
        if (p.first.empty()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:actions element error: name cannot be empty"));
          continue;
        }
        RefCountedPtr<LoadBalancingPolicy::Config> child_config;
        std::vector<grpc_error*> child_errors =
            ParseChildConfig(p.second, &child_config);
        if (!child_errors.empty()) {
          // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
          // string is not static in this case.
          grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:actions name:", p.first).c_str());
          for (grpc_error* child_error : child_errors) {
            error = grpc_error_add_child(error, child_error);
          }
          error_list.push_back(error);
        } else {
          action_map[p.first] = std::move(child_config);
          actions_to_be_used.insert(p.first);
        }
      }
    }
    if (action_map.empty()) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no valid actions configured"));
    }
    XdsRoutingLbConfig::RouteTable route_table;
    it = json.object_value().find("routes");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:routes error:required field not present"));
    } else if (it->second.type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:routes error:type should be array"));
    } else {
      const Json::Array& array = it->second.array_value();
      for (size_t i = 0; i < array.size(); ++i) {
        XdsRoutingLbConfig::Route route;
        std::vector<grpc_error*> route_errors =
            ParseRoute(array[i], action_map, &route, &actions_to_be_used);
        if (!route_errors.empty()) {
          // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
          // string is not static in this case.
          grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("field:routes element: ", i, " error").c_str());
          for (grpc_error* route_error : route_errors) {
            error = grpc_error_add_child(error, route_error);
          }
          error_list.push_back(error);
        }
        route_table.emplace_back(std::move(route));
      }
    }
    if (route_table.empty()) {
      grpc_error* error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no valid routes configured");
      error_list.push_back(error);
    }
    if (!route_table.back().matcher.service.empty() ||
        !route_table.back().matcher.method.empty()) {
      grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "default route must not contain service or method");
      error_list.push_back(error);
    }
    if (!actions_to_be_used.empty()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "some actions were not referenced by any route"));
    }
    if (!error_list.empty()) {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "xds_routing_experimental LB policy config", &error_list);
      return nullptr;
    }
    return MakeRefCounted<XdsRoutingLbConfig>(std::move(action_map),
                                              std::move(route_table));
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

  static std::vector<grpc_error*> ParseMethodName(
      const Json& json, XdsRoutingLbConfig::Matcher* route_config) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    // Parse service
    auto it = json.object_value().find("service");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:service error: should be string"));
      } else {
        route_config->service = it->second.string_value();
      }
    }
    // Parse method
    it = json.object_value().find("method");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:method error: should be string"));
      } else {
        route_config->method = it->second.string_value();
      }
    }
    if (route_config->service.empty() && !route_config->method.empty()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "service is empty when method is not"));
    }
    return error_list;
  }

  static std::vector<grpc_error*> ParseRoute(
      const Json& json, const XdsRoutingLbConfig::ActionMap& action_map,
      XdsRoutingLbConfig::Route* route,
      std::set<std::string /*action_name*/>* actions_to_be_used) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    // Parse MethodName.
    auto it = json.object_value().find("methodName");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:methodName error:required field missing"));
    } else {
      std::vector<grpc_error*> method_name_errors =
          ParseMethodName(it->second, &route->matcher);
      if (!method_name_errors.empty()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
            "field:methodName", &method_name_errors));
      }
    }
    // Parse action.
    it = json.object_value().find("action");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:action error:required field missing"));
    } else if (it->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:action error:should be of type string"));
    } else {
      route->action = it->second.string_value();
      if (route->action.empty()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:action error:cannot be empty"));
      } else {
        // Validate action exists and mark it as used.
        if (action_map.find(route->action) == action_map.end()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              absl::StrCat("field:action error:", route->action,
                           " does not exist")
                  .c_str()));
        }
        actions_to_be_used->erase(route->action);
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

void grpc_lb_policy_xds_routing_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::XdsRoutingLbFactory>());
}

void grpc_lb_policy_xds_routing_shutdown() {}
