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

#include <stddef.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
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
#include "src/core/lib/load_balancing/delegating_helper.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {
TraceFlag grpc_lb_xds_override_host_trace(false, "xds_override_host_lb");

namespace {
template <typename Value>
struct PtrLessThan {
  using is_transparent = void;

  bool operator()(const std::unique_ptr<Value>& v1,
                  const std::unique_ptr<Value>& v2) const {
    return v1 < v2;
  }
  bool operator()(const Value* v1, const Value* v2) const { return v1 < v2; }
  bool operator()(const Value* v1, const std::unique_ptr<Value>& v2) const {
    return v1 < v2.get();
  }
  bool operator()(const std::unique_ptr<Value>& v1, const Value* v2) const {
    return v1.get() < v2;
  }
};

XdsHealthStatus GetAddressHealthStatus(const ServerAddress& address) {
  auto attribute = address.GetAttribute(XdsEndpointHealthStatusAttribute::kKey);
  if (attribute == nullptr) {
    return XdsHealthStatus(XdsHealthStatus::HealthStatus::kUnknown);
  }
  return static_cast<const XdsEndpointHealthStatusAttribute*>(attribute)
      ->status();
}

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
                      absl::string_view key);

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
      explicit ConnectivityStateWatcher(
          WeakRefCountedPtr<SubchannelWrapper> subchannel)
          : subchannel_(std::move(subchannel)) {}

      void OnConnectivityStateChange(grpc_connectivity_state state,
                                     absl::Status status) override;

      grpc_pollset_set* interested_parties() override;

     private:
      WeakRefCountedPtr<SubchannelWrapper> subchannel_;
    };

    void Orphan() override;

    void UpdateConnectivityState(grpc_connectivity_state state,
                                 absl::Status status);

    ConnectivityStateWatcher* watcher_;
    absl::optional<std::string> key_;
    RefCountedPtr<XdsOverrideHostLb> policy_;
    std::set<std::unique_ptr<ConnectivityStateWatcherInterface>,
             PtrLessThan<ConnectivityStateWatcherInterface>>
        watchers_;
    std::atomic<grpc_connectivity_state> connectivity_state_ = {
        GRPC_CHANNEL_IDLE};
  };

  // A picker that wraps the picker from the child for cases when cookie is
  // present.
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<XdsOverrideHostLb> xds_override_host_lb,
           RefCountedPtr<SubchannelPicker> picker,
           XdsHealthStatusSet override_host_health_status_set);

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
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<SubchannelConnectionRequester*>(arg);
        self->subchannel_->policy()->work_serializer()->Run(
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
    XdsHealthStatusSet override_host_health_status_set_;
  };

  class Helper
      : public ParentOwningDelegatingChannelControlHelper<XdsOverrideHostLb> {
   public:
    explicit Helper(RefCountedPtr<XdsOverrideHostLb> xds_override_host_policy)
        : ParentOwningDelegatingChannelControlHelper(
              std::move(xds_override_host_policy)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const ChannelArgs& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override;
  };

  class SubchannelEntry {
   public:
    explicit SubchannelEntry(XdsHealthStatus eds_health_status)
        : eds_health_status_(eds_health_status) {}

    void SetSubchannel(SubchannelWrapper* subchannel) {
      if (eds_health_status_.status() == XdsHealthStatus::kDraining) {
        subchannel_ = subchannel->Ref();
      } else {
        subchannel_ = subchannel;
      }
    }

    void UnsetSubchannel() { subchannel_ = nullptr; }

    SubchannelWrapper* GetSubchannel() const {
      return Match(
          subchannel_,
          [](XdsOverrideHostLb::SubchannelWrapper* subchannel) {
            return subchannel;
          },
          [](RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper> subchannel) {
            return subchannel.get();
          });
    }

    void SetEdsHealthStatus(XdsHealthStatus eds_health_status) {
      eds_health_status_ = eds_health_status;
      auto subchannel = GetSubchannel();
      if (subchannel == nullptr) {
        return;
      }
      if (eds_health_status_.status() == XdsHealthStatus::kDraining) {
        subchannel_ = subchannel->Ref();
      } else {
        subchannel_ = subchannel;
      }
    }

    XdsHealthStatus eds_health_status() const { return eds_health_status_; }

   private:
    absl::variant<SubchannelWrapper*, RefCountedPtr<SubchannelWrapper>>
        subchannel_;
    XdsHealthStatus eds_health_status_;
  };

  ~XdsOverrideHostLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  void MaybeUpdatePickerLocked();

  absl::StatusOr<ServerAddressList> UpdateAddressMap(
      absl::StatusOr<ServerAddressList> addresses);

  RefCountedPtr<SubchannelWrapper> AdoptSubchannel(
      ServerAddress address, RefCountedPtr<SubchannelInterface> subchannel);

  void UnsetSubchannel(absl::string_view key, SubchannelWrapper* subchannel);

  RefCountedPtr<SubchannelWrapper> GetSubchannelByAddress(
      absl::string_view address, XdsHealthStatusSet overriden_health_statuses);

  void OnSubchannelConnectivityStateChange(absl::string_view subchannel_key)
      ABSL_NO_THREAD_SAFETY_ANALYSIS;  // Called from within the worker
                                       // serializer and does not require
                                       // additional synchronization

  // Current config from the resolver.
  RefCountedPtr<XdsOverrideHostLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_CONNECTING;
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
    RefCountedPtr<SubchannelPicker> picker,
    XdsHealthStatusSet override_host_health_status_set)
    : policy_(std::move(xds_override_host_lb)),
      picker_(std::move(picker)),
      override_host_health_status_set_(override_host_health_status_set) {
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
  auto subchannel = policy_->GetSubchannelByAddress(
      override_host, override_host_health_status_set_);
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
  auto* call_state = static_cast<ClientChannelLbCallState*>(args.call_state);
  auto* override_host = static_cast<XdsOverrideHostAttribute*>(
      call_state->GetCallAttribute(XdsOverrideHostAttribute::TypeName()));
  auto overridden_host_pick =
      PickOverridenHost(override_host != nullptr ? override_host->host_name()
                                                 : absl::string_view());
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
  {
    MutexLock lock(&subchannel_map_mu_);
    subchannel_map_.clear();
  }
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
  // Update child policy.
  UpdateArgs update_args;
  update_args.addresses = UpdateAddressMap(std::move(args.addresses));
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
    auto xds_override_host_picker = MakeRefCounted<Picker>(
        Ref(), picker_, config_->override_host_status_set());
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

absl::StatusOr<ServerAddressList> XdsOverrideHostLb::UpdateAddressMap(
    absl::StatusOr<ServerAddressList> addresses) {
  if (!addresses.ok()) {
    return addresses;
  }
  ServerAddressList return_value;
  std::map<const std::string, XdsHealthStatus> addresses_for_map;
  for (const auto& address : *addresses) {
    XdsHealthStatus status = GetAddressHealthStatus(address);
    if (status.status() != XdsHealthStatus::kDraining) {
      return_value.push_back(address);
    } else if (!config_->override_host_status_set().Contains(status)) {
      // Skip draining hosts if not in the override status set.
      continue;
    }
    auto key = grpc_sockaddr_to_uri(&address.address());
    if (key.ok()) {
      addresses_for_map.emplace(std::move(*key), status);
    }
  }
  {
    MutexLock lock(&subchannel_map_mu_);
    for (auto it = subchannel_map_.begin(); it != subchannel_map_.end();) {
      if (addresses_for_map.find(it->first) == addresses_for_map.end()) {
        it = subchannel_map_.erase(it);
      } else {
        ++it;
      }
    }
    for (const auto& key_status : addresses_for_map) {
      auto it = subchannel_map_.find(key_status.first);
      if (it == subchannel_map_.end()) {
        subchannel_map_.emplace(std::piecewise_construct,
                                std::forward_as_tuple(key_status.first),
                                std::forward_as_tuple(key_status.second));
      } else {
        it->second.SetEdsHealthStatus(key_status.second);
      }
    }
  }
  return return_value;
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::AdoptSubchannel(
    ServerAddress address, RefCountedPtr<SubchannelInterface> subchannel) {
  auto key = grpc_sockaddr_to_uri(&address.address());
  if (!key.ok()) {
    return subchannel;
  }
  auto wrapper =
      MakeRefCounted<SubchannelWrapper>(std::move(subchannel), Ref(), *key);
  MutexLock lock(&subchannel_map_mu_);
  auto it = subchannel_map_.find(*key);
  if (it != subchannel_map_.end()) {
    it->second.SetSubchannel(wrapper.get());
  }
  return wrapper;
}

void XdsOverrideHostLb::UnsetSubchannel(absl::string_view key,
                                        SubchannelWrapper* subchannel) {
  MutexLock lock(&subchannel_map_mu_);
  auto it = subchannel_map_.find(key);
  if (it != subchannel_map_.end()) {
    if (subchannel == it->second.GetSubchannel()) {
      it->second.UnsetSubchannel();
    }
  }
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::GetSubchannelByAddress(
    absl::string_view address, XdsHealthStatusSet overriden_health_statuses) {
  MutexLock lock(&subchannel_map_mu_);
  auto it = subchannel_map_.find(address);
  if (it == subchannel_map_.end() || it->second.GetSubchannel() == nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
      gpr_log(GPR_INFO, "Subchannel %s was not found",
              std::string(address).c_str());
    }
    return nullptr;
  }
  if (!overriden_health_statuses.Contains(it->second.eds_health_status())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
      gpr_log(GPR_INFO, "Subchannel %s health status is not overridden (%s)",
              std::string(address).c_str(),
              it->second.eds_health_status().ToString());
    }
    return nullptr;
  }
  return it->second.GetSubchannel()->Ref();
}

void XdsOverrideHostLb::OnSubchannelConnectivityStateChange(
    absl::string_view subchannel_key) {
  auto it = subchannel_map_.find(subchannel_key);
  if (it == subchannel_map_.end()) {
    return;
  }
  if (it->second.eds_health_status().status() == XdsHealthStatus::kDraining) {
    MaybeUpdatePickerLocked();
  }
}

//
// XdsOverrideHostLb::Helper
//

RefCountedPtr<SubchannelInterface> XdsOverrideHostLb::Helper::CreateSubchannel(
    ServerAddress address, const ChannelArgs& args) {
  auto subchannel =
      parent()->channel_control_helper()->CreateSubchannel(address, args);
  return parent()->AdoptSubchannel(address, subchannel);
}

void XdsOverrideHostLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  if (parent()->shutting_down_) return;
  // Save the state and picker.
  parent()->state_ = state;
  parent()->status_ = status;
  parent()->picker_ = std::move(picker);
  // Wrap the picker and return it to the channel.
  parent()->MaybeUpdatePickerLocked();
}

