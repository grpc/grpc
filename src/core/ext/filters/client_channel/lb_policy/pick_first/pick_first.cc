/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/mutex_lock.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_pick_first_trace(false, "pick_first");

namespace {

//
// pick_first LB policy
//

constexpr char kPickFirst[] = "pick_first";

class PickFirst : public LoadBalancingPolicy {
 public:
  explicit PickFirst(Args args);

  const char* name() const override { return kPickFirst; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;
  void FillChildRefsForChannelz(channelz::ChildRefsList* child_subchannels,
                                channelz::ChildRefsList* ignored) override;

 private:
  ~PickFirst();

  class PickFirstSubchannelList;

  class PickFirstSubchannelData
      : public SubchannelData<PickFirstSubchannelList,
                              PickFirstSubchannelData> {
   public:
    PickFirstSubchannelData(
        SubchannelList<PickFirstSubchannelList, PickFirstSubchannelData>*
            subchannel_list,
        const ServerAddress& address, Subchannel* subchannel,
        grpc_combiner* combiner)
        : SubchannelData(subchannel_list, address, subchannel, combiner) {}

    void ProcessConnectivityChangeLocked(
        grpc_connectivity_state connectivity_state, grpc_error* error) override;

    // Processes the connectivity change to READY for an unselected subchannel.
    void ProcessUnselectedReadyLocked();

    void CheckConnectivityStateAndStartWatchingLocked();
  };

  class PickFirstSubchannelList
      : public SubchannelList<PickFirstSubchannelList,
                              PickFirstSubchannelData> {
   public:
    PickFirstSubchannelList(PickFirst* policy, TraceFlag* tracer,
                            const ServerAddressList& addresses,
                            grpc_combiner* combiner,
                            const grpc_channel_args& args)
        : SubchannelList(policy, tracer, addresses, combiner,
                         policy->channel_control_helper(), args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
    }

    ~PickFirstSubchannelList() {
      PickFirst* p = static_cast<PickFirst*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    bool in_transient_failure() const { return in_transient_failure_; }
    void set_in_transient_failure(bool in_transient_failure) {
      in_transient_failure_ = in_transient_failure;
    }

   private:
    bool in_transient_failure_ = false;
  };

  class Picker : public SubchannelPicker {
   public:
    explicit Picker(RefCountedPtr<ConnectedSubchannel> connected_subchannel)
        : connected_subchannel_(std::move(connected_subchannel)) {}

    PickResult Pick(PickArgs* pick, grpc_error** error) override {
      pick->connected_subchannel = connected_subchannel_;
      return PICK_COMPLETE;
    }

   private:
    RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
  };

  // Helper class to ensure that any function that modifies the child refs
  // data structures will update the channelz snapshot data structures before
  // returning.
  class AutoChildRefsUpdater {
   public:
    explicit AutoChildRefsUpdater(PickFirst* pf) : pf_(pf) {}
    ~AutoChildRefsUpdater() { pf_->UpdateChildRefsLocked(); }

   private:
    PickFirst* pf_;
  };

  void ShutdownLocked() override;

  void UpdateChildRefsLocked();

  // All our subchannels.
  OrphanablePtr<PickFirstSubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  OrphanablePtr<PickFirstSubchannelList> latest_pending_subchannel_list_;
  // Selected subchannel in \a subchannel_list_.
  PickFirstSubchannelData* selected_ = nullptr;
  // Are we in IDLE state?
  bool idle_ = false;
  // Are we shut down?
  bool shutdown_ = false;

  /// Lock and data used to capture snapshots of this channels child
  /// channels and subchannels. This data is consumed by channelz.
  gpr_mu child_refs_mu_;
  channelz::ChildRefsList child_subchannels_;
  channelz::ChildRefsList child_channels_;
};

PickFirst::PickFirst(Args args) : LoadBalancingPolicy(std::move(args)) {
  gpr_mu_init(&child_refs_mu_);
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Pick First %p created.", this);
  }
}

PickFirst::~PickFirst() {
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Destroying Pick First %p", this);
  }
  gpr_mu_destroy(&child_refs_mu_);
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
}

void PickFirst::ShutdownLocked() {
  AutoChildRefsUpdater guard(this);
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Pick First %p Shutting down", this);
  }
  shutdown_ = true;
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
}

void PickFirst::ExitIdleLocked() {
  if (idle_) {
    idle_ = false;
    if (subchannel_list_ == nullptr ||
        subchannel_list_->num_subchannels() == 0) {
      grpc_error* error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("No addresses to connect to");
      channel_control_helper()->UpdateState(
          GRPC_CHANNEL_TRANSIENT_FAILURE, GRPC_ERROR_REF(error),
          UniquePtr<SubchannelPicker>(New<TransientFailurePicker>(error)));
    } else {
      subchannel_list_->subchannel(0)
          ->CheckConnectivityStateAndStartWatchingLocked();
    }
  }
}

