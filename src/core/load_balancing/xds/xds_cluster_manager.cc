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

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/child_policy_handler.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/xds/xds_resolver_attributes.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"

namespace grpc_core {

namespace {

using ::grpc_event_engine::experimental::EventEngine;

constexpr Duration kChildRetentionInterval = Duration::Minutes(15);
constexpr absl::string_view kXdsClusterManager =
    "xds_cluster_manager_experimental";

// Config for xds_cluster_manager LB policy.
class XdsClusterManagerLbConfig final : public LoadBalancingPolicy::Config {
 public:
  struct Child {
    RefCountedPtr<LoadBalancingPolicy::Config> config;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
    void JsonPostLoad(const Json& json, const JsonArgs&,
                      ValidationErrors* errors);
  };

  XdsClusterManagerLbConfig() = default;

  XdsClusterManagerLbConfig(const XdsClusterManagerLbConfig&) = delete;
  XdsClusterManagerLbConfig& operator=(const XdsClusterManagerLbConfig&) =
      delete;

  XdsClusterManagerLbConfig(XdsClusterManagerLbConfig&& other) = delete;
  XdsClusterManagerLbConfig& operator=(XdsClusterManagerLbConfig&& other) =
      delete;

  absl::string_view name() const override { return kXdsClusterManager; }

  const std::map<std::string, Child>& cluster_map() const {
    return cluster_map_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);

 private:
  std::map<std::string, Child> cluster_map_;
};

// xds_cluster_manager LB policy.
class XdsClusterManagerLb final : public LoadBalancingPolicy {
 public:
  explicit XdsClusterManagerLb(Args args);

  absl::string_view name() const override { return kXdsClusterManager; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // Picks a child using prefix or path matching and then delegates to that
  // child's picker.
  class ClusterPicker final : public SubchannelPicker {
   public:
    // Maintains a map of cluster names to pickers.
    using ClusterMap = std::map<std::string /*cluster_name*/,
                                RefCountedPtr<SubchannelPicker>, std::less<>>;

    // It is required that the keys of cluster_map have to live at least as long
    // as the ClusterPicker instance.
    explicit ClusterPicker(ClusterMap cluster_map)
        : cluster_map_(std::move(cluster_map)) {}

    PickResult Pick(PickArgs args) override;

   private:
    ClusterMap cluster_map_;
  };

  // Each ClusterChild holds a ref to its parent XdsClusterManagerLb.
  class ClusterChild final : public InternallyRefCounted<ClusterChild> {
   public:
    ClusterChild(RefCountedPtr<XdsClusterManagerLb> xds_cluster_manager_policy,
                 const std::string& name);
    ~ClusterChild() override;

    void Orphan() override;

    absl::Status UpdateLocked(
        RefCountedPtr<LoadBalancingPolicy::Config> config,
        const absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>>&
            addresses,
        const ChannelArgs& args);
    void ExitIdleLocked();
    void ResetBackoffLocked();
    void DeactivateLocked();

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }
    RefCountedPtr<SubchannelPicker> picker() const { return picker_; }

   private:
    class Helper final : public DelegatingChannelControlHelper {
     public:
      explicit Helper(RefCountedPtr<ClusterChild> xds_cluster_manager_child)
          : xds_cluster_manager_child_(std::move(xds_cluster_manager_child)) {}

      ~Helper() override {
        xds_cluster_manager_child_.reset(DEBUG_LOCATION, "Helper");
      }

      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       RefCountedPtr<SubchannelPicker> picker) override;

     private:
      ChannelControlHelper* parent_helper() const override {
        return xds_cluster_manager_child_->xds_cluster_manager_policy_
            ->channel_control_helper();
      }

      RefCountedPtr<ClusterChild> xds_cluster_manager_child_;
    };

    // Methods for dealing with the child policy.
    OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
        const ChannelArgs& args);

    void OnDelayedRemovalTimerLocked();

    // The owning LB policy.
    RefCountedPtr<XdsClusterManagerLb> xds_cluster_manager_policy_;

