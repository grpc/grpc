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

#include "src/core/load_balancing/xds/xds_override_host.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <algorithm>
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
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/child_policy_handler.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/xds/xds_config.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/match.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/ref_counted_string.h"
#include "src/core/util/sync.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/grpc/xds_health_status.h"

namespace grpc_core {

using ::grpc_event_engine::experimental::EventEngine;

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

//
// xds_override_host LB policy
//

class XdsOverrideHostLb final : public LoadBalancingPolicy {
 public:
  explicit XdsOverrideHostLb(Args args);

  absl::string_view name() const override {
    return XdsOverrideHostLbConfig::Name();
  }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class SubchannelEntry;

  class SubchannelWrapper final : public DelegatingSubchannel {
   public:
    SubchannelWrapper(RefCountedPtr<SubchannelInterface> subchannel,
                      RefCountedPtr<XdsOverrideHostLb> policy);

    // Called immediately after construction.  We use two-phase initialization
    // to avoid doing an allocation while holding the lock.
    void set_subchannel_entry(RefCountedPtr<SubchannelEntry> subchannel_entry) {
      subchannel_entry_ = std::move(subchannel_entry);
    }

    void WatchConnectivityState(
        std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override;

    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface* watcher) override;

    RefCountedStringValue address_list() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      return subchannel_entry_->address_list();
    }

    void set_last_used_time()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      subchannel_entry_->set_last_used_time();
    }

    XdsOverrideHostLb* policy() const { return policy_.get(); }

    RefCountedPtr<SubchannelWrapper> Clone() const {
      auto subchannel =
          MakeRefCounted<SubchannelWrapper>(wrapped_subchannel(), policy_);
      subchannel->set_subchannel_entry(subchannel_entry_);
      return subchannel;
    }

   private:
    class ConnectivityStateWatcher final
        : public ConnectivityStateWatcherInterface {
     public:
      explicit ConnectivityStateWatcher(
          WeakRefCountedPtr<SubchannelWrapper> subchannel)
          : subchannel_(std::move(subchannel)) {}

      void OnConnectivityStateChange(grpc_connectivity_state state,
                                     absl::Status status) override {
        subchannel_->UpdateConnectivityState(state, status);
      }

      grpc_pollset_set* interested_parties() override {
        return subchannel_->policy()->interested_parties();
      }

     private:
      WeakRefCountedPtr<SubchannelWrapper> subchannel_;
    };

    void Orphaned() override;
    void UpdateConnectivityState(grpc_connectivity_state state,
                                 absl::Status status);