void PickFirst::ResetBackoffLocked() {
  subchannel_list_->ResetBackoffLocked();
  if (latest_pending_subchannel_list_ != nullptr) {
    latest_pending_subchannel_list_->ResetBackoffLocked();
  }
}

void PickFirst::FillChildRefsForChannelz(
    channelz::ChildRefsList* child_subchannels_to_fill,
    channelz::ChildRefsList* ignored) {
  MutexLock lock(&child_refs_mu_);
  for (size_t i = 0; i < child_subchannels_.size(); ++i) {
    // TODO(ncteisen): implement a de dup loop that is not O(n^2). Might
    // have to implement lightweight set. For now, we don't care about
    // performance when channelz requests are made.
    bool found = false;
    for (size_t j = 0; j < child_subchannels_to_fill->size(); ++j) {
      if ((*child_subchannels_to_fill)[j] == child_subchannels_[i]) {
        found = true;
        break;
      }
    }
    if (!found) {
      child_subchannels_to_fill->push_back(child_subchannels_[i]);
    }
  }
}

void PickFirst::UpdateChildRefsLocked() {
  channelz::ChildRefsList cs;
  if (subchannel_list_ != nullptr) {
    subchannel_list_->PopulateChildRefsList(&cs);
  }
  if (latest_pending_subchannel_list_ != nullptr) {
    latest_pending_subchannel_list_->PopulateChildRefsList(&cs);
  }
  // atomically update the data that channelz will actually be looking at.
  MutexLock lock(&child_refs_mu_);
  child_subchannels_ = std::move(cs);
}

void PickFirst::UpdateLocked(UpdateArgs args) {
  AutoChildRefsUpdater guard(this);
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO,
            "Pick First %p received update with %" PRIuPTR " addresses", this,
            args.addresses.size());
  }
  grpc_arg new_arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1);
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add(args.args, &new_arg, 1);
  auto subchannel_list = MakeOrphanable<PickFirstSubchannelList>(
      this, &grpc_lb_pick_first_trace, args.addresses, combiner(), *new_args);
  grpc_channel_args_destroy(new_args);
  if (subchannel_list->num_subchannels() == 0) {
    // Empty update or no valid subchannels. Unsubscribe from all current
    // subchannels.
    subchannel_list_ = std::move(subchannel_list);  // Empty list.
    selected_ = nullptr;
    // If not idle, put the channel in TRANSIENT_FAILURE.
    // (If we are idle, then this will happen in ExitIdleLocked() if we
    // haven't gotten a non-empty update by the time the application tries
    // to start a new call.)
    if (!idle_) {
      grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update");
      channel_control_helper()->UpdateState(
          GRPC_CHANNEL_TRANSIENT_FAILURE, GRPC_ERROR_REF(error),
          UniquePtr<SubchannelPicker>(New<TransientFailurePicker>(error)));
    }
    return;
  }
  // If one of the subchannels in the new list is already in state
  // READY, then select it immediately.  This can happen when the
  // currently selected subchannel is also present in the update.  It
  // can also happen if one of the subchannels in the update is already
  // in the global subchannel pool because it's in use by another channel.
  // TODO(roth): If we're in IDLE state, we should probably defer this
  // check and instead do it in ExitIdleLocked().
  for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
    PickFirstSubchannelData* sd = subchannel_list->subchannel(i);
    grpc_error* error = GRPC_ERROR_NONE;
    grpc_connectivity_state state = sd->CheckConnectivityStateLocked(&error);
    GRPC_ERROR_UNREF(error);
    if (state == GRPC_CHANNEL_READY) {
      subchannel_list_ = std::move(subchannel_list);
      sd->StartConnectivityWatchLocked();
      sd->ProcessUnselectedReadyLocked();
      // If there was a previously pending update (which may or may
      // not have contained the currently selected subchannel), drop
      // it, so that it doesn't override what we've done here.
      latest_pending_subchannel_list_.reset();
      // Make sure that subsequent calls to ExitIdleLocked() don't cause
      // us to start watching a subchannel other than the one we've
      // selected.
      idle_ = false;
      return;
    }
  }
  if (selected_ == nullptr) {
    // We don't yet have a selected subchannel, so replace the current
    // subchannel list immediately.
    subchannel_list_ = std::move(subchannel_list);
    // If we're not in IDLE state, start trying to connect to the first
    // subchannel in the new list.
    if (!idle_) {
      // Note: No need to use CheckConnectivityStateAndStartWatchingLocked()
      // here, since we've already checked the initial connectivity
      // state of all subchannels above.
      subchannel_list_->subchannel(0)->StartConnectivityWatchLocked();
    }
  } else {
    // We do have a selected subchannel (which means it's READY), so keep
    // using it until one of the subchannels in the new list reports READY.
    if (latest_pending_subchannel_list_ != nullptr) {
      if (grpc_lb_pick_first_trace.enabled()) {
        gpr_log(GPR_INFO,
                "Pick First %p Shutting down latest pending subchannel list "
                "%p, about to be replaced by newer latest %p",
                this, latest_pending_subchannel_list_.get(),
                subchannel_list.get());
      }
    }
    latest_pending_subchannel_list_ = std::move(subchannel_list);
    // If we're not in IDLE state, start trying to connect to the first
    // subchannel in the new list.
    if (!idle_) {
      // Note: No need to use CheckConnectivityStateAndStartWatchingLocked()
      // here, since we've already checked the initial connectivity
      // state of all subchannels above.
      latest_pending_subchannel_list_->subchannel(0)
          ->StartConnectivityWatchLocked();
    }
  }
}

