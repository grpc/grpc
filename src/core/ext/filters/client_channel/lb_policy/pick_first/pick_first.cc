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

 private:
  ~PickFirst();

  void ShutdownLocked() override;

  void StartPickingLocked();
  void DestroyUnselectedSubchannelsLocked();

  static void OnConnectivityChangedLocked(void* arg, grpc_error* error);

  void SubchannelListRefForConnectivityWatch(
      grpc_lb_subchannel_list* subchannel_list, const char* reason);
  void SubchannelListUnrefForConnectivityWatch(
      grpc_lb_subchannel_list* subchannel_list, const char* reason);

  /** all our subchannels */
  grpc_lb_subchannel_list* subchannel_list_ = nullptr;
  /** latest pending subchannel list */
  grpc_lb_subchannel_list* latest_pending_subchannel_list_ = nullptr;
  /** selected subchannel in \a subchannel_list */
  grpc_lb_subchannel_data* selected_ = nullptr;
  /** have we started picking? */
  bool started_picking_ = false;
  /** are we shut down? */
  bool shutdown_ = false;
  /** list of picks that are waiting on connectivity */
  PickState* pending_picks_ = nullptr;
  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker_;
};

PickFirst::PickFirst(const Args& args) : LoadBalancingPolicy(args) {
  GPR_ASSERT(args.client_channel_factory != nullptr);
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "pick_first");
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Pick First %p created.", this);
  }
  UpdateLocked(*args.args);
  grpc_subchannel_index_ref();
}