    RefCountedPtr<XdsOverrideHostLb> policy_;
    RefCountedPtr<SubchannelEntry> subchannel_entry_;
    ConnectivityStateWatcher* watcher_;
    std::set<std::unique_ptr<ConnectivityStateWatcherInterface>,
             PtrLessThan<ConnectivityStateWatcherInterface>>
        watchers_;
  };

  // An entry in the subchannel map.
  //
  // The entry may hold either an owned (RefCountedPtr<>) or unowned
  // (raw pointer) SubchannelWrapper, but not both.  It will be unowned
  // in the case where the SubchannelWrapper is owned by the child policy.
  // It will be owned in the case where the child policy has not created a
  // subchannel but we have RPCs whose cookies point to that address.
  //
  // Note that when a SubchannelWrapper is orphaned, it will try to
  // acquire the lock to remove itself from the entry.  This means that
  // whenever we need to remove an owned subchannel from an entry, if we
  // released our ref to the SubchannelWrapper immediately, we would
  // cause a deadlock, since our caller is already holding the lock.  To
  // avoid that, any method that may result in releasing a ref to the
  // SubchannelWrapper will instead return that ref to the caller, who is
  // responsible for releasing the ref after releasing the lock.
  class SubchannelEntry final : public RefCounted<SubchannelEntry> {
   public:
    bool HasOwnedSubchannel() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      auto* sc = absl::get_if<RefCountedPtr<SubchannelWrapper>>(&subchannel_);
      return sc != nullptr && *sc != nullptr;
    }

    // Sets the unowned subchannel.  If the entry previously had an
    // owned subchannel, returns the ref to it.
    RefCountedPtr<SubchannelWrapper> SetUnownedSubchannel(
        SubchannelWrapper* subchannel)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_);

    // Sets the owned subchannel.  Must not be called if the entry
    // already has an owned subchannel.
    void SetOwnedSubchannel(RefCountedPtr<SubchannelWrapper> subchannel)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      DCHECK(!HasOwnedSubchannel());
      subchannel_ = std::move(subchannel);
    }

    // Returns a pointer to the subchannel, regardless of whether it's
    // owned or not.
    SubchannelWrapper* GetSubchannel() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_);

    // Returns a ref to the subchannel, regardless of whether it's owned
    // or not.  Returns null if there is no subchannel or if the
    // subchannel's ref count is 0.
    RefCountedPtr<SubchannelWrapper> GetSubchannelRef() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_);

    // If the entry has an owned subchannel, moves it out of the entry
    // and returns it.
    RefCountedPtr<SubchannelWrapper> TakeOwnedSubchannel()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_);

    // Unsets the entry's subchannel.
    // If the entry had an owned subchannel, moves the ref into
    // owned_subchannels.
    void UnsetSubchannel(
        std::vector<RefCountedPtr<SubchannelWrapper>>* owned_subchannels)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_);

    // Called when a SubchannelWrapper is orphaned.  May replace the
    // unowned SubchannelWrapper with an owned one based on
    // last_used_time_ and connection_idle_timeout.
    void OnSubchannelWrapperOrphan(SubchannelWrapper* wrapper,
                                   Duration connection_idle_timeout)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_);

    grpc_connectivity_state connectivity_state() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      return connectivity_state_;
    }
    void set_connectivity_state(grpc_connectivity_state state)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      connectivity_state_ = state;
    }

    XdsHealthStatus eds_health_status() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      return eds_health_status_;
    }
    void set_eds_health_status(XdsHealthStatus eds_health_status)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      eds_health_status_ = eds_health_status;
    }

    RefCountedStringValue address_list() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      return address_list_;
    }
    void set_address_list(RefCountedStringValue address_list)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      address_list_ = std::move(address_list);
    }

    Timestamp last_used_time() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      return last_used_time_;
    }
    void set_last_used_time()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
      last_used_time_ = Timestamp::Now();
    }

   private:
    grpc_connectivity_state connectivity_state_
        ABSL_GUARDED_BY(&XdsOverrideHostLb::mu_) = GRPC_CHANNEL_IDLE;
    absl::variant<SubchannelWrapper*, RefCountedPtr<SubchannelWrapper>>
        subchannel_ ABSL_GUARDED_BY(&XdsOverrideHostLb::mu_);
    XdsHealthStatus eds_health_status_ ABSL_GUARDED_BY(
        &XdsOverrideHostLb::mu_) = XdsHealthStatus(XdsHealthStatus::kUnknown);
    RefCountedStringValue address_list_
        ABSL_GUARDED_BY(&XdsOverrideHostLb::mu_);
    Timestamp last_used_time_ ABSL_GUARDED_BY(&XdsOverrideHostLb::mu_) =
        Timestamp::InfPast();
  };

  // A picker that wraps the picker from the child for cases when cookie is
  // present.
  class Picker final : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<XdsOverrideHostLb> xds_override_host_lb,
           RefCountedPtr<SubchannelPicker> picker,
           XdsHealthStatusSet override_host_health_status_set);

    PickResult Pick(PickArgs args) override;

   private:
    class SubchannelConnectionRequester final {
     public:
      explicit SubchannelConnectionRequester(
          RefCountedPtr<SubchannelWrapper> subchannel)
          : subchannel_(std::move(subchannel)) {
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
        // Hop into ExecCtx, so that we don't get stuck running
        // arbitrary WorkSerializer callbacks while doing a pick.
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

    class SubchannelCreationRequester final {
     public:
      SubchannelCreationRequester(RefCountedPtr<XdsOverrideHostLb> policy,
                                  absl::string_view address)
          : policy_(std::move(policy)), address_(address) {
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
        // Hop into ExecCtx, so that we don't get stuck running
        // arbitrary WorkSerializer callbacks while doing a pick.
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

     private:
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<SubchannelCreationRequester*>(arg);
        self->policy_->work_serializer()->Run(
            [self]() {
              self->policy_->CreateSubchannelForAddress(self->address_);
              delete self;
            },
            DEBUG_LOCATION);
      }

      RefCountedPtr<XdsOverrideHostLb> policy_;
      std::string address_;
      grpc_closure closure_;
    };

    absl::optional<LoadBalancingPolicy::PickResult> PickOverriddenHost(
        XdsOverrideHostAttribute* override_host_attr) const;

    RefCountedPtr<XdsOverrideHostLb> policy_;
    RefCountedPtr<SubchannelPicker> picker_;
    XdsHealthStatusSet override_host_health_status_set_;
  };

  class Helper final
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

  class IdleTimer final : public InternallyRefCounted<IdleTimer> {
   public:
    IdleTimer(RefCountedPtr<XdsOverrideHostLb> policy, Duration duration);

    void Orphan() override;

   private:
    void OnTimerLocked();

    RefCountedPtr<XdsOverrideHostLb> policy_;
    absl::optional<EventEngine::TaskHandle> timer_handle_;
  };

  ~XdsOverrideHostLb() override;

  void ShutdownLocked() override;

  void ResetState();
  void ReportTransientFailure(absl::Status status);

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  void MaybeUpdatePickerLocked();

  void UpdateAddressMap(const EndpointAddressesIterator& endpoints);

  RefCountedPtr<SubchannelWrapper> AdoptSubchannel(
      const grpc_resolved_address& address,
      RefCountedPtr<SubchannelInterface> subchannel);

  void CreateSubchannelForAddress(absl::string_view address);

  void CleanupSubchannels();

  // State from most recent resolver update.
  ChannelArgs args_;
  XdsHealthStatusSet override_host_status_set_;
  Duration connection_idle_timeout_;

  // Internal state.
  bool shutting_down_ = false;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_CONNECTING;
  absl::Status status_;
  RefCountedPtr<SubchannelPicker> picker_;
  Mutex mu_;
  std::map<std::string, RefCountedPtr<SubchannelEntry>, std::less<>>
      subchannel_map_ ABSL_GUARDED_BY(mu_);

  // Timer handle for periodic subchannel sweep.
  OrphanablePtr<IdleTimer> idle_timer_;
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
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << policy_.get()
      << "] constructed new picker " << this;
}