void PickFirst::PickFirstSubchannelData::ProcessConnectivityChangeLocked(
    grpc_connectivity_state connectivity_state, grpc_error* error) {
  PickFirst* p = static_cast<PickFirst*>(subchannel_list()->policy());
  AutoChildRefsUpdater guard(p);
  // The notification must be for a subchannel in either the current or
  // latest pending subchannel lists.
  GPR_ASSERT(subchannel_list() == p->subchannel_list_.get() ||
             subchannel_list() == p->latest_pending_subchannel_list_.get());
  GPR_ASSERT(connectivity_state != GRPC_CHANNEL_SHUTDOWN);
  // Handle updates for the currently selected subchannel.
  if (p->selected_ == this) {
    if (grpc_lb_pick_first_trace.enabled()) {
      gpr_log(GPR_INFO,
              "Pick First %p selected subchannel connectivity changed to %s", p,
              grpc_connectivity_state_name(connectivity_state));
    }
    // If the new state is anything other than READY and there is a
    // pending update, switch to the pending update.
    if (connectivity_state != GRPC_CHANNEL_READY &&
        p->latest_pending_subchannel_list_ != nullptr) {
      if (grpc_lb_pick_first_trace.enabled()) {
        gpr_log(GPR_INFO,
                "Pick First %p promoting pending subchannel list %p to "
                "replace %p",
                p, p->latest_pending_subchannel_list_.get(),
                p->subchannel_list_.get());
      }
      p->selected_ = nullptr;
      StopConnectivityWatchLocked();
      p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
      // Set our state to that of the pending subchannel list.
      if (p->subchannel_list_->in_transient_failure()) {
        grpc_error* new_error =
            GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                "selected subchannel failed; switching to pending update",
                &error, 1);
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_TRANSIENT_FAILURE, GRPC_ERROR_REF(new_error),
            UniquePtr<SubchannelPicker>(
                New<TransientFailurePicker>(new_error)));
      } else {
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE,
            UniquePtr<SubchannelPicker>(New<QueuePicker>(p->Ref())));
      }
    } else {
      if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        // If the selected subchannel goes bad, request a re-resolution. We
        // also set the channel state to IDLE. The reason is that if the new
        // state is TRANSIENT_FAILURE due to a GOAWAY reception we don't want
        // to connect to the re-resolved backends until we leave IDLE state.
        p->idle_ = true;
        p->channel_control_helper()->RequestReresolution();
        p->selected_ = nullptr;
        StopConnectivityWatchLocked();
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_IDLE, GRPC_ERROR_NONE,
            UniquePtr<SubchannelPicker>(New<QueuePicker>(p->Ref())));
      } else {
        // This is unlikely but can happen when a subchannel has been asked
        // to reconnect by a different channel and this channel has dropped
        // some connectivity state notifications.
        if (connectivity_state == GRPC_CHANNEL_READY) {
          p->channel_control_helper()->UpdateState(
              GRPC_CHANNEL_READY, GRPC_ERROR_NONE,
              UniquePtr<SubchannelPicker>(
                  New<Picker>(connected_subchannel()->Ref())));
        } else {  // CONNECTING
          p->channel_control_helper()->UpdateState(
              connectivity_state, GRPC_ERROR_REF(error),
              UniquePtr<SubchannelPicker>(New<QueuePicker>(p->Ref())));
        }
        // Renew notification.
        RenewConnectivityWatchLocked();
      }
    }
    GRPC_ERROR_UNREF(error);
    return;
  }
  // If we get here, there are two possible cases:
  // 1. We do not currently have a selected subchannel, and the update is
  //    for a subchannel in p->subchannel_list_ that we're trying to
  //    connect to.  The goal here is to find a subchannel that we can
  //    select.
  // 2. We do currently have a selected subchannel, and the update is
  //    for a subchannel in p->latest_pending_subchannel_list_.  The
  //    goal here is to find a subchannel from the update that we can
  //    select in place of the current one.
  subchannel_list()->set_in_transient_failure(false);
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY: {
      // Renew notification.
      RenewConnectivityWatchLocked();
      ProcessUnselectedReadyLocked();
      break;
    }
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      StopConnectivityWatchLocked();
      PickFirstSubchannelData* sd = this;
      size_t next_index =
          (sd->Index() + 1) % subchannel_list()->num_subchannels();
      sd = subchannel_list()->subchannel(next_index);
      // If we're tried all subchannels, set state to TRANSIENT_FAILURE.
      if (sd->Index() == 0) {
        // Re-resolve if this is the most recent subchannel list.
        if (subchannel_list() == (p->latest_pending_subchannel_list_ != nullptr
                                      ? p->latest_pending_subchannel_list_.get()
                                      : p->subchannel_list_.get())) {
          p->channel_control_helper()->RequestReresolution();
        }
        subchannel_list()->set_in_transient_failure(true);
        // Only report new state in case 1.
        if (subchannel_list() == p->subchannel_list_.get()) {
          grpc_error* new_error =
              GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                  "failed to connect to all addresses", &error, 1);
          p->channel_control_helper()->UpdateState(
              GRPC_CHANNEL_TRANSIENT_FAILURE, GRPC_ERROR_REF(new_error),
              UniquePtr<SubchannelPicker>(
                  New<TransientFailurePicker>(new_error)));
        }
      }
      sd->CheckConnectivityStateAndStartWatchingLocked();
      break;
    }
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE: {
      // Only update connectivity state in case 1.
      if (subchannel_list() == p->subchannel_list_.get()) {
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE,
            UniquePtr<SubchannelPicker>(New<QueuePicker>(p->Ref())));
      }
      // Renew notification.
      RenewConnectivityWatchLocked();
      break;
    }
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_UNREACHABLE_CODE(break);
  }
  GRPC_ERROR_UNREF(error);
}

