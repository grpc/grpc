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

/** Round Robin Policy.
 *
 * Before every pick, the \a get_next_ready_subchannel_index_locked function
 * returns the p->subchannel_list->subchannels index for next subchannel,
 * respecting the relative order of the addresses provided upon creation or
 * updates. Note however that updates will start picking from the beginning of
 * the updated list. */

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/static_metadata.h"

namespace grpc_core {

TraceFlag grpc_lb_round_robin_trace(false, "round_robin");

namespace {

//
// round_robin LB policy
//

class RoundRobin : public LoadBalancingPolicy {
 public:
  explicit RoundRobin(const Args& args);

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
  ~RoundRobin();

  void ShutdownLocked() override;

  void StartPickingLocked();
  size_t GetNextReadySubchannelIndexLocked();
  void UpdateLastReadySubchannelIndexLocked(size_t last_ready_index);
  void UpdateConnectivityStatusLocked(grpc_lb_subchannel_data* sd,
                                      grpc_error* error);

  static void OnConnectivityChangedLocked(void* arg, grpc_error* error);

  void SubchannelListRefForConnectivityWatch(
      grpc_lb_subchannel_list* subchannel_list, const char* reason);
  void SubchannelListUnrefForConnectivityWatch(
      grpc_lb_subchannel_list* subchannel_list, const char* reason);

  /** list of subchannels */
  grpc_lb_subchannel_list* subchannel_list_ = nullptr;
  /** have we started picking? */
  bool started_picking_ = false;
  /** are we shutting down? */
  bool shutdown_ = false;
  /** List of picks that are waiting on connectivity */
  PickState* pending_picks_ = nullptr;
  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker_;
  /** Index into subchannels for last pick. */
  size_t last_ready_subchannel_index_ = 0;
  /** Latest version of the subchannel list.
   * Subchannel connectivity callbacks will only promote updated subchannel
   * lists if they equal \a latest_pending_subchannel_list. In other words,
   * racing callbacks that reference outdated subchannel lists won't perform any
   * update. */
  grpc_lb_subchannel_list* latest_pending_subchannel_list_ = nullptr;
};

RoundRobin::RoundRobin(const Args& args) : LoadBalancingPolicy(args) {
  GPR_ASSERT(args.client_channel_factory != nullptr);
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "round_robin");
  UpdateLocked(*args.args);
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] Created with %" PRIuPTR " subchannels", this,
            subchannel_list_->num_subchannels);
  }
  grpc_subchannel_index_ref();
}

RoundRobin::~RoundRobin() {
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] Destroying Round Robin policy", this);
  }
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
  GPR_ASSERT(pending_picks_ == nullptr);
  grpc_connectivity_state_destroy(&state_tracker_);
  grpc_subchannel_index_unref();
}

/** Returns the index into p->subchannel_list->subchannels of the next
 * subchannel in READY state, or p->subchannel_list->num_subchannels if no
 * subchannel is READY.
 *
 * Note that this function does *not* update p->last_ready_subchannel_index.
 * The caller must do that if it returns a pick. */
size_t RoundRobin::GetNextReadySubchannelIndexLocked() {
  GPR_ASSERT(subchannel_list_ != nullptr);
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[RR %p] getting next ready subchannel (out of %" PRIuPTR
            "), "
            "last_ready_subchannel_index=%" PRIuPTR,
            this, subchannel_list_->num_subchannels,
            last_ready_subchannel_index_);
  }
  for (size_t i = 0; i < subchannel_list_->num_subchannels; ++i) {
    const size_t index = (i + last_ready_subchannel_index_ + 1) %
                         subchannel_list_->num_subchannels;
    if (grpc_lb_round_robin_trace.enabled()) {
      gpr_log(
          GPR_DEBUG,
          "[RR %p] checking subchannel %p, subchannel_list %p, index %" PRIuPTR
          ": state=%s",
          this, subchannel_list_->subchannels[index].subchannel,
          subchannel_list_, index,
          grpc_connectivity_state_name(
              subchannel_list_->subchannels[index].curr_connectivity_state));
    }
    if (subchannel_list_->subchannels[index].curr_connectivity_state ==
        GRPC_CHANNEL_READY) {
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "[RR %p] found next ready subchannel (%p) at index %" PRIuPTR
                " of subchannel_list %p",
                this, subchannel_list_->subchannels[index].subchannel, index,
                subchannel_list_);
      }
      return index;
    }
  }
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] no subchannels in ready state", this);
  }
  return subchannel_list_->num_subchannels;
}