absl::optional<LoadBalancingPolicy::PickResult>
XdsOverrideHostLb::Picker::PickOverriddenHost(
    XdsOverrideHostAttribute* override_host_attr) const {
  CHECK_NE(override_host_attr, nullptr);
  auto cookie_address_list = override_host_attr->cookie_address_list();
  if (cookie_address_list.empty()) return absl::nullopt;
  // The cookie has an address list, so look through the addresses in order.
  absl::string_view address_with_no_subchannel;
  RefCountedPtr<SubchannelWrapper> idle_subchannel;
  bool found_connecting = false;
  {
    MutexLock lock(&policy_->mu_);
    for (absl::string_view address : absl::StrSplit(cookie_address_list, ',')) {
      auto it = policy_->subchannel_map_.find(address);
      if (it == policy_->subchannel_map_.end()) continue;
      if (!override_host_health_status_set_.Contains(
              it->second->eds_health_status())) {
        GRPC_TRACE_LOG(xds_override_host_lb, INFO)
            << "Subchannel " << address << " health status is not overridden ("
            << it->second->eds_health_status().ToString() << ")";
        continue;
      }
      auto subchannel = it->second->GetSubchannelRef();
      if (subchannel == nullptr) {
        GRPC_TRACE_LOG(xds_override_host_lb, INFO)
            << "No subchannel for " << address;
        if (address_with_no_subchannel.empty()) {
          address_with_no_subchannel = it->first;
        }
        continue;
      }
      auto connectivity_state = it->second->connectivity_state();
      if (connectivity_state == GRPC_CHANNEL_READY) {
        // Found a READY subchannel.  Pass back the actual address list
        // and return the subchannel.
        GRPC_TRACE_LOG(xds_override_host_lb, INFO)
            << "Picker override found READY subchannel " << address;
        it->second->set_last_used_time();
        override_host_attr->set_actual_address_list(it->second->address_list());
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
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "Picker override found IDLE subchannel";
    // Deletes itself after the connection is requested.
    new SubchannelConnectionRequester(std::move(idle_subchannel));
    return PickResult::Queue();
  }
  // No READY or IDLE subchannels.  If we found a CONNECTING subchannel,
  // queue the pick and wait for the connection attempt to complete.
  if (found_connecting) {
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "Picker override found CONNECTING subchannel";
    return PickResult::Queue();
  }
  // No READY, IDLE, or CONNECTING subchannels found.  If we found an
  // entry that has no subchannel, then queue the pick and trigger
  // creation of a subchannel for that entry.
  if (!address_with_no_subchannel.empty()) {
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "Picker override found entry with no subchannel";
    if (!IsWorkSerializerDispatchEnabled()) {
      new SubchannelCreationRequester(policy_, address_with_no_subchannel);
    } else {
      policy_->work_serializer()->Run(
          [policy = policy_,
           address = std::string(address_with_no_subchannel)]() {
            policy->CreateSubchannelForAddress(address);
          },
          DEBUG_LOCATION);
    }
    return PickResult::Queue();
  }
  // No entry found that was not in TRANSIENT_FAILURE.
  return absl::nullopt;
}

LoadBalancingPolicy::PickResult XdsOverrideHostLb::Picker::Pick(PickArgs args) {
  auto* call_state = static_cast<ClientChannelLbCallState*>(args.call_state);
  auto* override_host_attr =
      call_state->GetCallAttribute<XdsOverrideHostAttribute>();
  if (override_host_attr != nullptr) {
    auto overridden_host_pick = PickOverriddenHost(override_host_attr);
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
      MutexLock lock(&wrapper->policy()->mu_);
      wrapper->set_last_used_time();
      override_host_attr->set_actual_address_list(wrapper->address_list());
    }
    // Unwrap the subchannel.
    complete_pick->subchannel = wrapper->wrapped_subchannel();
  }
  return result;
}

//
// XdsOverrideHostLb::IdleTimer
//

XdsOverrideHostLb::IdleTimer::IdleTimer(RefCountedPtr<XdsOverrideHostLb> policy,
                                        Duration duration)
    : policy_(std::move(policy)) {
  // Min time between timer runs is 5s so that we don't kill ourselves
  // with lock contention and CPU usage due to sweeps over the map.
  duration = std::max(duration, Duration::Seconds(5));
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << policy_.get() << "] idle timer " << this
      << ": subchannel cleanup pass will run in " << duration;
  timer_handle_ = policy_->channel_control_helper()->GetEventEngine()->RunAfter(
      duration, [self = RefAsSubclass<IdleTimer>()]() mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        auto self_ptr = self.get();
        self_ptr->policy_->work_serializer()->Run(
            [self = std::move(self)]() { self->OnTimerLocked(); },
            DEBUG_LOCATION);
      });
}