//
// XdsOverrideHostLb::SubchannelWrapper::SubchannelWrapper
//

XdsOverrideHostLb::SubchannelWrapper::SubchannelWrapper(
    RefCountedPtr<SubchannelInterface> subchannel,
    RefCountedPtr<XdsOverrideHostLb> policy, absl::string_view key)
    : DelegatingSubchannel(std::move(subchannel)),
      key_(key),
      policy_(std::move(policy)) {
  auto watcher = std::make_unique<ConnectivityStateWatcher>(WeakRef());
  watcher_ = watcher.get();
  wrapped_subchannel()->WatchConnectivityState(std::move(watcher));
}

XdsOverrideHostLb::SubchannelWrapper::~SubchannelWrapper() {
  if (key_.has_value()) {
    policy_->UnsetSubchannel(*key_, this);
  }
}

void XdsOverrideHostLb::SubchannelWrapper::WatchConnectivityState(
    std::unique_ptr<ConnectivityStateWatcherInterface> watcher) {
  watchers_.insert(std::move(watcher));
}

void XdsOverrideHostLb::SubchannelWrapper::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  auto it = watchers_.find(watcher);
  if (it != watchers_.end()) {
    watchers_.erase(it);
  }
}

void XdsOverrideHostLb::SubchannelWrapper::UpdateConnectivityState(
    grpc_connectivity_state state, absl::Status status) {
  connectivity_state_.store(state);
  // Sending connectivity state notifications to the watchers may cause the set
  // of watchers to change, so we can't be iterating over the set of watchers
  // while we send the notifications
  std::vector<ConnectivityStateWatcherInterface*> watchers(watchers_.size());
  for (const auto& watcher : watchers_) {
    watchers.push_back(watcher.get());
  }
  for (const auto& watcher : watchers) {
    if (watchers_.find(watcher) != watchers_.end()) {
      watcher->OnConnectivityStateChange(state, status);
    }
  }
  if (key_.has_value()) {
    policy_->OnSubchannelConnectivityStateChange(*key_);
  }
}