    // Points to the corresponding key in children map.
    const std::string name_;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    RefCountedPtr<SubchannelPicker> picker_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_CONNECTING;

    // States for delayed removal.
    absl::optional<EventEngine::TaskHandle> delayed_removal_timer_handle_;
    bool shutdown_ = false;
  };

  ~XdsClusterManagerLb() override;

  void ShutdownLocked() override;

  void UpdateStateLocked();

  // Current config from the resolver.
  RefCountedPtr<XdsClusterManagerLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;
  bool update_in_progress_ = false;

  // Children.
  std::map<std::string, OrphanablePtr<ClusterChild>> children_;
};

//
// XdsClusterManagerLb::ClusterPicker
//

XdsClusterManagerLb::PickResult XdsClusterManagerLb::ClusterPicker::Pick(
    PickArgs args) {
  auto* call_state = static_cast<ClientChannelLbCallState*>(args.call_state);
  auto* cluster_name_attribute =
      call_state->GetCallAttribute<XdsClusterAttribute>();
  absl::string_view cluster_name;
  if (cluster_name_attribute != nullptr) {
    cluster_name = cluster_name_attribute->cluster();
  }
  auto it = cluster_map_.find(cluster_name);
  if (it != cluster_map_.end()) {
    return it->second->Pick(args);
  }
  return PickResult::Fail(absl::InternalError(absl::StrCat(
      "xds cluster manager picker: unknown cluster \"", cluster_name, "\"")));
}

//
// XdsClusterManagerLb
//

XdsClusterManagerLb::XdsClusterManagerLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {}

XdsClusterManagerLb::~XdsClusterManagerLb() {
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << this
      << "] destroying xds_cluster_manager LB policy";
}

void XdsClusterManagerLb::ShutdownLocked() {
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << this << "] shutting down";
  shutting_down_ = true;
  children_.clear();
}

void XdsClusterManagerLb::ExitIdleLocked() {
  for (auto& p : children_) p.second->ExitIdleLocked();
}

void XdsClusterManagerLb::ResetBackoffLocked() {
  for (auto& p : children_) p.second->ResetBackoffLocked();
}

