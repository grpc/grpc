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
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_pick_first_trace(false, "pick_first");

namespace {

//
// pick_first LB policy
//

class PickFirst : public LoadBalancingPolicy {
 public:
  explicit PickFirst(const Args& args);

  void UpdateLocked(const grpc_channel_args& args) override;
  bool PickLocked(PickState* pick) override;
  void CancelPickLocked(PickState* pick, grpc_error* error) override;
  void CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
                                 uint32_t initial_metadata_flags_eq,
                                 grpc_error* error) override;
  void NotifyOnStateChangeLocked(grpc_connectivity_state* state,
                                 grpc_closure* closure) override;
  grpc_connectivity_state CheckConnectivityLocked(
      grpc_error** connectivity_error) override;
  void HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) override;
  void PingOneLocked(grpc_closure* on_initiate, grpc_closure* on_ack) override;
  void ExitIdleLocked() override;
  void FillChildRefsForChannelz(ChildRefsList* child_subchannels,
                                ChildRefsList* child_channels) override;

 private:
  ~PickFirst();

  class PickFirstSubchannelList;

  class PickFirstSubchannelData
      : public SubchannelData<PickFirstSubchannelList,
                              PickFirstSubchannelData> {
   public:
    PickFirstSubchannelData(PickFirstSubchannelList* subchannel_list,
                            const grpc_lb_user_data_vtable* user_data_vtable,
                            const grpc_lb_address& address,
                            grpc_subchannel* subchannel,
                            grpc_combiner* combiner)
        : SubchannelData(subchannel_list, user_data_vtable, address, subchannel,
                         combiner) {}

    void ProcessConnectivityChangeLocked(
        grpc_connectivity_state connectivity_state, grpc_error* error) override;
  };

  class PickFirstSubchannelList
      : public SubchannelList<PickFirstSubchannelList,
                              PickFirstSubchannelData> {
   public:
    PickFirstSubchannelList(PickFirst* policy, TraceFlag* tracer,
                            const grpc_lb_addresses* addresses,
                            grpc_combiner* combiner,
                            grpc_client_channel_factory* client_channel_factory,
                            const grpc_channel_args& args)
        : SubchannelList(policy, tracer, addresses, combiner,
                         client_channel_factory, args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
    }

    ~PickFirstSubchannelList() {
      PickFirst* p = static_cast<PickFirst*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }
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

  void StartPickingLocked();
  void DestroyUnselectedSubchannelsLocked();
  void UpdateChildRefsLocked();

  // All our subchannels.
  OrphanablePtr<PickFirstSubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  OrphanablePtr<PickFirstSubchannelList> latest_pending_subchannel_list_;
  // Selected subchannel in \a subchannel_list_.
  PickFirstSubchannelData* selected_ = nullptr;
  // Have we started picking?
  bool started_picking_ = false;
  // Are we shut down?
  bool shutdown_ = false;
  // List of picks that are waiting on connectivity.
  PickState* pending_picks_ = nullptr;
  // Our connectivity state tracker.
  grpc_connectivity_state_tracker state_tracker_;

  /// Lock and data used to capture snapshots of this channels child
  /// channels and subchannels. This data is consumed by channelz.
  gpr_mu child_refs_mu_;
  ChildRefsList child_subchannels_;
  ChildRefsList child_channels_;
};

PickFirst::PickFirst(const Args& args) : LoadBalancingPolicy(args) {
  GPR_ASSERT(args.client_channel_factory != nullptr);
  gpr_mu_init(&child_refs_mu_);
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "pick_first");
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Pick First %p created.", this);
  }
  UpdateLocked(*args.args);
  grpc_subchannel_index_ref();
}

PickFirst::~PickFirst() {
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Destroying Pick First %p", this);
  }
  gpr_mu_destroy(&child_refs_mu_);
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
  GPR_ASSERT(pending_picks_ == nullptr);
  grpc_connectivity_state_destroy(&state_tracker_);
  grpc_subchannel_index_unref();
}

void PickFirst::HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) {
  PickState* pick;
  while ((pick = pending_picks_) != nullptr) {
    pending_picks_ = pick->next;
    if (new_policy->PickLocked(pick)) {
      // Synchronous return, schedule closure.
      GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
    }
  }
}