void XdsOverrideHostLb::IdleTimer::Orphan() {
  if (timer_handle_.has_value()) {
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "[xds_override_host_lb " << policy_.get() << "] idle timer " << this
        << ": cancelling";
    policy_->channel_control_helper()->GetEventEngine()->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  Unref();
}

void XdsOverrideHostLb::IdleTimer::OnTimerLocked() {
  if (timer_handle_.has_value()) {
    timer_handle_.reset();
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "[xds_override_host_lb " << policy_.get() << "] idle timer " << this
        << ": timer fired";
    policy_->CleanupSubchannels();
  }
}

//
// XdsOverrideHostLb
//

XdsOverrideHostLb::XdsOverrideHostLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this << "] created";
}

XdsOverrideHostLb::~XdsOverrideHostLb() {
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this
      << "] destroying xds_override_host LB policy";
}

void XdsOverrideHostLb::ShutdownLocked() {
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this << "] shutting down";
  shutting_down_ = true;
  ResetState();
}

void XdsOverrideHostLb::ResetState() {
  {
    // Drop subchannel refs after releasing the lock to avoid deadlock.
    std::vector<RefCountedPtr<SubchannelWrapper>> subchannel_refs_to_drop;
    MutexLock lock(&mu_);
    subchannel_refs_to_drop.reserve(subchannel_map_.size());
    for (auto& p : subchannel_map_) {
      p.second->UnsetSubchannel(&subchannel_refs_to_drop);
    }
    subchannel_map_.clear();
  }
  // Cancel timer, if any.
  idle_timer_.reset();
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

void XdsOverrideHostLb::ReportTransientFailure(absl::Status status) {
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this
      << "] reporting TRANSIENT_FAILURE: " << status;
  ResetState();
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, status,
      MakeRefCounted<TransientFailurePicker>(status));
}

void XdsOverrideHostLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void XdsOverrideHostLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

XdsHealthStatus GetEndpointHealthStatus(const EndpointAddresses& endpoint) {
  return XdsHealthStatus(static_cast<XdsHealthStatus::HealthStatus>(
      endpoint.args()
          .GetInt(GRPC_ARG_XDS_HEALTH_STATUS)
          .value_or(XdsHealthStatus::HealthStatus::kUnknown)));
}

// Wraps the endpoint iterator and filters out endpoints in state DRAINING.
class ChildEndpointIterator final : public EndpointAddressesIterator {
 public:
  explicit ChildEndpointIterator(
      std::shared_ptr<EndpointAddressesIterator> parent_it)
      : parent_it_(std::move(parent_it)) {}

  void ForEach(absl::FunctionRef<void(const EndpointAddresses&)> callback)
      const override {
    parent_it_->ForEach([&](const EndpointAddresses& endpoint) {
      XdsHealthStatus status = GetEndpointHealthStatus(endpoint);
      if (status.status() != XdsHealthStatus::kDraining) {
        GRPC_TRACE_LOG(xds_override_host_lb, INFO)
            << "[xds_override_host_lb " << this << "] endpoint "
            << endpoint.ToString() << ": not draining, passing to child";
        callback(endpoint);
      }
    });
  }

 private:
  std::shared_ptr<EndpointAddressesIterator> parent_it_;
};

absl::Status XdsOverrideHostLb::UpdateLocked(UpdateArgs args) {
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this << "] Received update";
  // Grab new LB policy config.
  if (args.config == nullptr) {
    return absl::InvalidArgumentError("Missing policy config");
  }
  auto new_config = args.config.TakeAsSubclass<XdsOverrideHostLbConfig>();
  // Get xDS config.
  auto new_xds_config = args.args.GetObjectRef<XdsConfig>();
  if (new_xds_config == nullptr) {
    // Should never happen.
    absl::Status status = absl::InternalError(
        "xDS config not passed to xds_cluster_impl LB policy");
    ReportTransientFailure(status);
    return status;
  }
  auto it = new_xds_config->clusters.find(new_config->cluster_name());
  if (it == new_xds_config->clusters.end() || !it->second.ok() ||
      it->second->cluster == nullptr) {
    // Should never happen.
    absl::Status status = absl::InternalError(absl::StrCat(
        "xDS config has no entry for cluster ", new_config->cluster_name()));
    ReportTransientFailure(status);
    return status;
  }
  args_ = std::move(args.args);
  override_host_status_set_ = it->second->cluster->override_host_statuses;
  connection_idle_timeout_ = it->second->cluster->connection_idle_timeout;
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this
      << "] override host status set: " << override_host_status_set_.ToString()
      << " connection idle timeout: " << connection_idle_timeout_.ToString();
  // Update address map and wrap endpoint iterator for child policy.
  if (args.addresses.ok()) {
    UpdateAddressMap(**args.addresses);
    args.addresses =
        std::make_shared<ChildEndpointIterator>(std::move(*args.addresses));
  } else {
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "[xds_override_host_lb " << this
        << "] address error: " << args.addresses.status();
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
  }
  // Update child policy.
  UpdateArgs update_args;
  update_args.addresses = std::move(args.addresses);
  update_args.resolution_note = std::move(args.resolution_note);
  update_args.config = new_config->child_config();
  update_args.args = args_;
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this << "] Updating child policy handler "
      << child_policy_.get();
  return child_policy_->UpdateLocked(std::move(update_args));
}

void XdsOverrideHostLb::MaybeUpdatePickerLocked() {
  if (picker_ != nullptr) {
    auto xds_override_host_picker = MakeRefCounted<Picker>(
        RefAsSubclass<XdsOverrideHostLb>(), picker_, override_host_status_set_);
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "[xds_override_host_lb " << this
        << "] updating connectivity: state=" << ConnectivityStateName(state_)
        << " status=(" << status_
        << ") picker=" << xds_override_host_picker.get();
    channel_control_helper()->UpdateState(state_, status_,
                                          std::move(xds_override_host_picker));
  }
}