void PickFirst::PickFirstSubchannelData::ProcessUnselectedReadyLocked() {
  PickFirst* p = static_cast<PickFirst*>(subchannel_list()->policy());
  // If we get here, there are two possible cases:
  // 1. We do not currently have a selected subchannel, and the update is
  //    for a subchannel in p->subchannel_list_ that we're trying to
  //    connect to.  The goal here is to find a subchannel that we can
  //    select.
  // 2. We do currently have a selected subchannel, and the update is
  //    for a subchannel in p->latest_pending_subchannel_list_.  The
  //    goal here is to find a subchannel from the update that we can
  //    select in place of the current one.
  GPR_ASSERT(subchannel_list() == p->subchannel_list_.get() ||
             subchannel_list() == p->latest_pending_subchannel_list_.get());
  // Case 2.  Promote p->latest_pending_subchannel_list_ to p->subchannel_list_.
  if (subchannel_list() == p->latest_pending_subchannel_list_.get()) {
    if (grpc_lb_pick_first_trace.enabled()) {
      gpr_log(GPR_INFO,
              "Pick First %p promoting pending subchannel list %p to "
              "replace %p",
              p, p->latest_pending_subchannel_list_.get(),
              p->subchannel_list_.get());
    }
    p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
  }
  // Cases 1 and 2.
  p->selected_ = this;
  p->channel_control_helper()->UpdateState(
      GRPC_CHANNEL_READY, GRPC_ERROR_NONE,
      UniquePtr<SubchannelPicker>(New<Picker>(connected_subchannel()->Ref())));
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Pick First %p selected subchannel %p", p, subchannel());
  }
}

void PickFirst::PickFirstSubchannelData::
    CheckConnectivityStateAndStartWatchingLocked() {
  PickFirst* p = static_cast<PickFirst*>(subchannel_list()->policy());
  // Check current state.
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_connectivity_state current_state = CheckConnectivityStateLocked(&error);
  GRPC_ERROR_UNREF(error);
  // Start watch.
  StartConnectivityWatchLocked();
  // If current state is READY, select the subchannel now, since we started
  // watching from this state and will not get a notification of it
  // transitioning into this state.
  if (p->selected_ != this && current_state == GRPC_CHANNEL_READY) {
    ProcessUnselectedReadyLocked();
  }
}

//
// factory
//

class PickFirstFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return OrphanablePtr<LoadBalancingPolicy>(New<PickFirst>(std::move(args)));
  }

  const char* name() const override { return kPickFirst; }
};

}  // namespace

}  // namespace grpc_core

void grpc_lb_policy_pick_first_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::UniquePtr<grpc_core::LoadBalancingPolicyFactory>(
              grpc_core::New<grpc_core::PickFirstFactory>()));
}

void grpc_lb_policy_pick_first_shutdown() {}