absl::Status XdsClusterManagerLb::UpdateLocked(UpdateArgs args) {
  if (shutting_down_) return absl::OkStatus();
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << this << "] Received update";
  update_in_progress_ = true;
  // Update config.
  config_ = args.config.TakeAsSubclass<XdsClusterManagerLbConfig>();
  // Deactivate the children not in the new config.
  for (const auto& p : children_) {
    const std::string& name = p.first;
    ClusterChild* child = p.second.get();
    if (config_->cluster_map().find(name) == config_->cluster_map().end()) {
      child->DeactivateLocked();
    }
  }
  // Add or update the children in the new config.
  std::vector<std::string> errors;
  for (const auto& p : config_->cluster_map()) {
    const std::string& name = p.first;
    const RefCountedPtr<LoadBalancingPolicy::Config>& config = p.second.config;
    auto& child = children_[name];
    if (child == nullptr) {
      child = MakeOrphanable<ClusterChild>(
          RefAsSubclass<XdsClusterManagerLb>(DEBUG_LOCATION, "ClusterChild"),
          name);
    }
    absl::Status status =
        child->UpdateLocked(config, args.addresses, args.args);
    if (!status.ok()) {
      errors.emplace_back(
          absl::StrCat("child ", name, ": ", status.ToString()));
    }
  }
  update_in_progress_ = false;
  UpdateStateLocked();
  // Return status.
  if (!errors.empty()) {
    return absl::UnavailableError(absl::StrCat(
        "errors from children: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

void XdsClusterManagerLb::UpdateStateLocked() {
  // If we're in the process of propagating an update from our parent to
  // our children, ignore any updates that come from the children.  We
  // will instead return a new picker once the update has been seen by
  // all children.  This avoids unnecessary picker churn while an update
  // is being propagated to our children.
  if (update_in_progress_) return;
  // Also count the number of children in each state, to determine the
  // overall state.
  size_t num_ready = 0;
  size_t num_connecting = 0;
  size_t num_idle = 0;
  for (const auto& p : children_) {
    const auto& child_name = p.first;
    const ClusterChild* child = p.second.get();
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
        break;
      }
      default:
        GPR_UNREACHABLE_CODE(return);
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
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << this << "] connectivity changed to "
      << ConnectivityStateName(connectivity_state);
  ClusterPicker::ClusterMap cluster_map;
  for (const auto& p : config_->cluster_map()) {
    const std::string& cluster_name = p.first;
    RefCountedPtr<SubchannelPicker>& child_picker = cluster_map[cluster_name];
    child_picker = children_[cluster_name]->picker();
    if (child_picker == nullptr) {
      GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
          << "[xds_cluster_manager_lb " << this << "] child " << cluster_name
          << " has not yet returned a picker; creating a QueuePicker.";
      child_picker =
          MakeRefCounted<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker"));
    }
  }
  auto picker = MakeRefCounted<ClusterPicker>(std::move(cluster_map));
  absl::Status status;
  if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    status = absl::Status(absl::StatusCode::kUnavailable,
                          "TRANSIENT_FAILURE from XdsClusterManagerLb");
  }
  channel_control_helper()->UpdateState(connectivity_state, status,
                                        std::move(picker));
}

//
// XdsClusterManagerLb::ClusterChild
//

XdsClusterManagerLb::ClusterChild::ClusterChild(
    RefCountedPtr<XdsClusterManagerLb> xds_cluster_manager_policy,
    const std::string& name)
    : xds_cluster_manager_policy_(std::move(xds_cluster_manager_policy)),
      name_(name),
      picker_(MakeRefCounted<QueuePicker>(nullptr)) {
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << xds_cluster_manager_policy_.get()
      << "] created ClusterChild " << this << " for " << name_;
}

XdsClusterManagerLb::ClusterChild::~ClusterChild() {
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << xds_cluster_manager_policy_.get()
      << "] ClusterChild " << this << ": destroying child";
  xds_cluster_manager_policy_.reset(DEBUG_LOCATION, "ClusterChild");
}

void XdsClusterManagerLb::ClusterChild::Orphan() {
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << xds_cluster_manager_policy_.get()
      << "] ClusterChild " << this << " " << name_ << ": shutting down child";
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  grpc_pollset_set_del_pollset_set(
      child_policy_->interested_parties(),
      xds_cluster_manager_policy_->interested_parties());
  child_policy_.reset();
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_.reset();
  if (delayed_removal_timer_handle_.has_value()) {
    xds_cluster_manager_policy_->channel_control_helper()
        ->GetEventEngine()
        ->Cancel(*delayed_removal_timer_handle_);
  }
  shutdown_ = true;
  Unref();
}

OrphanablePtr<LoadBalancingPolicy>
XdsClusterManagerLb::ClusterChild::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer =
      xds_cluster_manager_policy_->work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(this->Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &xds_cluster_manager_lb_trace);
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << xds_cluster_manager_policy_.get()
      << "] ClusterChild " << this << " " << name_
      << ": Created new child policy handler " << lb_policy.get();
  // Add the xDS's interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // xDS LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(
      lb_policy->interested_parties(),
      xds_cluster_manager_policy_->interested_parties());
  return lb_policy;
}

absl::Status XdsClusterManagerLb::ClusterChild::UpdateLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> config,
    const absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>>& addresses,
    const ChannelArgs& args) {
  if (xds_cluster_manager_policy_->shutting_down_) return absl::OkStatus();
  // Update child weight.
  // Reactivate if needed.
  if (delayed_removal_timer_handle_.has_value() &&
      xds_cluster_manager_policy_->channel_control_helper()
          ->GetEventEngine()
          ->Cancel(*delayed_removal_timer_handle_)) {
    delayed_removal_timer_handle_.reset();
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args);
  }
  // Construct update args.
  UpdateArgs update_args;
  update_args.config = std::move(config);
  update_args.addresses = addresses;
  update_args.args = args;
  // Update the policy.
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb " << xds_cluster_manager_policy_.get()
      << "] ClusterChild " << this << " " << name_
      << ": Updating child policy handler " << child_policy_.get();
  return child_policy_->UpdateLocked(std::move(update_args));
}