void PickFirst::ShutdownLocked() {
  AutoChildRefsUpdater(this);
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO, "Pick First %p Shutting down", this);
  }
  shutdown_ = true;
  PickState* pick;
  while ((pick = pending_picks_) != nullptr) {
    pending_picks_ = pick->next;
    pick->connected_subchannel.reset();
    GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_REF(error));
  }
  grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_SHUTDOWN,
                              GRPC_ERROR_REF(error), "shutdown");
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
  TryReresolutionLocked(&grpc_lb_pick_first_trace, GRPC_ERROR_CANCELLED);
  GRPC_ERROR_UNREF(error);
}

void PickFirst::CancelPickLocked(PickState* pick, grpc_error* error) {
  PickState* pp = pending_picks_;
  pending_picks_ = nullptr;
  while (pp != nullptr) {
    PickState* next = pp->next;
    if (pp == pick) {
      pick->connected_subchannel.reset();
      GRPC_CLOSURE_SCHED(pick->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pp->next = pending_picks_;
      pending_picks_ = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

void PickFirst::CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
                                          uint32_t initial_metadata_flags_eq,
                                          grpc_error* error) {
  PickState* pick = pending_picks_;
  pending_picks_ = nullptr;
  while (pick != nullptr) {
    PickState* next = pick->next;
    if ((pick->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      GRPC_CLOSURE_SCHED(pick->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
    } else {
      pick->next = pending_picks_;
      pending_picks_ = pick;
    }
    pick = next;
  }
  GRPC_ERROR_UNREF(error);
}

void PickFirst::StartPickingLocked() {
  started_picking_ = true;
  if (subchannel_list_ != nullptr) {
    for (size_t i = 0; i < subchannel_list_->num_subchannels(); ++i) {
      if (subchannel_list_->subchannel(i)->subchannel() != nullptr) {
        subchannel_list_->subchannel(i)->StartConnectivityWatchLocked();
        break;
      }
    }
  }
}

void PickFirst::ExitIdleLocked() {
  if (!started_picking_) {
    StartPickingLocked();
  }
}

bool PickFirst::PickLocked(PickState* pick) {
  // If we have a selected subchannel already, return synchronously.
  if (selected_ != nullptr) {
    pick->connected_subchannel = selected_->connected_subchannel()->Ref();
    return true;
  }
  // No subchannel selected yet, so handle asynchronously.
  if (!started_picking_) {
    StartPickingLocked();
  }
  pick->next = pending_picks_;
  pending_picks_ = pick;
  return false;
}

void PickFirst::DestroyUnselectedSubchannelsLocked() {
  for (size_t i = 0; i < subchannel_list_->num_subchannels(); ++i) {
    PickFirstSubchannelData* sd = subchannel_list_->subchannel(i);
    if (selected_ != sd) {
      sd->UnrefSubchannelLocked("selected_different_subchannel");
    }
  }
}

grpc_connectivity_state PickFirst::CheckConnectivityLocked(grpc_error** error) {
  return grpc_connectivity_state_get(&state_tracker_, error);
}

void PickFirst::NotifyOnStateChangeLocked(grpc_connectivity_state* current,
                                          grpc_closure* notify) {
  grpc_connectivity_state_notify_on_state_change(&state_tracker_, current,
                                                 notify);
}

void PickFirst::PingOneLocked(grpc_closure* on_initiate, grpc_closure* on_ack) {
  if (selected_ != nullptr) {
    selected_->connected_subchannel()->Ping(on_initiate, on_ack);
  } else {
    GRPC_CLOSURE_SCHED(on_initiate,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Not connected"));
    GRPC_CLOSURE_SCHED(on_ack,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Not connected"));
  }
}

void PickFirst::FillChildRefsForChannelz(
    ChildRefsList* child_subchannels_to_fill, ChildRefsList* ignored) {
  mu_guard guard(&child_refs_mu_);
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
  ChildRefsList cs;
  if (subchannel_list_ != nullptr) {
    for (size_t i = 0; i < subchannel_list_->num_subchannels(); ++i) {
      if (subchannel_list_->subchannel(i)->subchannel() != nullptr) {
        grpc_core::channelz::SubchannelNode* subchannel_node =
            grpc_subchannel_get_channelz_node(
                subchannel_list_->subchannel(i)->subchannel());
        if (subchannel_node != nullptr) {
          cs.push_back(subchannel_node->subchannel_uuid());
        }
      }
    }
  }
  if (latest_pending_subchannel_list_ != nullptr) {
    for (size_t i = 0; i < latest_pending_subchannel_list_->num_subchannels();
         ++i) {
      if (latest_pending_subchannel_list_->subchannel(i)->subchannel() !=
          nullptr) {
        grpc_core::channelz::SubchannelNode* subchannel_node =
            grpc_subchannel_get_channelz_node(
                latest_pending_subchannel_list_->subchannel(i)->subchannel());
        if (subchannel_node != nullptr) {
          cs.push_back(subchannel_node->subchannel_uuid());
        }
      }
    }
  }
  // atomically update the data that channelz will actually be looking at.
  mu_guard guard(&child_refs_mu_);
  child_subchannels_ = std::move(cs);
}

void PickFirst::UpdateLocked(const grpc_channel_args& args) {
  AutoChildRefsUpdater guard(this);
  const grpc_arg* arg = grpc_channel_args_find(&args, GRPC_ARG_LB_ADDRESSES);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
    if (subchannel_list_ == nullptr) {
      // If we don't have a current subchannel list, go into TRANSIENT FAILURE.
      grpc_connectivity_state_set(
          &state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "pf_update_missing");
    } else {
      // otherwise, keep using the current subchannel list (ignore this update).
      gpr_log(GPR_ERROR,
              "No valid LB addresses channel arg for Pick First %p update, "
              "ignoring.",
              this);
    }
    return;
  }
  const grpc_lb_addresses* addresses =
      static_cast<const grpc_lb_addresses*>(arg->value.pointer.p);
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO,
            "Pick First %p received update with %" PRIuPTR " addresses", this,
            addresses->num_addresses);
  }
  auto subchannel_list = MakeOrphanable<PickFirstSubchannelList>(
      this, &grpc_lb_pick_first_trace, addresses, combiner(),
      client_channel_factory(), args);
  if (subchannel_list->num_subchannels() == 0) {
    // Empty update or no valid subchannels. Unsubscribe from all current
    // subchannels and put the channel in TRANSIENT_FAILURE.
    grpc_connectivity_state_set(
        &state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
        "pf_update_empty");
    subchannel_list_ = std::move(subchannel_list);  // Empty list.
    selected_ = nullptr;
    return;
  }
  if (selected_ == nullptr) {
    // We don't yet have a selected subchannel, so replace the current
    // subchannel list immediately.
    subchannel_list_ = std::move(subchannel_list);
    // If we've started picking, start trying to connect to the first
    // subchannel in the new list.
    if (started_picking_) {
      subchannel_list_->subchannel(0)->StartConnectivityWatchLocked();
    }
  } else {
    // We do have a selected subchannel.
    // Check if it's present in the new list.  If so, we're done.
    for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
      PickFirstSubchannelData* sd = subchannel_list->subchannel(i);
      if (sd->subchannel() == selected_->subchannel()) {
        // The currently selected subchannel is in the update: we are done.
        if (grpc_lb_pick_first_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "Pick First %p found already selected subchannel %p "
                  "at update index %" PRIuPTR " of %" PRIuPTR "; update done",
                  this, selected_->subchannel(), i,
                  subchannel_list->num_subchannels());
        }
        // Make sure it's in state READY.  It might not be if we grabbed
        // the combiner while a connectivity state notification
        // informing us otherwise is pending.
        // Note that CheckConnectivityStateLocked() also takes a ref to
        // the connected subchannel.
        grpc_error* error = GRPC_ERROR_NONE;
        if (sd->CheckConnectivityStateLocked(&error) == GRPC_CHANNEL_READY) {
          selected_ = sd;
          subchannel_list_ = std::move(subchannel_list);
          DestroyUnselectedSubchannelsLocked();
          sd->StartConnectivityWatchLocked();
          // If there was a previously pending update (which may or may
          // not have contained the currently selected subchannel), drop
          // it, so that it doesn't override what we've done here.
          latest_pending_subchannel_list_.reset();
          return;
        }
        GRPC_ERROR_UNREF(error);
      }
    }
    // Not keeping the previous selected subchannel, so set the latest
    // pending subchannel list to the new subchannel list.  We will wait
    // for it to report READY before swapping it into the current
    // subchannel list.
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
    // If we've started picking, start trying to connect to the first
    // subchannel in the new list.
    if (started_picking_) {
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
  // Handle updates for the currently selected subchannel.
  if (p->selected_ == this) {
    if (grpc_lb_pick_first_trace.enabled()) {
      gpr_log(GPR_INFO,
              "Pick First %p connectivity changed for selected subchannel", p);
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
      grpc_connectivity_state_set(
          &p->state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
          error != GRPC_ERROR_NONE
              ? GRPC_ERROR_REF(error)
              : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "selected subchannel not ready; switching to pending "
                    "update"),
          "selected_not_ready+switch_to_update");
    } else {
      // TODO(juanlishen): we re-resolve when the selected subchannel goes to
      // TRANSIENT_FAILURE because we used to shut down in this case before
      // re-resolution is introduced. But we need to investigate whether we
      // really want to take any action instead of waiting for the selected
      // subchannel reconnecting.
      GPR_ASSERT(connectivity_state != GRPC_CHANNEL_SHUTDOWN);
      if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        // If the selected channel goes bad, request a re-resolution.
        grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_IDLE,
                                    GRPC_ERROR_NONE,
                                    "selected_changed+reresolve");
        p->started_picking_ = false;
        p->TryReresolutionLocked(&grpc_lb_pick_first_trace, GRPC_ERROR_NONE);
        // In transient failure. Rely on re-resolution to recover.
        p->selected_ = nullptr;
        UnrefSubchannelLocked("pf_selected_shutdown");
        StopConnectivityWatchLocked();
      } else {
        grpc_connectivity_state_set(&p->state_tracker_, connectivity_state,
                                    GRPC_ERROR_REF(error), "selected_changed");
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
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY: {
      // Case 2.  Promote p->latest_pending_subchannel_list_ to
      // p->subchannel_list_.
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
      grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_READY,
                                  GRPC_ERROR_NONE, "connecting_ready");
      p->selected_ = this;
      if (grpc_lb_pick_first_trace.enabled()) {
        gpr_log(GPR_INFO, "Pick First %p selected subchannel %p", p,
                subchannel());
      }
      // Drop all other subchannels, since we are now connected.
      p->DestroyUnselectedSubchannelsLocked();
      // Update any calls that were waiting for a pick.
      PickState* pick;
      while ((pick = p->pending_picks_)) {
        p->pending_picks_ = pick->next;
        pick->connected_subchannel =
            p->selected_->connected_subchannel()->Ref();
        if (grpc_lb_pick_first_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "Servicing pending pick with selected subchannel %p",
                  p->selected_->subchannel());
        }
        GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
      }
      // Renew notification.
      RenewConnectivityWatchLocked();
      break;
    }
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      StopConnectivityWatchLocked();
      PickFirstSubchannelData* sd = this;
      do {
        size_t next_index =
            (sd->Index() + 1) % subchannel_list()->num_subchannels();
        sd = subchannel_list()->subchannel(next_index);
      } while (sd->subchannel() == nullptr);
      // Case 1: Only set state to TRANSIENT_FAILURE if we've tried
      // all subchannels.
      if (sd->Index() == 0 && subchannel_list() == p->subchannel_list_.get()) {
        grpc_connectivity_state_set(
            &p->state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
            GRPC_ERROR_REF(error), "connecting_transient_failure");
      }
      sd->StartConnectivityWatchLocked();
      break;
    }
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE: {
      // Only update connectivity state in case 1.
      if (subchannel_list() == p->subchannel_list_.get()) {
        grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_CONNECTING,
                                    GRPC_ERROR_REF(error),
                                    "connecting_changed");
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

//
// factory
//

class PickFirstFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      const LoadBalancingPolicy::Args& args) const override {
    return OrphanablePtr<LoadBalancingPolicy>(New<PickFirst>(args));
  }

  const char* name() const override { return "pick_first"; }
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
