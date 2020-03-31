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
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/timer.h"

#define GRPC_XDS_ROUTING_CHILD_RETENTION_INTERVAL_MS (15 * 60 * 1000)

namespace grpc_core {

TraceFlag grpc_xds_routing_lb_trace(false, "xds_routing_lb");

namespace {

constexpr char kXdsRouting[] = "xds_routing_experimental";

// Config for xds_routing LB policy.
class XdsRoutingLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct ChildConfig {
    RefCountedPtr<LoadBalancingPolicy::Config> config;
  };
  struct Matcher {
    std::string service;
    std::string method;
  };
  using RouteTable = std::vector<std::pair<Matcher, std::string>>;
  using ActionMap = std::map<std::string, ChildConfig>;

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
    PickResult Pick(PickArgs args) { return picker_->Pick(std::move(args)); }

    const std::string& name() { return name_; }

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

    RoutePicker(RouteTable route_table)
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

    void UpdateLocked(const XdsRoutingLbConfig::ChildConfig& config,
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
      void AddTraceEvent(TraceSeverity severity, StringView message) override;

     private:
      RefCountedPtr<XdsRoutingChild> xds_routing_child_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const grpc_channel_args* args);

    static void OnDelayedRemovalTimer(void* arg, grpc_error* error);
    static void OnDelayedRemovalTimerLocked(void* arg, grpc_error* error);

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