PickFirst::~PickFirst() {
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Destroying Pick First %p", this);
  }
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
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG, "Pick First %p Shutting down", this);
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
  if (subchannel_list_ != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(subchannel_list_, "pf_shutdown");
    subchannel_list_ = nullptr;
  }
  if (latest_pending_subchannel_list_ != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(latest_pending_subchannel_list_,
                                               "pf_shutdown");
    latest_pending_subchannel_list_ = nullptr;
  }
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
  if (subchannel_list_ != nullptr && subchannel_list_->num_subchannels > 0) {
    subchannel_list_->checking_subchannel = 0;
    for (size_t i = 0; i < subchannel_list_->num_subchannels; ++i) {
      if (subchannel_list_->subchannels[i].subchannel != nullptr) {
        SubchannelListRefForConnectivityWatch(
            subchannel_list_, "connectivity_watch+start_picking");
        grpc_lb_subchannel_data_start_connectivity_watch(
            &subchannel_list_->subchannels[i]);
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
    pick->connected_subchannel = selected_->connected_subchannel;
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
  for (size_t i = 0; i < subchannel_list_->num_subchannels; ++i) {
    grpc_lb_subchannel_data* sd = &subchannel_list_->subchannels[i];
    if (selected_ != sd) {
      grpc_lb_subchannel_data_unref_subchannel(sd,
                                               "selected_different_subchannel");
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
    selected_->connected_subchannel->Ping(on_initiate, on_ack);
  } else {
    GRPC_CLOSURE_SCHED(on_initiate,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Not connected"));
    GRPC_CLOSURE_SCHED(on_ack,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Not connected"));
  }
}

void PickFirst::SubchannelListRefForConnectivityWatch(
    grpc_lb_subchannel_list* subchannel_list, const char* reason) {
  // TODO(roth): We currently track this ref manually.  Once the new
  // ClosureRef API is ready and the subchannel_list code has been
  // converted to a C++ API, find a way to hold the RefCountedPtr<>
  // somewhere (maybe in the subchannel_data object) instead of doing
  // this manually.
  auto self = Ref(DEBUG_LOCATION, reason);
  self.release();
  grpc_lb_subchannel_list_ref(subchannel_list, reason);
}

void PickFirst::SubchannelListUnrefForConnectivityWatch(
    grpc_lb_subchannel_list* subchannel_list, const char* reason) {
  Unref(DEBUG_LOCATION, reason);
  grpc_lb_subchannel_list_unref(subchannel_list, reason);
}

void PickFirst::UpdateLocked(const grpc_channel_args& args) {
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
      (const grpc_lb_addresses*)arg->value.pointer.p;
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_INFO,
            "Pick First %p received update with %" PRIuPTR " addresses", this,
            addresses->num_addresses);
  }
  grpc_lb_subchannel_list* subchannel_list = grpc_lb_subchannel_list_create(
      this, &grpc_lb_pick_first_trace, addresses, combiner(),
      client_channel_factory(), args, &PickFirst::OnConnectivityChangedLocked);
  if (subchannel_list->num_subchannels == 0) {
    // Empty update or no valid subchannels. Unsubscribe from all current
    // subchannels and put the channel in TRANSIENT_FAILURE.
    grpc_connectivity_state_set(
        &state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
        "pf_update_empty");
    if (subchannel_list_ != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(subchannel_list_,
                                                 "sl_shutdown_empty_update");
    }
    subchannel_list_ = subchannel_list;  // Empty list.
    selected_ = nullptr;
    return;
  }
  if (selected_ == nullptr) {
    // We don't yet have a selected subchannel, so replace the current
    // subchannel list immediately.
    if (subchannel_list_ != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(subchannel_list_,
                                                 "pf_update_before_selected");
    }
    subchannel_list_ = subchannel_list;
  } else {
    // We do have a selected subchannel.
    // Check if it's present in the new list.  If so, we're done.
    for (size_t i = 0; i < subchannel_list->num_subchannels; ++i) {
      grpc_lb_subchannel_data* sd = &subchannel_list->subchannels[i];
      if (sd->subchannel == selected_->subchannel) {
        // The currently selected subchannel is in the update: we are done.
        if (grpc_lb_pick_first_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "Pick First %p found already selected subchannel %p "
                  "at update index %" PRIuPTR " of %" PRIuPTR "; update done",
                  this, selected_->subchannel, i,
                  subchannel_list->num_subchannels);
        }
        if (selected_->connected_subchannel != nullptr) {
          sd->connected_subchannel = selected_->connected_subchannel;
        }
        selected_ = sd;
        if (subchannel_list_ != nullptr) {
          grpc_lb_subchannel_list_shutdown_and_unref(
              subchannel_list_, "pf_update_includes_selected");
        }
        subchannel_list_ = subchannel_list;
        DestroyUnselectedSubchannelsLocked();
        SubchannelListRefForConnectivityWatch(
            subchannel_list, "connectivity_watch+replace_selected");
        grpc_lb_subchannel_data_start_connectivity_watch(sd);
        // If there was a previously pending update (which may or may
        // not have contained the currently selected subchannel), drop
        // it, so that it doesn't override what we've done here.
        if (latest_pending_subchannel_list_ != nullptr) {
          grpc_lb_subchannel_list_shutdown_and_unref(
              latest_pending_subchannel_list_,
              "pf_update_includes_selected+outdated");
          latest_pending_subchannel_list_ = nullptr;
        }
        return;
      }
    }
    // Not keeping the previous selected subchannel, so set the latest
    // pending subchannel list to the new subchannel list.  We will wait
    // for it to report READY before swapping it into the current
    // subchannel list.
    if (latest_pending_subchannel_list_ != nullptr) {
      if (grpc_lb_pick_first_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "Pick First %p Shutting down latest pending subchannel list "
                "%p, about to be replaced by newer latest %p",
                this, latest_pending_subchannel_list_, subchannel_list);
      }
      grpc_lb_subchannel_list_shutdown_and_unref(
          latest_pending_subchannel_list_, "sl_outdated_dont_smash");
    }
    latest_pending_subchannel_list_ = subchannel_list;
  }
  // If we've started picking, start trying to connect to the first
  // subchannel in the new list.
  if (started_picking_) {
    SubchannelListRefForConnectivityWatch(subchannel_list,
                                          "connectivity_watch+update");
    grpc_lb_subchannel_data_start_connectivity_watch(
        &subchannel_list->subchannels[0]);
  }
}

void PickFirst::OnConnectivityChangedLocked(void* arg, grpc_error* error) {
  grpc_lb_subchannel_data* sd = static_cast<grpc_lb_subchannel_data*>(arg);
  PickFirst* p = static_cast<PickFirst*>(sd->subchannel_list->policy);
  if (grpc_lb_pick_first_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "Pick First %p connectivity changed for subchannel %p (%" PRIuPTR
            " of %" PRIuPTR
            "), subchannel_list %p: state=%s p->shutdown_=%d "
            "sd->subchannel_list->shutting_down=%d error=%s",
            p, sd->subchannel, sd->subchannel_list->checking_subchannel,
            sd->subchannel_list->num_subchannels, sd->subchannel_list,
            grpc_connectivity_state_name(sd->pending_connectivity_state_unsafe),
            p->shutdown_, sd->subchannel_list->shutting_down,
            grpc_error_string(error));
  }
  // If the policy is shutting down, unref and return.
  if (p->shutdown_) {
    grpc_lb_subchannel_data_stop_connectivity_watch(sd);
    grpc_lb_subchannel_data_unref_subchannel(sd, "pf_shutdown");
    p->SubchannelListUnrefForConnectivityWatch(sd->subchannel_list,
                                               "pf_shutdown");
    return;
  }
  // If the subchannel list is shutting down, stop watching.
  if (sd->subchannel_list->shutting_down || error == GRPC_ERROR_CANCELLED) {
    grpc_lb_subchannel_data_stop_connectivity_watch(sd);
    grpc_lb_subchannel_data_unref_subchannel(sd, "pf_sl_shutdown");
    p->SubchannelListUnrefForConnectivityWatch(sd->subchannel_list,
                                               "pf_sl_shutdown");
    return;
  }
  // If we're still here, the notification must be for a subchannel in
  // either the current or latest pending subchannel lists.
  GPR_ASSERT(sd->subchannel_list == p->subchannel_list_ ||
             sd->subchannel_list == p->latest_pending_subchannel_list_);
  // Update state.
  sd->curr_connectivity_state = sd->pending_connectivity_state_unsafe;
  // Handle updates for the currently selected subchannel.
  if (p->selected_ == sd) {
    // If the new state is anything other than READY and there is a
    // pending update, switch to the pending update.
    if (sd->curr_connectivity_state != GRPC_CHANNEL_READY &&
        p->latest_pending_subchannel_list_ != nullptr) {
      p->selected_ = nullptr;
      grpc_lb_subchannel_data_stop_connectivity_watch(sd);
      p->SubchannelListUnrefForConnectivityWatch(
          sd->subchannel_list, "selected_not_ready+switch_to_update");
      grpc_lb_subchannel_list_shutdown_and_unref(
          p->subchannel_list_, "selected_not_ready+switch_to_update");
      p->subchannel_list_ = p->latest_pending_subchannel_list_;
      p->latest_pending_subchannel_list_ = nullptr;
      grpc_connectivity_state_set(
          &p->state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_REF(error), "selected_not_ready+switch_to_update");
    } else {
      // TODO(juanlishen): we re-resolve when the selected subchannel goes to
      // TRANSIENT_FAILURE because we used to shut down in this case before
      // re-resolution is introduced. But we need to investigate whether we
      // really want to take any action instead of waiting for the selected
      // subchannel reconnecting.
      GPR_ASSERT(sd->curr_connectivity_state != GRPC_CHANNEL_SHUTDOWN);
      if (sd->curr_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        // If the selected channel goes bad, request a re-resolution.
        grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_IDLE,
                                    GRPC_ERROR_NONE,
                                    "selected_changed+reresolve");
        p->started_picking_ = false;
        p->TryReresolutionLocked(&grpc_lb_pick_first_trace, GRPC_ERROR_NONE);
        // In transient failure. Rely on re-resolution to recover.
        p->selected_ = nullptr;
        grpc_lb_subchannel_data_stop_connectivity_watch(sd);
        p->SubchannelListUnrefForConnectivityWatch(sd->subchannel_list,
                                                   "pf_selected_shutdown");
        grpc_lb_subchannel_data_unref_subchannel(
            sd, "pf_selected_shutdown");  // Unrefs connected subchannel
      } else {
        grpc_connectivity_state_set(&p->state_tracker_,
                                    sd->curr_connectivity_state,
                                    GRPC_ERROR_REF(error), "selected_changed");
        // Renew notification.
        grpc_lb_subchannel_data_start_connectivity_watch(sd);
      }
    }
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
  switch (sd->curr_connectivity_state) {
    case GRPC_CHANNEL_READY: {
      // Case 2.  Promote p->latest_pending_subchannel_list_ to
      // p->subchannel_list_.
      sd->connected_subchannel =
          grpc_subchannel_get_connected_subchannel(sd->subchannel);
      if (sd->subchannel_list == p->latest_pending_subchannel_list_) {
        GPR_ASSERT(p->subchannel_list_ != nullptr);
        grpc_lb_subchannel_list_shutdown_and_unref(p->subchannel_list_,
                                                   "finish_update");
        p->subchannel_list_ = p->latest_pending_subchannel_list_;
        p->latest_pending_subchannel_list_ = nullptr;
      }
      // Cases 1 and 2.
      grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_READY,
                                  GRPC_ERROR_NONE, "connecting_ready");
      p->selected_ = sd;
      if (grpc_lb_pick_first_trace.enabled()) {
        gpr_log(GPR_INFO, "Pick First %p selected subchannel %p", p,
                sd->subchannel);
      }
      // Drop all other subchannels, since we are now connected.
      p->DestroyUnselectedSubchannelsLocked();
      // Update any calls that were waiting for a pick.
      PickState* pick;
      while ((pick = p->pending_picks_)) {
        p->pending_picks_ = pick->next;
        pick->connected_subchannel = p->selected_->connected_subchannel;
        if (grpc_lb_pick_first_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "Servicing pending pick with selected subchannel %p",
                  p->selected_);
        }
        GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
      }
      // Renew notification.
      grpc_lb_subchannel_data_start_connectivity_watch(sd);
      break;
    }
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      grpc_lb_subchannel_data_stop_connectivity_watch(sd);
      do {
        sd->subchannel_list->checking_subchannel =
            (sd->subchannel_list->checking_subchannel + 1) %
            sd->subchannel_list->num_subchannels;
        sd = &sd->subchannel_list
                  ->subchannels[sd->subchannel_list->checking_subchannel];
      } while (sd->subchannel == nullptr);
      // Case 1: Only set state to TRANSIENT_FAILURE if we've tried
      // all subchannels.
      if (sd->subchannel_list->checking_subchannel == 0 &&
          sd->subchannel_list == p->subchannel_list_) {
        grpc_connectivity_state_set(
            &p->state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
            GRPC_ERROR_REF(error), "connecting_transient_failure");
      }
      // Reuses the connectivity refs from the previous watch.
      grpc_lb_subchannel_data_start_connectivity_watch(sd);
      break;
    }
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE: {
      // Only update connectivity state in case 1.
      if (sd->subchannel_list == p->subchannel_list_) {
        grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_CONNECTING,
                                    GRPC_ERROR_REF(error),
                                    "connecting_changed");
      }
      // Renew notification.
      grpc_lb_subchannel_data_start_connectivity_watch(sd);
      break;
    }
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_UNREACHABLE_CODE(break);
  }
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