// Sets last_ready_subchannel_index_ to last_ready_index.
void RoundRobin::UpdateLastReadySubchannelIndexLocked(size_t last_ready_index) {
  GPR_ASSERT(last_ready_index < subchannel_list_->num_subchannels);
  last_ready_subchannel_index_ = last_ready_index;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "[RR %p] setting last_ready_subchannel_index=%" PRIuPTR
            " (SC %p, CSC %p)",
            this, last_ready_index,
            subchannel_list_->subchannels[last_ready_index].subchannel,
            subchannel_list_->subchannels[last_ready_index]
                .connected_subchannel.get());
  }
}

void RoundRobin::HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) {
  PickState* pick;
  while ((pick = pending_picks_) != nullptr) {
    pending_picks_ = pick->next;
    if (new_policy->PickLocked(pick)) {
      // Synchronous return, schedule closure.
      GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
    }
  }
}

void RoundRobin::ShutdownLocked() {
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] Shutting down", this);
  }
  shutdown_ = true;
  PickState* pick;
  while ((pick = pending_picks_) != nullptr) {
    pending_picks_ = pick->next;
    pick->connected_subchannel.reset();
    GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_REF(error));
  }
  grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_SHUTDOWN,
                              GRPC_ERROR_REF(error), "rr_shutdown");
  if (subchannel_list_ != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(subchannel_list_,
                                               "sl_shutdown_rr_shutdown");
    subchannel_list_ = nullptr;
  }
  if (latest_pending_subchannel_list_ != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(
        latest_pending_subchannel_list_, "sl_shutdown_pending_rr_shutdown");
    latest_pending_subchannel_list_ = nullptr;
  }
  TryReresolutionLocked(&grpc_lb_round_robin_trace, GRPC_ERROR_CANCELLED);
  GRPC_ERROR_UNREF(error);
}