OrphanablePtr<LoadBalancingPolicy> XdsOverrideHostLb::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper = std::make_unique<Helper>(
      RefAsSubclass<XdsOverrideHostLb>(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &xds_override_host_lb_trace);
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this
      << "] Created new child policy handler " << lb_policy.get();
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
        !override_host_status_set_.Contains(status)) {
      GRPC_TRACE_LOG(xds_override_host_lb, INFO)
          << "[xds_override_host_lb " << this << "] endpoint "
          << endpoint.ToString()
          << ": draining but not in override_host_status set -- "
             "ignoring";
      return;
    }
    std::vector<std::string> addresses;
    addresses.reserve(endpoint.addresses().size());
    for (const auto& address : endpoint.addresses()) {
      auto key = grpc_sockaddr_to_string(&address, /*normalize=*/false);
      if (!key.ok()) {
        GRPC_TRACE_LOG(xds_override_host_lb, INFO)
            << "[xds_override_host_lb " << this
            << "] no key for endpoint address; not adding to map";
      } else {
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
  const Timestamp now = Timestamp::Now();
  const Timestamp idle_threshold = now - connection_idle_timeout_;
  Duration next_time = connection_idle_timeout_;
  {
    // Drop subchannel refs after releasing the lock to avoid deadlock.
    std::vector<RefCountedPtr<SubchannelWrapper>> subchannel_refs_to_drop;
    MutexLock lock(&mu_);
    for (auto it = subchannel_map_.begin(); it != subchannel_map_.end();) {
      if (addresses_for_map.find(it->first) == addresses_for_map.end()) {
        GRPC_TRACE_LOG(xds_override_host_lb, INFO)
            << "[xds_override_host_lb " << this << "] removing map key "
            << it->first;
        it->second->UnsetSubchannel(&subchannel_refs_to_drop);
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
        GRPC_TRACE_LOG(xds_override_host_lb, INFO)
            << "[xds_override_host_lb " << this << "] adding map key "
            << address;
        it = subchannel_map_.emplace(address, MakeRefCounted<SubchannelEntry>())
                 .first;
      }
      GRPC_TRACE_LOG(xds_override_host_lb, INFO)
          << "[xds_override_host_lb " << this << "] map key " << address
          << ": setting "
          << "eds_health_status=" << address_info.eds_health_status.ToString()
          << " address_list=" << address_info.address_list.c_str();
      it->second->set_eds_health_status(address_info.eds_health_status);
      it->second->set_address_list(std::move(address_info.address_list));
      // Check the entry's last_used_time to determine the next time at
      // which the timer needs to run.
      if (it->second->last_used_time() > idle_threshold) {
        const Duration next_time_for_entry =
            it->second->last_used_time() + connection_idle_timeout_ - now;
        next_time = std::min(next_time, next_time_for_entry);
      }
    }
  }
  idle_timer_ =
      MakeOrphanable<IdleTimer>(RefAsSubclass<XdsOverrideHostLb>(), next_time);
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::AdoptSubchannel(
    const grpc_resolved_address& address,
    RefCountedPtr<SubchannelInterface> subchannel) {
  auto wrapper = MakeRefCounted<SubchannelWrapper>(
      std::move(subchannel), RefAsSubclass<XdsOverrideHostLb>());
  auto key = grpc_sockaddr_to_string(&address, /*normalize=*/false);
  if (key.ok()) {
    // Drop ref to previously owned subchannel (if any) after releasing
    // the lock.
    RefCountedPtr<SubchannelWrapper> subchannel_ref_to_drop;
    MutexLock lock(&mu_);
    auto it = subchannel_map_.find(*key);
    if (it != subchannel_map_.end()) {
      wrapper->set_subchannel_entry(it->second);
      subchannel_ref_to_drop = it->second->SetUnownedSubchannel(wrapper.get());
    }
  }
  return wrapper;
}

void XdsOverrideHostLb::CreateSubchannelForAddress(absl::string_view address) {
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << this << "] creating owned subchannel for "
      << address;
  auto addr = StringToSockaddr(address);
  CHECK(addr.ok());
  // Note: We don't currently have any cases where per_address_args need to
  // be passed through.  If we encounter any such cases in the future, we
  // will need to change this to store those attributes from the resolver
  // update in the map entry.
  auto subchannel = channel_control_helper()->CreateSubchannel(
      *addr, /*per_address_args=*/ChannelArgs(), args_);
  auto wrapper = MakeRefCounted<SubchannelWrapper>(
      std::move(subchannel), RefAsSubclass<XdsOverrideHostLb>());
  {
    MutexLock lock(&mu_);
    auto it = subchannel_map_.find(address);
    // This can happen if the map entry was removed between the time that
    // the picker requested the subchannel creation and the time that we got
    // here.  In that case, we can just make it a no-op, since the update
    // that removed the entry will have generated a new picker already.
    if (it == subchannel_map_.end()) return;
    // This can happen if the picker requests subchannel creation for
    // the same address multiple times.
    if (it->second->HasOwnedSubchannel()) return;
    wrapper->set_subchannel_entry(it->second);
    it->second->SetOwnedSubchannel(std::move(wrapper));
  }
  MaybeUpdatePickerLocked();
}

void XdsOverrideHostLb::CleanupSubchannels() {
  const Timestamp now = Timestamp::Now();
  const Timestamp idle_threshold = now - connection_idle_timeout_;
  Duration next_time = connection_idle_timeout_;
  std::vector<RefCountedPtr<SubchannelWrapper>> subchannel_refs_to_drop;
  {
    MutexLock lock(&mu_);
    if (subchannel_map_.empty()) return;
    for (const auto& p : subchannel_map_) {
      if (p.second->last_used_time() <= idle_threshold) {
        auto subchannel = p.second->TakeOwnedSubchannel();
        if (subchannel != nullptr) {
          GRPC_TRACE_LOG(xds_override_host_lb, INFO)
              << "[xds_override_host_lb " << this
              << "] dropping subchannel for " << p.first;
          subchannel_refs_to_drop.push_back(std::move(subchannel));
        }
      } else {
        // Not dropping the subchannel.  Check the entry's last_used_time to
        // determine the next time at which the timer needs to run.
        const Duration next_time_for_entry =
            p.second->last_used_time() + connection_idle_timeout_ - now;
        next_time = std::min(next_time, next_time_for_entry);
      }
    }
  }
  idle_timer_ =
      MakeOrphanable<IdleTimer>(RefAsSubclass<XdsOverrideHostLb>(), next_time);
}

//
// XdsOverrideHostLb::Helper
//

RefCountedPtr<SubchannelInterface> XdsOverrideHostLb::Helper::CreateSubchannel(
    const grpc_resolved_address& address, const ChannelArgs& per_address_args,
    const ChannelArgs& args) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_override_host_lb)) {
    auto key = grpc_sockaddr_to_string(&address, /*normalize=*/false);
    LOG(INFO) << "[xds_override_host_lb " << this
              << "] creating subchannel for " << key.value_or("<unknown>")
              << ", per_address_args=" << per_address_args << ", args=" << args;
  }
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
// XdsOverrideHostLb::SubchannelWrapper
//

