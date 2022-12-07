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

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_override_host.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_xds_override_host_trace(false, "xds_override_host_lb");

namespace {

//
// xds_override_host LB policy
//

constexpr absl::string_view kXdsOverrideHost = "xds_override_host_experimental";

std::string MakeKeyForAddress(const ServerAddress& address) {
  // Use only the address, not the attributes.
  auto addr_str = grpc_sockaddr_to_string(&address.address(), false);
  return addr_str.ok() ? addr_str.value() : addr_str.status().ToString();
}

// Config for stateful session LB policy.
class XdsOverrideHostLbConfig : public LoadBalancingPolicy::Config {
 public:
  XdsOverrideHostLbConfig() = default;

  XdsOverrideHostLbConfig(const XdsOverrideHostLbConfig&) = delete;
  XdsOverrideHostLbConfig& operator=(const XdsOverrideHostLbConfig&) = delete;

  XdsOverrideHostLbConfig(XdsOverrideHostLbConfig&& other) = delete;
  XdsOverrideHostLbConfig& operator=(XdsOverrideHostLbConfig&& other) = delete;

  absl::string_view name() const override { return kXdsOverrideHost; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_config() const {
    return child_config_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors);

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_config_;
};

// xDS Cluster Impl LB policy.
class XdsOverrideHostLb : public LoadBalancingPolicy {
 public:
  explicit XdsOverrideHostLb(Args args);

  absl::string_view name() const override { return kXdsOverrideHost; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // A picker that wraps the picker from the child for cases when cookie is
  // present.
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<XdsOverrideHostLb> xds_override_host_lb,
           RefCountedPtr<SubchannelPicker> picker);

    PickResult Pick(PickArgs args) override;

   private:
    class SubchannelCallTracker;
    RefCountedPtr<XdsOverrideHostLb> policy_;
    RefCountedPtr<SubchannelPicker> picker_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<XdsOverrideHostLb> xds_override_host_policy)
        : xds_override_host_policy_(std::move(xds_override_host_policy)) {}

    ~Helper() override {
      xds_override_host_policy_.reset(DEBUG_LOCATION, "Helper");
    }

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const ChannelArgs& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    absl::string_view GetAuthority() override;
    grpc_event_engine::experimental::EventEngine* GetEventEngine() override;
    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override;

   private:
    RefCountedPtr<XdsOverrideHostLb> xds_override_host_policy_;
  };

  class SubchannelConnectivityStateWatcher
      : public SubchannelInterface::ConnectivityStateWatcherInterface {
   public:
    void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                   absl::Status status) override {}

    grpc_pollset_set* interested_parties() override { return nullptr; }
  };

  struct AddressMapEntry {
    RefCountedPtr<SubchannelInterface> subchannel;
    grpc_connectivity_state state;
  };

  ~XdsOverrideHostLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  void MaybeUpdatePickerLocked();

  void RegisterSubchannel(const ServerAddress& address,
                          RefCountedPtr<SubchannelInterface> subchannel);

  RefCountedPtr<SubchannelInterface> LookupSubchannelByAddress(
      absl::string_view address);

  // Current config from the resolver.
  RefCountedPtr<XdsOverrideHostLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  absl::Status status_;
  RefCountedPtr<SubchannelPicker> picker_;

  absl::Mutex subchannel_by_address_map_mu_;
  std::unordered_map<std::string, AddressMapEntry> subchannel_by_address_map_
      ABSL_GUARDED_BY(subchannel_by_address_map_mu_);
};

//
// XdsOverrideHostLb::Picker
//

XdsOverrideHostLb::Picker::Picker(
    RefCountedPtr<XdsOverrideHostLb> xds_override_host_lb,
    RefCountedPtr<SubchannelPicker> picker)
    : policy_(xds_override_host_lb), picker_(std::move(picker)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO, "[xds_override_host_lb %p] constructed new picker %p",
            xds_override_host_lb.get(), this);
  }
}

LoadBalancingPolicy::PickResult XdsOverrideHostLb::Picker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  std::string buffer;
  auto override_host =
      args.initial_metadata->Lookup(kOverrideHostHeaderName, &buffer);
  if (override_host.has_value()) {
    auto subchannel = policy_->LookupSubchannelByAddress(*override_host);
    if (subchannel != nullptr) {
      return PickResult::Complete{subchannel};
    }
  }
  if (picker_ == nullptr) {  // Should never happen.
    return PickResult::Fail(absl::InternalError(
        "xds_override_host picker not given any child picker"));
  }
  // Delegate to child picker
  return picker_->Pick(args);
}