void RoundRobin::CancelPickLocked(PickState* pick, grpc_error* error) {
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

void RoundRobin::CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
                                           uint32_t initial_metadata_flags_eq,
                                           grpc_error* error) {
  PickState* pick = pending_picks_;
  pending_picks_ = nullptr;
  while (pick != nullptr) {
    PickState* next = pick->next;
    if ((pick->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      pick->connected_subchannel.reset();
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

void RoundRobin::SubchannelListRefForConnectivityWatch(
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

void RoundRobin::SubchannelListUnrefForConnectivityWatch(
    grpc_lb_subchannel_list* subchannel_list, const char* reason) {
  Unref(DEBUG_LOCATION, reason);
  grpc_lb_subchannel_list_unref(subchannel_list, reason);
}

void RoundRobin::StartPickingLocked() {
  started_picking_ = true;
  for (size_t i = 0; i < subchannel_list_->num_subchannels; i++) {
    if (subchannel_list_->subchannels[i].subchannel != nullptr) {
      SubchannelListRefForConnectivityWatch(subchannel_list_,
                                            "connectivity_watch");
      grpc_lb_subchannel_data_start_connectivity_watch(
          &subchannel_list_->subchannels[i]);
    }
  }
}

void RoundRobin::ExitIdleLocked() {
  if (!started_picking_) {
    StartPickingLocked();
  }
}

bool RoundRobin::PickLocked(PickState* pick) {
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] Trying to pick (shutdown: %d)", this,
            shutdown_);
  }
  GPR_ASSERT(!shutdown_);
  if (subchannel_list_ != nullptr) {
    const size_t next_ready_index = GetNextReadySubchannelIndexLocked();
    if (next_ready_index < subchannel_list_->num_subchannels) {
      /* readily available, report right away */
      grpc_lb_subchannel_data* sd =
          &subchannel_list_->subchannels[next_ready_index];
      pick->connected_subchannel = sd->connected_subchannel;
      if (pick->user_data != nullptr) {
        *pick->user_data = sd->user_data;
      }
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(
            GPR_DEBUG,
            "[RR %p] Picked target <-- Subchannel %p (connected %p) (sl %p, "
            "index %" PRIuPTR ")",
            this, sd->subchannel, pick->connected_subchannel.get(),
            sd->subchannel_list, next_ready_index);
      }
      /* only advance the last picked pointer if the selection was used */
      UpdateLastReadySubchannelIndexLocked(next_ready_index);
      return true;
    }
  }
  /* no pick currently available. Save for later in list of pending picks */
  if (!started_picking_) {
    StartPickingLocked();
  }
  pick->next = pending_picks_;
  pending_picks_ = pick;
  return false;
}

void UpdateStateCountersLocked(grpc_lb_subchannel_data* sd) {
  grpc_lb_subchannel_list* subchannel_list = sd->subchannel_list;
  GPR_ASSERT(sd->prev_connectivity_state != GRPC_CHANNEL_SHUTDOWN);
  GPR_ASSERT(sd->curr_connectivity_state != GRPC_CHANNEL_SHUTDOWN);
  if (sd->prev_connectivity_state == GRPC_CHANNEL_READY) {
    GPR_ASSERT(subchannel_list->num_ready > 0);
    --subchannel_list->num_ready;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    GPR_ASSERT(subchannel_list->num_transient_failures > 0);
    --subchannel_list->num_transient_failures;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_IDLE) {
    GPR_ASSERT(subchannel_list->num_idle > 0);
    --subchannel_list->num_idle;
  }
  sd->prev_connectivity_state = sd->curr_connectivity_state;
  if (sd->curr_connectivity_state == GRPC_CHANNEL_READY) {
    ++subchannel_list->num_ready;
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++subchannel_list->num_transient_failures;
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_IDLE) {
    ++subchannel_list->num_idle;
  }
}

/** Sets the policy's connectivity status based on that of the passed-in \a sd
 * (the grpc_lb_subchannel_data associated with the updated subchannel) and the
 * subchannel list \a sd belongs to (sd->subchannel_list). \a error will be used
 * only if the policy transitions to state TRANSIENT_FAILURE. */
void RoundRobin::UpdateConnectivityStatusLocked(grpc_lb_subchannel_data* sd,
                                                grpc_error* error) {
  /* In priority order. The first rule to match terminates the search (ie, if we
   * are on rule n, all previous rules were unfulfilled).
   *
   * 1) RULE: ANY subchannel is READY => policy is READY.
   *    CHECK: subchannel_list->num_ready > 0.
   *
   * 2) RULE: ANY subchannel is CONNECTING => policy is CONNECTING.
   *    CHECK: sd->curr_connectivity_state == CONNECTING.
   *
   * 3) RULE: ALL subchannels are TRANSIENT_FAILURE => policy is
   *                                                   TRANSIENT_FAILURE.
   *    CHECK: subchannel_list->num_transient_failures ==
   *           subchannel_list->num_subchannels.
   */
  grpc_lb_subchannel_list* subchannel_list = sd->subchannel_list;
  GPR_ASSERT(sd->curr_connectivity_state != GRPC_CHANNEL_IDLE);
  if (subchannel_list->num_ready > 0) {
    /* 1) READY */
    grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_READY,
                                GRPC_ERROR_NONE, "rr_ready");
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_CONNECTING) {
    /* 2) CONNECTING */
    grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_CONNECTING,
                                GRPC_ERROR_NONE, "rr_connecting");
  } else if (subchannel_list->num_transient_failures ==
             subchannel_list->num_subchannels) {
    /* 3) TRANSIENT_FAILURE */
    grpc_connectivity_state_set(&state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
                                GRPC_ERROR_REF(error),
                                "rr_exhausted_subchannels");
  }
  GRPC_ERROR_UNREF(error);
}