XdsOverrideHostLb::SubchannelWrapper::SubchannelWrapper(
    RefCountedPtr<SubchannelInterface> subchannel,
    RefCountedPtr<XdsOverrideHostLb> policy)
    : DelegatingSubchannel(std::move(subchannel)), policy_(std::move(policy)) {
  auto watcher = std::make_unique<ConnectivityStateWatcher>(
      WeakRefAsSubclass<SubchannelWrapper>());
  watcher_ = watcher.get();
  wrapped_subchannel()->WatchConnectivityState(std::move(watcher));
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

void XdsOverrideHostLb::SubchannelWrapper::Orphaned() {
  GRPC_TRACE_LOG(xds_override_host_lb, INFO)
      << "[xds_override_host_lb " << policy_.get() << "] subchannel wrapper "
      << this << " orphaned";
  if (!IsWorkSerializerDispatchEnabled()) {
    wrapped_subchannel()->CancelConnectivityStateWatch(watcher_);
    if (subchannel_entry_ != nullptr) {
      MutexLock lock(&policy()->mu_);
      subchannel_entry_->OnSubchannelWrapperOrphan(
          this, policy()->connection_idle_timeout_);
    }
    return;
  }
  policy()->work_serializer()->Run(
      [self = WeakRefAsSubclass<SubchannelWrapper>()]() {
        self->wrapped_subchannel()->CancelConnectivityStateWatch(
            self->watcher_);
        if (self->subchannel_entry_ != nullptr) {
          MutexLock lock(&self->policy()->mu_);
          self->subchannel_entry_->OnSubchannelWrapperOrphan(
              self.get(), self->policy()->connection_idle_timeout_);
        }
      },
      DEBUG_LOCATION);
}

void XdsOverrideHostLb::SubchannelWrapper::UpdateConnectivityState(
    grpc_connectivity_state state, absl::Status status) {
  bool update_picker = false;
  if (subchannel_entry_ != nullptr) {
    MutexLock lock(&policy()->mu_);
    if (subchannel_entry_->connectivity_state() != state) {
      subchannel_entry_->set_connectivity_state(state);
      update_picker = subchannel_entry_->HasOwnedSubchannel() &&
                      subchannel_entry_->GetSubchannel() == this;
    }
  }
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
  if (update_picker) policy()->MaybeUpdatePickerLocked();
}

//
// XdsOverrideHostLb::SubchannelEntry
//

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::SubchannelEntry::SetUnownedSubchannel(
    SubchannelWrapper* subchannel)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
  auto owned_subchannel = TakeOwnedSubchannel();
  subchannel_ = subchannel;
  return owned_subchannel;
}

