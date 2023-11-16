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
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

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
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/ref_counted_string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/delegating_helper.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
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

XdsHealthStatus GetEndpointHealthStatus(const EndpointAddresses& endpoint) {
  return XdsHealthStatus(static_cast<XdsHealthStatus::HealthStatus>(
      endpoint.args()
          .GetInt(GRPC_ARG_XDS_HEALTH_STATUS)
          .value_or(XdsHealthStatus::HealthStatus::kUnknown)));
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
                      RefCountedPtr<XdsOverrideHostLb> policy);

    ~SubchannelWrapper() override;

    void WatchConnectivityState(
        std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override;

    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface* watcher) override;

    grpc_connectivity_state connectivity_state() {
      return connectivity_state_.load();
    }

    XdsOverrideHostLb* policy() { return policy_.get(); }

    void set_key(absl::string_view key) { key_ = std::string(key); }
    const absl::optional<std::string>& key() const { return key_; }

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

  class SubchannelEntry {
   public:
    explicit SubchannelEntry(XdsHealthStatus eds_health_status)
        : eds_health_status_(eds_health_status) {}

    void SetSubchannel(SubchannelWrapper* subchannel) {
      if (eds_health_status_.status() == XdsHealthStatus::kDraining) {
        subchannel_ = subchannel->Ref();
      } else {
        subchannel_ = subchannel->WeakRef();
      }
    }

    void UnsetSubchannel() {
      subchannel_ = WeakRefCountedPtr<SubchannelWrapper>(nullptr);
    }

    SubchannelWrapper* GetSubchannel() const {
      return Match(
          subchannel_,
          [](WeakRefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
                 subchannel) { return subchannel.get(); },
          [](RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper> subchannel) {
            return subchannel.get();
          });
    }

    void SetEdsHealthStatus(XdsHealthStatus eds_health_status) {
      eds_health_status_ = eds_health_status;
      auto subchannel = GetSubchannel();
      if (subchannel == nullptr) return;
      if (eds_health_status_.status() == XdsHealthStatus::kDraining) {
        subchannel_ = subchannel->Ref();
      } else {
        subchannel_ = subchannel->WeakRef();
      }
    }

    XdsHealthStatus eds_health_status() const { return eds_health_status_; }

    void set_address_list(RefCountedStringValue address_list) {
      address_list_ = std::move(address_list);
    }

    RefCountedStringValue address_list() const { return address_list_; }

   private:
    absl::variant<WeakRefCountedPtr<SubchannelWrapper>,
                  RefCountedPtr<SubchannelWrapper>>
        subchannel_;
    XdsHealthStatus eds_health_status_;
    RefCountedStringValue address_list_;
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
        XdsOverrideHostAttribute* override_host_attr) const;

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
        const grpc_resolved_address& address,
        const ChannelArgs& per_address_args, const ChannelArgs& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override;
  };

  ~XdsOverrideHostLb() override;

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  void MaybeUpdatePickerLocked();

  void UpdateAddressMap(const EndpointAddressesIterator& endpoints);

  RefCountedPtr<SubchannelWrapper> AdoptSubchannel(
      const grpc_resolved_address& address,
      RefCountedPtr<SubchannelInterface> subchannel);

  void UnsetSubchannel(absl::string_view key, SubchannelWrapper* subchannel);

  void OnSubchannelConnectivityStateChange(absl::string_view subchannel_key)
      ABSL_NO_THREAD_SAFETY_ANALYSIS;  // Called from within the
                                       // WorkSerializer and does not require
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
XdsOverrideHostLb::Picker::PickOverridenHost(
    XdsOverrideHostAttribute* override_host_attr) const {
  GPR_ASSERT(override_host_attr != nullptr);
  auto cookie_address_list = override_host_attr->cookie_address_list();
  if (cookie_address_list.empty()) return absl::nullopt;
  // The cookie has an address list, so look through the addresses in order.
  RefCountedPtr<SubchannelWrapper> idle_subchannel;
  bool found_connecting = false;
  {
    MutexLock lock(&policy_->subchannel_map_mu_);
    for (absl::string_view address : absl::StrSplit(cookie_address_list, ',')) {
      RefCountedPtr<SubchannelWrapper> subchannel;
      auto it = policy_->subchannel_map_.find(address);
      if (it != policy_->subchannel_map_.end()) {
        subchannel = it->second.GetSubchannel()->RefIfNonZero();
      }
      if (subchannel == nullptr) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO, "Subchannel %s was not found",
                  std::string(address).c_str());
        }
        continue;
      }
      if (!override_host_health_status_set_.Contains(
              it->second.eds_health_status())) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO,
                  "Subchannel %s health status is not overridden (%s)",
                  std::string(address).c_str(),
                  it->second.eds_health_status().ToString());
        }
        continue;
      }
      auto connectivity_state = subchannel->connectivity_state();
      if (connectivity_state == GRPC_CHANNEL_READY) {
        // Found a READY subchannel.  Pass back the actual address list
        // and return the subchannel.
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO, "Picker override found READY subchannel %s",
                  std::string(address).c_str());
        }
        override_host_attr->set_actual_address_list(it->second.address_list());
        return PickResult::Complete(subchannel->wrapped_subchannel());
      } else if (connectivity_state == GRPC_CHANNEL_IDLE) {
        if (idle_subchannel == nullptr) idle_subchannel = std::move(subchannel);
      } else if (connectivity_state == GRPC_CHANNEL_CONNECTING) {
        found_connecting = true;
      }
    }
  }
  // No READY subchannel found.  If we found an IDLE subchannel, trigger
  // a connection attempt and queue the pick until that attempt completes.
  if (idle_subchannel != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
      gpr_log(GPR_INFO, "Picker override found IDLE subchannel");
    }
    // Deletes itself after the connection is requested.
    new SubchannelConnectionRequester(std::move(idle_subchannel));
    return PickResult::Queue();
  }
  // No READY or IDLE subchannels.  If we found a CONNECTING subchannel,
  // queue the pick and wait for the connection attempt to complete.
  if (found_connecting) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
      gpr_log(GPR_INFO, "Picker override found CONNECTING subchannel");
    }
    return PickResult::Queue();
  }
  // No READY, IDLE, or CONNECTING subchannels found.
  return absl::nullopt;
}