  const grpc_millis child_retention_interval_ms_;

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
  for (const auto& p : *(args.initial_metadata)) {
    if (p.first == ":path") {
      path = p.second;
      break;
    }
  }
  std::vector<absl::string_view> v = absl::StrSplit(path.substr(1), '/');
  for (int i = 0; i < route_table_.size(); ++i) {
    if (v[0] == route_table_[i].matcher.service &&
        ("" == route_table_[i].matcher.method ||
         v[1] == route_table_[i].matcher.method)) {
      auto picker = route_table_[i].picker;
      if (picker != nullptr) {
        return picker.get()->Pick(args);
      }
    }
  }
  return route_table_[route_table_.size() - 1].picker.get()->Pick(args);
}

//
// XdsRoutingLb
//

XdsRoutingLb::XdsRoutingLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      // FIXME: new channel arg
      child_retention_interval_ms_(grpc_channel_args_find_integer(
          args.args, GRPC_ARG_LOCALITY_RETENTION_INTERVAL_MS,
          {GRPC_XDS_ROUTING_CHILD_RETENTION_INTERVAL_MS, 0, INT_MAX})) {}

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
  for (auto it = actions_.begin(); it != actions_.end();) {
    const std::string& name = it->first;
    XdsRoutingChild* child = it->second.get();
    if (config_->action_map().find(name) != config_->action_map().end()) {
      ++it;
      continue;
    }
    if (child_retention_interval_ms_ == 0) {
      it = actions_.erase(it);
    } else {
      child->DeactivateLocked();
      ++it;
    }
  }
  // Add or update the actions in the new config.
  for (const auto& p : config_->action_map()) {
    const std::string& name = p.first;
    const XdsRoutingLbConfig::ChildConfig& config = p.second;
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
  std::map<std::string, RefCountedPtr<ChildPickerWrapper>> picker_map;
  // Also count the number of children in each state, to determine the
  // overall state.
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
        picker_map[child_name] = child->picker_wrapper();
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
  if (picker_map.size() > 0) {
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
  RoutePicker::RouteTable route_table;
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY:
      for (int i = 0; i < config_->route_table().size(); ++i) {
        RoutePicker::Route route;
        route.matcher = config_->route_table()[i].first;
        auto child_picker = picker_map.find(config_->route_table()[i].second);
        if (child_picker != picker_map.end()) {
          route.picker = child_picker->second;
        }
        route_table.push_back(std::move(route));
      }
      picker = absl::make_unique<RoutePicker>(std::move(route_table));
      break;
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:
      picker =
          absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker"));
      break;
    default:
      picker = absl::make_unique<TransientFailurePicker>(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "xds_routing: all children report state TRANSIENT_FAILURE"));
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
}

XdsRoutingLb::XdsRoutingChild::~XdsRoutingChild() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_routing_lb_trace)) {
    gpr_log(GPR_INFO,
            "[xds_routing_lb %p] XdsRoutingChild %p %s: destroying child",
            xds_routing_policy_.get(), this, name_.c_str());
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
  lb_policy_args.combiner = xds_routing_policy_->combiner();
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
    const XdsRoutingLbConfig::ChildConfig& config,
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
  update_args.config = config.config;
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
  // Set the child weight to 0 so that future picker won't contain this child.
  // Start a timer to delete the child.
  Ref(DEBUG_LOCATION, "XdsRoutingChild+timer").release();
  GRPC_CLOSURE_INIT(&on_delayed_removal_timer_, OnDelayedRemovalTimer, this,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(
      &delayed_removal_timer_,
      ExecCtx::Get()->Now() + xds_routing_policy_->child_retention_interval_ms_,
      &on_delayed_removal_timer_);
  delayed_removal_timer_callback_pending_ = true;
}

void XdsRoutingLb::XdsRoutingChild::OnDelayedRemovalTimer(void* arg,
                                                          grpc_error* error) {
  XdsRoutingChild* self = static_cast<XdsRoutingChild*>(arg);
  self->xds_routing_policy_->combiner()->Run(
      GRPC_CLOSURE_INIT(&self->on_delayed_removal_timer_,
                        OnDelayedRemovalTimerLocked, self, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsRoutingLb::XdsRoutingChild::OnDelayedRemovalTimerLocked(
    void* arg, grpc_error* error) {
  XdsRoutingChild* self = static_cast<XdsRoutingChild*>(arg);
  self->delayed_removal_timer_callback_pending_ = false;
  if (error == GRPC_ERROR_NONE && !self->shutdown_) {
    self->xds_routing_policy_->actions_.erase(self->name_);
  }
  self->Unref(DEBUG_LOCATION, "XdsRoutingChild+timer");
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
            "XdsRoutingChild::Helper::UpdateState child %s, state %d, piker %p",
            xds_routing_child_->name_.c_str(), state, picker.get());
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
    TraceSeverity severity, StringView message) {
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
    auto it = json.object_value().find("actions");
    if (it == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:actions error:required field not present"));
    } else if (it->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:actions error:type should be object"));
    } else {
      for (const auto& p : it->second.object_value()) {
        XdsRoutingLbConfig::ChildConfig child_config;
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
        }
      }
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
      for (const auto& route : it->second.array_value()) {
        // Parse methodName.
        XdsRoutingLbConfig::Matcher matcher;
        std::vector<grpc_error*> route_errors =
            ParseRouteConfig(route.object_value(), &matcher);
        if (!route_errors.empty()) {
          // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
          // string is not static in this case.
          grpc_error* error =
              GRPC_ERROR_CREATE_FROM_COPIED_STRING("field:routes error");
          for (grpc_error* route_error : route_errors) {
            error = grpc_error_add_child(error, route_error);
          }
          error_list.push_back(error);
        }
        // Parse action.
        std::string cluster_name;
        std::vector<grpc_error*> action_errors =
            ParseActionConfig(route.object_value(), &cluster_name);
        if (!action_errors.empty()) {
          // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
          // string is not static in this case.
          grpc_error* error =
              GRPC_ERROR_CREATE_FROM_COPIED_STRING("field:actions error:");
          for (grpc_error* action_error : action_errors) {
            error = grpc_error_add_child(error, action_error);
          }
          error_list.push_back(error);
        }
        route_table.emplace_back(std::move(matcher), std::move(cluster_name));
      }
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
      const Json& json, XdsRoutingLbConfig::ChildConfig* child_config) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    auto it = json.object_value().find("child_policy");
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
    } else {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("did not find childPolicy"));
    }
    return error_list;
  }

  static std::vector<grpc_error*> ParseRouteConfig(
      const Json& json, XdsRoutingLbConfig::Matcher* route_config) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    auto method_name = json.object_value().find("methodName");
    if (method_name == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:routes error:methodName is required"));
    } else if (method_name->second.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:routes error:methodName error: type should be object"));
    } else {
      auto service = method_name->second.object_value().find("service");
      auto method = method_name->second.object_value().find("method");
      if (service != method_name->second.object_value().end()) {
        route_config->service = service->second.string_value();
      } else {
        route_config->service = "";
      }
      if (method != method_name->second.object_value().end()) {
        route_config->method = method->second.string_value();
      } else {
        route_config->method = "";
      }
      if ((route_config->service == "") && (route_config->method != "")) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:methodName error: service is empty when method is "
            "not"));
      }
    }
    return error_list;
  }

  static std::vector<grpc_error*> ParseActionConfig(const Json& json,
                                                    std::string* cluster_name) {
    std::vector<grpc_error*> error_list;
    if (json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "value should be of type object"));
      return error_list;
    }
    auto action_name = json.object_value().find("action");
    if (action_name == json.object_value().end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:routes error:action is required"));
    } else if (action_name->second.type() != Json::Type::STRING) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:methodName error:type should be string"));
    } else {
      *cluster_name = action_name->second.string_value();
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