XdsOverrideHostLb::SubchannelWrapper*
XdsOverrideHostLb::SubchannelEntry::GetSubchannel() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
  return Match(
      subchannel_, [](SubchannelWrapper* subchannel) { return subchannel; },
      [](const RefCountedPtr<SubchannelWrapper>& subchannel) {
        return subchannel.get();
      });
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::SubchannelEntry::GetSubchannelRef() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
  auto* sc = GetSubchannel();
  if (sc == nullptr) return nullptr;
  return sc->RefIfNonZero().TakeAsSubclass<SubchannelWrapper>();
}

RefCountedPtr<XdsOverrideHostLb::SubchannelWrapper>
XdsOverrideHostLb::SubchannelEntry::TakeOwnedSubchannel()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
  return MatchMutable(
      &subchannel_,
      [](SubchannelWrapper**) -> RefCountedPtr<SubchannelWrapper> {
        return nullptr;
      },
      [](RefCountedPtr<SubchannelWrapper>* subchannel) {
        return std::move(*subchannel);
      });
}

void XdsOverrideHostLb::SubchannelEntry::UnsetSubchannel(
    std::vector<RefCountedPtr<SubchannelWrapper>>* owned_subchannels)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
  auto subchannel = TakeOwnedSubchannel();
  if (subchannel != nullptr) {
    owned_subchannels->push_back(std::move(subchannel));
  }
  subchannel_ = nullptr;
}

void XdsOverrideHostLb::SubchannelEntry::OnSubchannelWrapperOrphan(
    SubchannelWrapper* wrapper, Duration connection_idle_timeout)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsOverrideHostLb::mu_) {
  auto* subchannel = GetSubchannel();
  if (subchannel != wrapper) return;
  if (last_used_time_ < (Timestamp::Now() - connection_idle_timeout)) {
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "[xds_override_host_lb] removing unowned subchannel "
           "wrapper "
        << subchannel;
    subchannel_ = nullptr;
  } else {
    // The subchannel is being released by the child policy, but it
    // is still within its idle timeout, so we make a new copy of
    // the wrapper with the same underlying subchannel, and we hold
    // our own ref to it.
    GRPC_TRACE_LOG(xds_override_host_lb, INFO)
        << "[xds_override_host_lb] subchannel wrapper " << subchannel
        << ": cloning to gain ownership";
    subchannel_ = wrapper->Clone();
  }
}

//
// factory
//

class XdsOverrideHostLbFactory final : public LoadBalancingPolicyFactory {
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

//
// XdsOverrideHostLbConfig
//

const JsonLoaderInterface* XdsOverrideHostLbConfig::JsonLoader(
    const JsonArgs&) {
  static const auto kJsonLoader =
      JsonObjectLoader<XdsOverrideHostLbConfig>()
          // Child policy config is parsed in JsonPostLoad
          .Field("clusterName", &XdsOverrideHostLbConfig::cluster_name_)
          .Finish();
  return kJsonLoader;
}

void XdsOverrideHostLbConfig::JsonPostLoad(const Json& json, const JsonArgs&,
                                           ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".childPolicy");
  auto it = json.object().find("childPolicy");
  if (it == json.object().end()) {
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