//
// XdsOverrideHostLb
//

XdsOverrideHostLb::XdsOverrideHostLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO, "[xds_override_host_lb %p] created", this);
  }
}

XdsOverrideHostLb::~XdsOverrideHostLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO,
            "[xds_override_host_lb %p] destroying xds_override_host LB policy",
            this);
  }
}

void XdsOverrideHostLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO, "[xds_override_host_lb %p] shutting down", this);
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

void XdsOverrideHostLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void XdsOverrideHostLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

absl::Status XdsOverrideHostLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO, "[xds_override_host_lb %p] Received update", this);
  }
  auto old_config = std::move(config_);
  // Update config.
  config_ = std::move(args.config);
  if (config_ == nullptr) {
    return absl::InvalidArgumentError("Missing policy config");
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
  }
  if (args.addresses.ok()) {
    for (const auto& address : *args.addresses) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
        gpr_log(GPR_INFO, "%s", MakeKeyForAddress(address).c_str());
      }
    }
  } else {
    absl::MutexLock lock(&subchannel_by_address_map_mu_);
    subchannel_by_address_map_.clear();
  }
  // Update child policy.
  UpdateArgs update_args;
  update_args.addresses = std::move(args.addresses);
  update_args.resolution_note = std::move(args.resolution_note);
  update_args.config = config_->child_config();
  update_args.args = std::move(args.args);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO,
            "[xds_override_host_lb %p] Updating child policy handler %p", this,
            child_policy_.get());
  }
  auto status = child_policy_->UpdateLocked(std::move(update_args));
  if (status.ok()) {
  }
  return status;
}

void XdsOverrideHostLb::MaybeUpdatePickerLocked() {
  if (picker_ != nullptr) {
    auto xds_override_host_picker = MakeRefCounted<Picker>(Ref(), picker_);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
      gpr_log(GPR_INFO,
              "[xds_override_host_lb %p] updating connectivity: state=%s "
              "status=(%s) picker=%p",
              this, ConnectivityStateName(state_), status_.ToString().c_str(),
              xds_override_host_picker.get());
    }
    channel_control_helper()->UpdateState(state_, status_,
                                          std::move(xds_override_host_picker));
  }
}

OrphanablePtr<LoadBalancingPolicy> XdsOverrideHostLb::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_lb_xds_override_host_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO,
            "[xds_override_host_lb %p] Created new child policy handler %p",
            this, lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

RefCountedPtr<SubchannelInterface> XdsOverrideHostLb::LookupSubchannelByAddress(
    absl::string_view address) {
  absl::MutexLock lock(&subchannel_by_address_map_mu_);
  std::string key{address};
  auto subchannel_record = subchannel_by_address_map_.find(key);
  if (subchannel_record == subchannel_by_address_map_.end()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
      gpr_log(
          GPR_INFO,
          "[xds_override_host_lb %p] Subchannel for address %s was not found",
          this, key.c_str());
    }
    return nullptr;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO,
            "[xds_override_host_lb %p] Subchannel for address %s was found",
            this, key.c_str());
  }
  /*
  if override_host is set in pick arguments:
  entry = lb_policy.address_map[override_host]
  if entry found:
    idle_subchannel = None
    found_connecting = False
    if entry.subchannel is set AND
     entry.health_status is in policy_config.override_host_status:
      if entry.subchannel.connectivity_state == READY:
        return entry.subchannel as pick result
      elif entry.subchannel.connectivity_state == IDLE:
        idle_subchannel = entry.subchannel
      elif entry.subchannel.connectivity_state == CONNECTING:
        found_connecting = True
    // Java-only, for now: check equivalent addresses
    for address in entry.equivalent_addresses:
      other_entry = lb_policy.address_map[address]
      if other_entry.subchannel is set AND
       other_entry.health_status is in policy_config.override_host_status:
        if other_entry.subchannel.connectivity_state == READY:
          return other_entry.subchannel as pick result
        elif other_entry.subchannel.connectivity_state == IDLE:
          idle_subchannel = other_entry.subchannel
        elif other_entry.subchannel.connectivity_state == CONNECTING:
          found_connecting = True
    // No READY subchannel found.  If we found an IDLE subchannel,
    // trigger a connection attempt and queue the pick until that attempt
    // completes.
    if idle_subchannel is not None:
      hop into control plane to trigger connection attempt for idle_subchannel
      return queue as pick result
    // No READY or IDLE subchannels.  If we found a CONNECTING
    // subchannel, queue the pick and wait for the connection attempt
    // to complete.
    if found_connecting:
      return queue as pick result
  */
  return subchannel_record->second.subchannel;
}