void XdsOverrideHostLb::SubchannelWrapper::Orphan() {
  key_.reset();
  wrapped_subchannel()->CancelConnectivityStateWatch(watcher_);
}

grpc_pollset_set* XdsOverrideHostLb::SubchannelWrapper::
    ConnectivityStateWatcher::interested_parties() {
  return subchannel_->policy_->interested_parties();
}

void XdsOverrideHostLb::SubchannelWrapper::ConnectivityStateWatcher::
    OnConnectivityStateChange(grpc_connectivity_state state,
                              absl::Status status) {
  subchannel_->UpdateConnectivityState(state, status);
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
    return LoadFromJson<RefCountedPtr<XdsOverrideHostLbConfig>>(
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

void XdsOverrideHostLbConfig::JsonPostLoad(const Json& json,
                                           const JsonArgs& args,
                                           ValidationErrors* errors) {
  {
    ValidationErrors::ScopedField field(errors, ".childPolicy");
    auto it = json.object().find("childPolicy");
    if (it == json.object().end()) {
      errors->AddError("field not present");
    } else {
      auto child_policy_config = CoreConfiguration::Get()
                                     .lb_policy_registry()
                                     .ParseLoadBalancingConfig(it->second);
      if (!child_policy_config.ok()) {
        errors->AddError(child_policy_config.status().message());
      } else {
        child_config_ = std::move(*child_policy_config);
      }
    }
  }
  {
    ValidationErrors::ScopedField field(errors, ".overrideHostStatus");
    auto host_status_list = LoadJsonObjectField<std::vector<std::string>>(
        json.object(), args, "overrideHostStatus", errors,
        /*required=*/false);
    if (host_status_list.has_value()) {
      for (size_t i = 0; i < host_status_list->size(); ++i) {
        const std::string& host_status = (*host_status_list)[i];
        auto status = XdsHealthStatus::FromString(host_status);
        if (!status.has_value()) {
          ValidationErrors::ScopedField field(errors,
                                              absl::StrCat("[", i, "]"));
          errors->AddError("invalid host status");
        } else {
          override_host_status_set_.Add(*status);
        }
      }
    } else {
      override_host_status_set_ = XdsHealthStatusSet(
          {XdsHealthStatus(XdsHealthStatus::HealthStatus::kHealthy),
           XdsHealthStatus(XdsHealthStatus::HealthStatus::kUnknown)});
    }
  }
}

}  // namespace grpc_core