LoadBalancingPolicy::PickResult XdsOverrideHostLb::Picker::Pick(PickArgs args) {
  auto* call_state = static_cast<ClientChannelLbCallState*>(args.call_state);
  auto* override_host_attr = static_cast<XdsOverrideHostAttribute*>(
      call_state->GetCallAttribute(XdsOverrideHostAttribute::TypeName()));
  if (override_host_attr != nullptr) {
    auto overridden_host_pick = PickOverridenHost(override_host_attr);
    if (overridden_host_pick.has_value()) {
      return std::move(*overridden_host_pick);
    }
  }
  // No usable override.  Delegate to child picker.
  if (picker_ == nullptr) {  // Should never happen.
    return PickResult::Fail(absl::InternalError(
        "xds_override_host picker not given any child picker"));
  }
  auto result = picker_->Pick(args);
  auto complete_pick = absl::get_if<PickResult::Complete>(&result.result);
  if (complete_pick != nullptr) {
    auto* wrapper =
        static_cast<SubchannelWrapper*>(complete_pick->subchannel.get());
    // Populate the address list in the override host attribute so that
    // the StatefulSession filter can set the cookie.
    if (override_host_attr != nullptr) {
      auto& key = wrapper->key();
      if (key.has_value()) {
        MutexLock lock(&policy_->subchannel_map_mu_);
        auto it = policy_->subchannel_map_.find(*key);
        if (it != policy_->subchannel_map_.end()) {  // Should always be true.
          override_host_attr->set_actual_address_list(
              it->second.address_list());
        }
      }
    }
    // Unwrap the subchannel.
    complete_pick->subchannel = wrapper->wrapped_subchannel();
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

// Wraps the endpoint iterator and filters out endpoints in state DRAINING.
class ChildEndpointIterator : public EndpointAddressesIterator {
 public:
  explicit ChildEndpointIterator(
      std::shared_ptr<EndpointAddressesIterator> parent_it)
      : parent_it_(std::move(parent_it)) {}

  void ForEach(absl::FunctionRef<void(const EndpointAddresses&)> callback)
      const override {
    parent_it_->ForEach([&](const EndpointAddresses& endpoint) {
      XdsHealthStatus status = GetEndpointHealthStatus(endpoint);
      if (status.status() != XdsHealthStatus::kDraining) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO,
                  "[xds_override_host_lb %p] endpoint %s: not draining, "
                  "passing to child",
                  this, endpoint.ToString().c_str());
        }
        callback(endpoint);
      }
    });
  }

 private:
  std::shared_ptr<EndpointAddressesIterator> parent_it_;
};

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
  // Update address map and wrap endpoint iterator for child policy.
  if (args.addresses.ok()) {
    UpdateAddressMap(**args.addresses);
    args.addresses =
        std::make_shared<ChildEndpointIterator>(std::move(*args.addresses));
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
      gpr_log(GPR_INFO, "[xds_override_host_lb %p] address error: %s", this,
              args.addresses.status().ToString().c_str());
    }
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
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