void XdsOverrideHostLb::RegisterSubchannel(
    const ServerAddress& address,
    RefCountedPtr<SubchannelInterface> subchannel) {
  auto key = MakeKeyForAddress(address);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO,
            "[xds_override_host_lb %p] Added subchannel with address %s", this,
            key.c_str());
  }
  absl::MutexLock lock(&subchannel_by_address_map_mu_);
  subchannel_by_address_map_[key] = {subchannel, GRPC_CHANNEL_IDLE};
  subchannel->WatchConnectivityState(
      std::make_unique<SubchannelConnectivityStateWatcher>());
}

//
// XdsOverrideHostLb::Helper
//

RefCountedPtr<SubchannelInterface> XdsOverrideHostLb::Helper::CreateSubchannel(
    ServerAddress address, const ChannelArgs& args) {
  auto subchannel =
      xds_override_host_policy_->channel_control_helper()->CreateSubchannel(
          address, args);
  xds_override_host_policy_->RegisterSubchannel(address, subchannel);
  return subchannel;
}

void XdsOverrideHostLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  if (xds_override_host_policy_->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO,
            "[xds_override_host_lb %p] child connectivity state update: "
            "state=%s (%s) picker=%p",
            xds_override_host_policy_.get(), ConnectivityStateName(state),
            status.ToString().c_str(), picker.get());
  }
  // Save the state and picker.
  xds_override_host_policy_->state_ = state;
  xds_override_host_policy_->status_ = status;
  xds_override_host_policy_->picker_ = std::move(picker);
  // Wrap the picker and return it to the channel.
  xds_override_host_policy_->MaybeUpdatePickerLocked();
}

void XdsOverrideHostLb::Helper::RequestReresolution() {
  if (xds_override_host_policy_->shutting_down_) return;
  xds_override_host_policy_->channel_control_helper()->RequestReresolution();
}

absl::string_view XdsOverrideHostLb::Helper::GetAuthority() {
  return xds_override_host_policy_->channel_control_helper()->GetAuthority();
}

grpc_event_engine::experimental::EventEngine*
XdsOverrideHostLb::Helper::GetEventEngine() {
  return xds_override_host_policy_->channel_control_helper()->GetEventEngine();
}

void XdsOverrideHostLb::Helper::AddTraceEvent(TraceSeverity severity,
                                              absl::string_view message) {
  if (xds_override_host_policy_->shutting_down_) return;
  xds_override_host_policy_->channel_control_helper()->AddTraceEvent(severity,
                                                                     message);
}

//
// factory
//
const JsonLoaderInterface* XdsOverrideHostLbConfig::JsonLoader(
    const JsonArgs&) {
  static const auto kJsonLoader =
      JsonObjectLoader<XdsOverrideHostLbConfig>()
          // Child policy config is parsed in JsonPostLoad
          .Finish();
  return kJsonLoader;
}

void XdsOverrideHostLbConfig::JsonPostLoad(const Json& json, const JsonArgs&,
                                           ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".childPolicy");
  auto it = json.object_value().find("childPolicy");
  if (it == json.object_value().end()) {
    errors->AddError("field not present");
  } else {
    auto child_policy_config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            it->second);
    if (!child_policy_config.ok()) {
      errors->AddError(child_policy_config.status().message());
    } else {
      child_config_ = std::move(*child_policy_config);
    }
  }
}

class XdsOverrideHostLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<XdsOverrideHostLb>(std::move(args));
  }

  absl::string_view name() const override { return kXdsOverrideHost; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    if (json.type() == Json::Type::JSON_NULL) {
      // This policy was configured in the deprecated loadBalancingPolicy
      // field or in the client API.
      return absl::InvalidArgumentError(
          "field:loadBalancingPolicy error:xds_override_host policy requires "
          "configuration. Please use loadBalancingConfig field of service "
          "config instead.");
    }
    return LoadRefCountedFromJson<XdsOverrideHostLbConfig>(
        json, JsonArgs(),
        "errors validating xds_override_host LB policy config");
  }
};

}  // namespace

void RegisterXdsOverrideHostLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<XdsOverrideHostLbFactory>());
}

}  // namespace grpc_core