void RoundRobin::OnConnectivityChangedLocked(void* arg, grpc_error* error) {
  grpc_lb_subchannel_data* sd = reinterpret_cast<grpc_lb_subchannel_data*>(arg);
  RoundRobin* p = reinterpret_cast<RoundRobin*>(sd->subchannel_list->policy);
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(
        GPR_DEBUG,
        "[RR %p] connectivity changed for subchannel %p, subchannel_list %p: "
        "prev_state=%s new_state=%s p->shutdown=%d "
        "sd->subchannel_list->shutting_down=%d error=%s",
        p, sd->subchannel, sd->subchannel_list,
        grpc_connectivity_state_name(sd->prev_connectivity_state),
        grpc_connectivity_state_name(sd->pending_connectivity_state_unsafe),
        p->shutdown_, sd->subchannel_list->shutting_down,
        grpc_error_string(error));
  }
  GPR_ASSERT(sd->subchannel != nullptr);
  // If the policy is shutting down, unref and return.
  if (p->shutdown_) {
    grpc_lb_subchannel_data_stop_connectivity_watch(sd);
    grpc_lb_subchannel_data_unref_subchannel(sd, "rr_shutdown");
    p->SubchannelListUnrefForConnectivityWatch(sd->subchannel_list,
                                               "rr_shutdown");
    return;
  }
  // If the subchannel list is shutting down, stop watching.
  if (sd->subchannel_list->shutting_down || error == GRPC_ERROR_CANCELLED) {
    grpc_lb_subchannel_data_stop_connectivity_watch(sd);
    grpc_lb_subchannel_data_unref_subchannel(sd, "rr_sl_shutdown");
    p->SubchannelListUnrefForConnectivityWatch(sd->subchannel_list,
                                               "rr_sl_shutdown");
    return;
  }
  // If we're still here, the notification must be for a subchannel in
  // either the current or latest pending subchannel lists.
  GPR_ASSERT(sd->subchannel_list == p->subchannel_list_ ||
             sd->subchannel_list == p->latest_pending_subchannel_list_);
  GPR_ASSERT(sd->pending_connectivity_state_unsafe != GRPC_CHANNEL_SHUTDOWN);
  // Now that we're inside the combiner, copy the pending connectivity
  // state (which was set by the connectivity state watcher) to
  // curr_connectivity_state, which is what we use inside of the combiner.
  sd->curr_connectivity_state = sd->pending_connectivity_state_unsafe;
  // If the sd's new state is TRANSIENT_FAILURE, unref the *connected*
  // subchannel, if any.
  switch (sd->curr_connectivity_state) {
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      sd->connected_subchannel.reset();
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "[RR %p] Subchannel %p has gone into TRANSIENT_FAILURE. "
                "Requesting re-resolution",
                p, sd->subchannel);
      }
      p->TryReresolutionLocked(&grpc_lb_round_robin_trace, GRPC_ERROR_NONE);
      break;
    }
    case GRPC_CHANNEL_READY: {
      if (sd->connected_subchannel == nullptr) {
        sd->connected_subchannel =
            grpc_subchannel_get_connected_subchannel(sd->subchannel);
      }
      if (sd->subchannel_list != p->subchannel_list_) {
        // promote sd->subchannel_list to p->subchannel_list_.
        // sd->subchannel_list must be equal to
        // p->latest_pending_subchannel_list_ because we have already filtered
        // for sds belonging to outdated subchannel lists.
        GPR_ASSERT(sd->subchannel_list == p->latest_pending_subchannel_list_);
        GPR_ASSERT(!sd->subchannel_list->shutting_down);
        if (grpc_lb_round_robin_trace.enabled()) {
          const size_t num_subchannels =
              p->subchannel_list_ != nullptr
                  ? p->subchannel_list_->num_subchannels
                  : 0;
          gpr_log(GPR_DEBUG,
                  "[RR %p] phasing out subchannel list %p (size %" PRIuPTR
                  ") in favor of %p (size %" PRIuPTR ")",
                  p, p->subchannel_list_, num_subchannels, sd->subchannel_list,
                  num_subchannels);
        }
        if (p->subchannel_list_ != nullptr) {
          // dispose of the current subchannel_list
          grpc_lb_subchannel_list_shutdown_and_unref(p->subchannel_list_,
                                                     "sl_phase_out_shutdown");
        }
        p->subchannel_list_ = p->latest_pending_subchannel_list_;
        p->latest_pending_subchannel_list_ = nullptr;
      }
      /* at this point we know there's at least one suitable subchannel. Go
       * ahead and pick one and notify the pending suitors in
       * p->pending_picks. This preemptively replicates rr_pick()'s actions. */
      const size_t next_ready_index = p->GetNextReadySubchannelIndexLocked();
      GPR_ASSERT(next_ready_index < p->subchannel_list_->num_subchannels);
      grpc_lb_subchannel_data* selected =
          &p->subchannel_list_->subchannels[next_ready_index];
      if (p->pending_picks_ != nullptr) {
        // if the selected subchannel is going to be used for the pending
        // picks, update the last picked pointer
        p->UpdateLastReadySubchannelIndexLocked(next_ready_index);
      }
      PickState* pick;
      while ((pick = p->pending_picks_)) {
        p->pending_picks_ = pick->next;
        pick->connected_subchannel = selected->connected_subchannel;
        if (pick->user_data != nullptr) {
          *pick->user_data = selected->user_data;
        }
        if (grpc_lb_round_robin_trace.enabled()) {
          gpr_log(GPR_DEBUG,
                  "[RR %p] Fulfilling pending pick. Target <-- subchannel %p "
                  "(subchannel_list %p, index %" PRIuPTR ")",
                  p, selected->subchannel, p->subchannel_list_,
                  next_ready_index);
        }
        GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
      }
      break;
    }
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_UNREACHABLE_CODE(return );
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE:;  // fallthrough
  }
  // Update state counters.
  UpdateStateCountersLocked(sd);
  // Only update connectivity based on the selected subchannel list.
  if (sd->subchannel_list == p->subchannel_list_) {
    p->UpdateConnectivityStatusLocked(sd, GRPC_ERROR_REF(error));
  }
  // Renew notification.
  grpc_lb_subchannel_data_start_connectivity_watch(sd);
}