void XdsOverrideHostLb::UpdateAddressMap(
    const EndpointAddressesIterator& endpoints) {
  // Construct a map of address info from which to update subchannel_map_.
  struct AddressInfo {
    XdsHealthStatus eds_health_status;
    RefCountedStringValue address_list;
    AddressInfo(XdsHealthStatus status, RefCountedStringValue addresses)
        : eds_health_status(status), address_list(std::move(addresses)) {}
  };
  std::map<const std::string, AddressInfo> addresses_for_map;
  endpoints.ForEach([&](const EndpointAddresses& endpoint) {
    XdsHealthStatus status = GetEndpointHealthStatus(endpoint);
    // Skip draining hosts if not in the override status set.
    if (status.status() == XdsHealthStatus::kDraining &&
        !config_->override_host_status_set().Contains(status)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
        gpr_log(GPR_INFO,
                "[xds_override_host_lb %p] endpoint %s: draining but not in "
                "override_host_status set -- ignoring",
                this, endpoint.ToString().c_str());
      }
      return;
    }
    std::vector<std::string> addresses;
    addresses.reserve(endpoint.addresses().size());
    for (const auto& address : endpoint.addresses()) {
      auto key = grpc_sockaddr_to_string(&address, /*normalize=*/false);
      if (key.ok()) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO,
                  "[xds_override_host_lb %p] endpoint %s: adding map key %s",
                  this, endpoint.ToString().c_str(), key->c_str());
        }
        addresses.push_back(*std::move(key));
      }
    }
    absl::Span<const std::string> addresses_span = addresses;
    for (size_t i = 0; i < addresses.size(); ++i) {
      std::string start = absl::StrJoin(addresses_span.subspan(0, i), ",");
      std::string end = absl::StrJoin(addresses_span.subspan(i + 1), ",");
      RefCountedStringValue address_list(
          absl::StrCat(addresses[i], (start.empty() ? "" : ","), start,
                       (end.empty() ? "" : ","), end));
      addresses_for_map.emplace(
          std::piecewise_construct, std::forward_as_tuple(addresses[i]),
          std::forward_as_tuple(status, std::move(address_list)));
    }
  });
  // Now grab the lock and update subchannel_map_ from addresses_for_map.
  {
    MutexLock lock(&subchannel_map_mu_);
    for (auto it = subchannel_map_.begin(); it != subchannel_map_.end();) {
      if (addresses_for_map.find(it->first) == addresses_for_map.end()) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO, "[xds_override_host_lb %p] removing map key %s",
                  this, it->first.c_str());
        }
        it = subchannel_map_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto& p : addresses_for_map) {
      const auto& address = p.first;
      auto& address_info = p.second;
      auto it = subchannel_map_.find(address);
      if (it == subchannel_map_.end()) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO, "[xds_override_host_lb %p] adding map key %s", this,
                  address.c_str());
        }
        it = subchannel_map_
                 .emplace(std::piecewise_construct,
                          std::forward_as_tuple(address),
                          std::forward_as_tuple(address_info.eds_health_status))
                 .first;
      } else {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
          gpr_log(GPR_INFO,
                  "[xds_override_host_lb %p] setting EDS health status for "
                  "%s to %s",
                  this, address.c_str(),
                  address_info.eds_health_status.ToString());
        }
        it->second.SetEdsHealthStatus(address_info.eds_health_status);
      }
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_xds_override_host_trace)) {
        gpr_log(GPR_INFO,
                "[xds_override_host_lb %p] setting address list for %s to %s",
                this, address.c_str(), address_info.address_list.c_str());
      }
      it->second.set_address_list(std::move(address_info.address_list));
    }
  }
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::AdoptSubchannel(
    const grpc_resolved_address& address,
    RefCountedPtr<SubchannelInterface> subchannel) {
  auto key = grpc_sockaddr_to_string(&address, /*normalize=*/false);
  auto wrapper =
      MakeRefCounted<SubchannelWrapper>(std::move(subchannel), Ref());
  if (key.ok()) {
    MutexLock lock(&subchannel_map_mu_);
    auto it = subchannel_map_.find(*key);
    if (it != subchannel_map_.end()) {
      wrapper->set_key(*key);
      it->second.SetSubchannel(wrapper.get());
    }
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
    const grpc_resolved_address& address, const ChannelArgs& per_address_args,
    const ChannelArgs& args) {
  auto subchannel = parent()->channel_control_helper()->CreateSubchannel(
      address, per_address_args, args);
  return parent()->AdoptSubchannel(address, std::move(subchannel));
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
    RefCountedPtr<XdsOverrideHostLb> policy)
    : DelegatingSubchannel(std::move(subchannel)), policy_(std::move(policy)) {
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
  std::vector<ConnectivityStateWatcherInterface*> watchers;
  watchers.reserve(watchers_.size());
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
  if (!IsWorkSerializerDispatchEnabled()) {
    key_.reset();
    wrapped_subchannel()->CancelConnectivityStateWatch(watcher_);
    return;
  }
  WeakRefCountedPtr<SubchannelWrapper> self = WeakRef();
  policy_->work_serializer()->Run(
      [self = std::move(self)]() {
        self->key_.reset();
        self->wrapped_subchannel()->CancelConnectivityStateWatch(
            self->watcher_);
      },
      DEBUG_LOCATION);
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