void XdsClusterManagerLb::ClusterChild::ExitIdleLocked() {
  child_policy_->ExitIdleLocked();
}

void XdsClusterManagerLb::ClusterChild::ResetBackoffLocked() {
  child_policy_->ResetBackoffLocked();
}

void XdsClusterManagerLb::ClusterChild::DeactivateLocked() {
  // If already deactivated, don't do that again.
  if (delayed_removal_timer_handle_.has_value()) return;
  // Start a timer to delete the child.
  delayed_removal_timer_handle_ =
      xds_cluster_manager_policy_->channel_control_helper()
          ->GetEventEngine()
          ->RunAfter(
              kChildRetentionInterval,
              [self = Ref(DEBUG_LOCATION, "ClusterChild+timer")]() mutable {
                ApplicationCallbackExecCtx application_exec_ctx;
                ExecCtx exec_ctx;
                auto* self_ptr = self.get();  // Avoid use-after-move problem.
                self_ptr->xds_cluster_manager_policy_->work_serializer()->Run(
                    [self = std::move(self)]() {
                      self->OnDelayedRemovalTimerLocked();
                    },
                    DEBUG_LOCATION);
              });
}

void XdsClusterManagerLb::ClusterChild::OnDelayedRemovalTimerLocked() {
  delayed_removal_timer_handle_.reset();
  if (!shutdown_) {
    xds_cluster_manager_policy_->children_.erase(name_);
  }
}

//
// XdsClusterManagerLb::ClusterChild::Helper
//

void XdsClusterManagerLb::ClusterChild::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  GRPC_TRACE_LOG(xds_cluster_manager_lb, INFO)
      << "[xds_cluster_manager_lb "
      << xds_cluster_manager_child_->xds_cluster_manager_policy_.get()
      << "] child " << xds_cluster_manager_child_->name_
      << ": received update: state=" << ConnectivityStateName(state) << " ("
      << status << ") picker=" << picker.get();
  if (xds_cluster_manager_child_->xds_cluster_manager_policy_->shutting_down_) {
    return;
  }
  // Cache the picker in the ClusterChild.
  xds_cluster_manager_child_->picker_ = std::move(picker);
  // Decide what state to report for aggregation purposes.
  // If the last recorded state was TRANSIENT_FAILURE and the new state
  // is something other than READY, don't change the state.
  if (xds_cluster_manager_child_->connectivity_state_ !=
          GRPC_CHANNEL_TRANSIENT_FAILURE ||
      state == GRPC_CHANNEL_READY) {
    xds_cluster_manager_child_->connectivity_state_ = state;
  }
  // Notify the LB policy.
  xds_cluster_manager_child_->xds_cluster_manager_policy_->UpdateStateLocked();
}

//
// factory
//

const JsonLoaderInterface* XdsClusterManagerLbConfig::Child::JsonLoader(
    const JsonArgs&) {
  // Note: The "childPolicy" field requires custom processing, so
  // it's handled in JsonPostLoad() instead.
  static const auto* loader = JsonObjectLoader<Child>().Finish();
  return loader;
}

void XdsClusterManagerLbConfig::Child::JsonPostLoad(const Json& json,
                                                    const JsonArgs&,
                                                    ValidationErrors* errors) {
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

const JsonLoaderInterface* XdsClusterManagerLbConfig::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<XdsClusterManagerLbConfig>()
          .Field("children", &XdsClusterManagerLbConfig::cluster_map_)
          .Finish();
  return loader;
}

class XdsClusterManagerLbFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<XdsClusterManagerLb>(std::move(args));
  }

  absl::string_view name() const override { return kXdsClusterManager; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<XdsClusterManagerLbConfig>>(
        json, JsonArgs(),
        "errors validating xds_cluster_manager LB policy config");
  }
};

}  // namespace

void RegisterXdsClusterManagerLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<XdsClusterManagerLbFactory>());
}

}  // namespace grpc_core