grpc_connectivity_state RoundRobin::CheckConnectivityLocked(
    grpc_error** error) {
  return grpc_connectivity_state_get(&state_tracker_, error);
}

void RoundRobin::NotifyOnStateChangeLocked(grpc_connectivity_state* current,
                                           grpc_closure* notify) {
  grpc_connectivity_state_notify_on_state_change(&state_tracker_, current,
                                                 notify);
}

void RoundRobin::PingOneLocked(grpc_closure* on_initiate,
                               grpc_closure* on_ack) {
  const size_t next_ready_index = GetNextReadySubchannelIndexLocked();
  if (next_ready_index < subchannel_list_->num_subchannels) {
    grpc_lb_subchannel_data* selected =
        &subchannel_list_->subchannels[next_ready_index];
    selected->connected_subchannel->Ping(on_initiate, on_ack);
  } else {
    GRPC_CLOSURE_SCHED(on_initiate, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                        "Round Robin not connected"));
    GRPC_CLOSURE_SCHED(on_ack, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                   "Round Robin not connected"));
  }
}

void RoundRobin::UpdateLocked(const grpc_channel_args& args) {
  const grpc_arg* arg = grpc_channel_args_find(&args, GRPC_ARG_LB_ADDRESSES);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "[RR %p] update provided no addresses; ignoring", this);
    // If we don't have a current subchannel list, go into TRANSIENT_FAILURE.
    // Otherwise, keep using the current subchannel list (ignore this update).
    if (subchannel_list_ == nullptr) {
      grpc_connectivity_state_set(
          &state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "rr_update_missing");
    }
    return;
  }
  grpc_lb_addresses* addresses = (grpc_lb_addresses*)arg->value.pointer.p;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] received update with %" PRIuPTR " addresses",
            this, addresses->num_addresses);
  }
  grpc_lb_subchannel_list* subchannel_list = grpc_lb_subchannel_list_create(
      this, &grpc_lb_round_robin_trace, addresses, combiner(),
      client_channel_factory(), args, &RoundRobin::OnConnectivityChangedLocked);
  if (subchannel_list->num_subchannels == 0) {
    grpc_connectivity_state_set(
        &state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
        "rr_update_empty");
    if (subchannel_list_ != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(subchannel_list_,
                                                 "sl_shutdown_empty_update");
    }
    subchannel_list_ = subchannel_list;  // empty list
    return;
  }
  if (started_picking_) {
    for (size_t i = 0; i < subchannel_list->num_subchannels; ++i) {
      const grpc_connectivity_state subchannel_state =
          grpc_subchannel_check_connectivity(
              subchannel_list->subchannels[i].subchannel, nullptr);
      // Override the default setting of IDLE for connectivity notification
      // purposes if the subchannel is already in transient failure. Otherwise
      // we'd be immediately notified of the IDLE-TRANSIENT_FAILURE
      // discrepancy, attempt to re-resolve and end up here again.
      // TODO(roth): As part of C++-ifying the subchannel_list API, design a
      // better API for notifying the LB policy of subchannel states, which can
      // be used both for the subchannel's initial state and for subsequent
      // state changes. This will allow us to handle this more generally instead
      // of special-casing TRANSIENT_FAILURE (e.g., we can also distribute any
      // pending picks across all READY subchannels rather than sending them all
      // to the first one).
      if (subchannel_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        subchannel_list->subchannels[i].pending_connectivity_state_unsafe =
            subchannel_list->subchannels[i].curr_connectivity_state =
                subchannel_list->subchannels[i].prev_connectivity_state =
                    subchannel_state;
        --subchannel_list->num_idle;
        ++subchannel_list->num_transient_failures;
      }
    }
    if (latest_pending_subchannel_list_ != nullptr) {
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "[RR %p] Shutting down latest pending subchannel list %p, "
                "about to be replaced by newer latest %p",
                this, latest_pending_subchannel_list_, subchannel_list);
      }
      grpc_lb_subchannel_list_shutdown_and_unref(
          latest_pending_subchannel_list_, "sl_outdated");
    }
    latest_pending_subchannel_list_ = subchannel_list;
    for (size_t i = 0; i < subchannel_list->num_subchannels; ++i) {
      /* Watch every new subchannel. A subchannel list becomes active the
       * moment one of its subchannels is READY. At that moment, we swap
       * p->subchannel_list for sd->subchannel_list, provided the subchannel
       * list is still valid (ie, isn't shutting down) */
      SubchannelListRefForConnectivityWatch(subchannel_list,
                                            "connectivity_watch");
      grpc_lb_subchannel_data_start_connectivity_watch(
          &subchannel_list->subchannels[i]);
    }
  } else {
    // The policy isn't picking yet. Save the update for later, disposing of
    // previous version if any.
    if (subchannel_list_ != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(
          subchannel_list_, "rr_update_before_started_picking");
    }
    subchannel_list_ = subchannel_list;
  }
}

//
// factory
//

class RoundRobinFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      const LoadBalancingPolicy::Args& args) const override {
    return OrphanablePtr<LoadBalancingPolicy>(New<RoundRobin>(args));
  }

  const char* name() const override { return "round_robin"; }
};

}  // namespace

}  // namespace grpc_core

void grpc_lb_policy_round_robin_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::UniquePtr<grpc_core::LoadBalancingPolicyFactory>(
              grpc_core::New<grpc_core::RoundRobinFactory>()));
}

void grpc_lb_policy_round_robin_shutdown() {}
