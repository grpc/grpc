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

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_call_state_internal.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {
TraceFlag grpc_lb_xds_override_host_trace(false, "xds_override_host_lb");

namespace {

//
// xds_override_host LB policy
//
class XdsOverrideHostLb : public LoadBalancingPolicy {
 public:
  explicit XdsOverrideHostLb(Args args);

  absl::string_view name() const override {
    return XdsOverrideHostLbConfig::Name();
  }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class SubchannelWrapper : public DelegatingSubchannel {
   public:
    SubchannelWrapper(RefCountedPtr<SubchannelInterface> subchannel,
                      RefCountedPtr<XdsOverrideHostLb> policy,
                      absl::optional<const std::string> key);

    ~SubchannelWrapper() override;

    void WatchConnectivityState(
        std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override;

    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface* watcher) override;

    grpc_connectivity_state connectivity_state() {
      return connectivity_state_.load();
    }

    XdsOverrideHostLb* policy() { return policy_.get(); }

   private:
    class ConnectivityStateWatcher : public ConnectivityStateWatcherInterface {
     public:
      ConnectivityStateWatcher(
          std::unique_ptr<ConnectivityStateWatcherInterface> delegate,
          RefCountedPtr<SubchannelWrapper> subchannel)
          : delegate_(std::move(delegate)), subchannel_(subchannel) {}

      void OnConnectivityStateChange(grpc_connectivity_state state,
                                     absl::Status status) override {
        delegate_->OnConnectivityStateChange(state, status);
        subchannel_->connectivity_state_ = state;
      }

      grpc_pollset_set* interested_parties() override {
        return delegate_->interested_parties();
      }

     private:
      std::unique_ptr<ConnectivityStateWatcherInterface> delegate_;
      RefCountedPtr<SubchannelWrapper> subchannel_;
    };

    const absl::optional<const std::string> key_;
    RefCountedPtr<XdsOverrideHostLb> policy_;
    std::atomic<grpc_connectivity_state> connectivity_state_{GRPC_CHANNEL_IDLE};
    std::map<ConnectivityStateWatcherInterface*, ConnectivityStateWatcher*>
        watchers_;
  };

  // A picker that wraps the picker from the child for cases when cookie is
  // present.
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<XdsOverrideHostLb> xds_override_host_lb,
           RefCountedPtr<SubchannelPicker> picker);

    PickResult Pick(PickArgs args) override;

   private:
    class SubchannelConnectionRequester {
     public:
      explicit SubchannelConnectionRequester(
          RefCountedPtr<SubchannelWrapper> subchannel)
          : subchannel_(std::move(subchannel)) {
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
        // Hop into ExecCtx, so that we're not holding the data plane mutex
        // while we run control-plane code.
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

     private:
      XdsOverrideHostLb* policy() { return subchannel_->policy(); }

      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<SubchannelConnectionRequester*>(arg);
        self->policy()->work_serializer()->Run(
            [self]() {
              self->subchannel_->RequestConnection();
              delete self;
            },
            DEBUG_LOCATION);
      }

      RefCountedPtr<SubchannelWrapper> subchannel_;
      grpc_closure closure_;
    };

    absl::optional<LoadBalancingPolicy::PickResult> PickOverridenHost(
        absl::string_view override_host);

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

  class SubchannelEntry {
   public:
    void SetSubchannel(SubchannelWrapper* subchannel) {
      subchannel_ = subchannel;
    }

    void ResetSubchannel(SubchannelWrapper* expected) {
      if (subchannel_ == expected) {
        subchannel_ = nullptr;
      }
    }

    RefCountedPtr<SubchannelWrapper> GetSubchannel() {
      if (subchannel_ == nullptr) {
        return nullptr;
      }
      return subchannel_->Ref();
    }

   private:
    SubchannelWrapper* subchannel_ = nullptr;
  };

  ~XdsOverrideHostLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  void MaybeUpdatePickerLocked();

  RefCountedPtr<SubchannelWrapper> LookupSubchannel(absl::string_view address);

  void UpdateAddressMap(const absl::StatusOr<ServerAddressList>& addresses);

  RefCountedPtr<SubchannelWrapper> AdoptSubchannel(
      ServerAddress address, RefCountedPtr<SubchannelInterface> subchannel);

  void ResetSubchannel(absl::string_view key, SubchannelWrapper* subchannel);

  RefCountedPtr<SubchannelWrapper> GetSubchannelByAddress(
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
  Mutex subchannel_map_mu_;
  std::map<std::string, SubchannelEntry, std::less<>> subchannel_map_
      ABSL_GUARDED_BY(subchannel_map_mu_);
};

//
// XdsOverrideHostLb::Picker
//

XdsOverrideHostLb::Picker::Picker(
    RefCountedPtr<XdsOverrideHostLb> xds_override_host_lb,
    RefCountedPtr<SubchannelPicker> picker)
    : policy_(std::move(xds_override_host_lb)), picker_(std::move(picker)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
    gpr_log(GPR_INFO, "[xds_override_host_lb %p] constructed new picker %p",
            policy_.get(), this);
  }
}

absl::optional<LoadBalancingPolicy::PickResult>
XdsOverrideHostLb::Picker::PickOverridenHost(absl::string_view override_host) {
  if (override_host.length() == 0) {
    return absl::nullopt;
  }
  auto subchannel = policy_->GetSubchannelByAddress(override_host);
  if (subchannel == nullptr) {
    return absl::nullopt;
  }
  auto connectivity_state = subchannel->connectivity_state();
  if (connectivity_state == GRPC_CHANNEL_READY) {
    return PickResult::Complete(subchannel->wrapped_subchannel());
  } else if (connectivity_state == GRPC_CHANNEL_CONNECTING) {
    return PickResult::Queue();
  } else if (connectivity_state == GRPC_CHANNEL_IDLE) {
    // Deleted after the connection is requested
    new SubchannelConnectionRequester(std::move(subchannel));
    return PickResult::Queue();
  }
  return absl::nullopt;
}

LoadBalancingPolicy::PickResult XdsOverrideHostLb::Picker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  auto* call_state = static_cast<LbCallStateInternal*>(args.call_state);
  auto override_host = call_state->GetCallAttribute(XdsOverrideHostTypeName());
  auto overridden_host_pick = PickOverridenHost(override_host);
  if (overridden_host_pick.has_value()) {
    return std::move(*overridden_host_pick);
  }
  if (picker_ == nullptr) {  // Should never happen.
    return PickResult::Fail(absl::InternalError(
        "xds_override_host picker not given any child picker"));
  }
  auto result = picker_->Pick(args);
  auto complete_pick = absl::get_if<PickResult::Complete>(&result.result);
  if (complete_pick != nullptr) {
    complete_pick->subchannel =
        static_cast<SubchannelWrapper*>(complete_pick->subchannel.get())
            ->wrapped_subchannel();
  }
  return result;
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
  UpdateAddressMap(args.addresses);
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
  return child_policy_->UpdateLocked(std::move(update_args));
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

void XdsOverrideHostLb::UpdateAddressMap(
    const absl::StatusOr<ServerAddressList>& addresses) {
  std::unordered_set<std::string> keys(addresses->size());
  if (addresses.ok()) {
    for (const auto& address : *addresses) {
      auto key = grpc_sockaddr_to_string(&address.address(), false);
      if (key.ok()) {
        keys.insert(std::move(*key));
      }
    }
  }
  MutexLock lock(&subchannel_map_mu_);
  for (auto it = subchannel_map_.begin(); it != subchannel_map_.end();) {
    if (keys.find(it->first) == keys.end()) {
      it = subchannel_map_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& key : keys) {
    if (subchannel_map_.find(key) == subchannel_map_.end()) {
      subchannel_map_.emplace(key, SubchannelEntry());
    }
  }
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::AdoptSubchannel(
    ServerAddress address, RefCountedPtr<SubchannelInterface> subchannel) {
  auto subchannel_key = grpc_sockaddr_to_string(&address.address(), false);
  absl::optional<std::string> key;
  if (subchannel_key.ok()) {
    key = std::move(*subchannel_key);
  }
  auto wrapper =
      MakeRefCounted<SubchannelWrapper>(std::move(subchannel), Ref(), key);
  if (key.has_value()) {
    MutexLock lock(&subchannel_map_mu_);
    auto it = subchannel_map_.find(*key);
    if (it != subchannel_map_.end()) {
      it->second.SetSubchannel(wrapper.get());
    }
  }
  return wrapper;
}

void XdsOverrideHostLb::ResetSubchannel(absl::string_view key,
                                        SubchannelWrapper* subchannel) {
  MutexLock lock(&subchannel_map_mu_);
  auto it = subchannel_map_.find(key);
  if (it != subchannel_map_.end()) {
    it->second.ResetSubchannel(subchannel);
  }
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::GetSubchannelByAddress(absl::string_view address) {
  MutexLock lock(&subchannel_map_mu_);
  auto it = subchannel_map_.find(address);
  if (it != subchannel_map_.end()) {
    return it->second.GetSubchannel();
  }
  return nullptr;
}

//
// XdsOverrideHostLb::Helper
//

RefCountedPtr<SubchannelInterface> XdsOverrideHostLb::Helper::CreateSubchannel(
    ServerAddress address, const ChannelArgs& args) {
  auto subchannel =
      xds_override_host_policy_->channel_control_helper()->CreateSubchannel(
          address, args);
  return xds_override_host_policy_->AdoptSubchannel(address, subchannel);
}

void XdsOverrideHostLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  if (xds_override_host_policy_->shutting_down_) return;
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
// XdsOverrideHostLb::SubchannelWrapper::SubchannelWrapper
//

XdsOverrideHostLb::SubchannelWrapper::SubchannelWrapper(
    RefCountedPtr<SubchannelInterface> subchannel,
    RefCountedPtr<XdsOverrideHostLb> policy,
    absl::optional<const std::string> key)
    : DelegatingSubchannel(std::move(subchannel)),
      key_(std::move(key)),
      policy_(std::move(policy)) {}

XdsOverrideHostLb::SubchannelWrapper::~SubchannelWrapper() {
  if (key_.has_value()) {
    policy_->ResetSubchannel(*key_, this);
  }
}

void XdsOverrideHostLb::SubchannelWrapper::WatchConnectivityState(
    std::unique_ptr<ConnectivityStateWatcherInterface> watcher) {
  auto watcher_id = watcher.get();
  auto wrapper =
      std::make_unique<ConnectivityStateWatcher>(std::move(watcher), Ref());
  watchers_.emplace(watcher_id, wrapper.get());
  wrapped_subchannel()->WatchConnectivityState(std::move(wrapper));
}

void XdsOverrideHostLb::SubchannelWrapper::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  auto original_watcher = watchers_.find(watcher);
  if (original_watcher != watchers_.end()) {
    wrapped_subchannel()->CancelConnectivityStateWatch(
        original_watcher->second);
    watchers_.erase(original_watcher);
  }
}

//
// factory
//
class XdsOverrideHostLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<XdsOverrideHostLb>(std::move(args));
  }

  absl::string_view name() const override {
    return XdsOverrideHostLbConfig::Name();
  }

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

// XdsOverrideHostLbConfig

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

}  // namespace grpc_core
