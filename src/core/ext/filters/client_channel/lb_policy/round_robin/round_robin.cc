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

#include <grpc/support/port_platform.h>

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
  // TODO(ncteisen): implement this in a follow up PR
  void FillChildRefsForChannelz(ChildRefsList* child_subchannels,
                                ChildRefsList* child_channels) override {}

 private:
  ~RoundRobin();

  // Forward declaration.
  class RoundRobinSubchannelList;

  // Data for a particular subchannel in a subchannel list.
  // This subclass adds the following functionality:
  // - Tracks user_data associated with each address, which will be
  //   returned along with picks that select the subchannel.
  // - Tracks the previous connectivity state of the subchannel, so that
  //   we know how many subchannels are in each state.
  class RoundRobinSubchannelData
      : public SubchannelData<RoundRobinSubchannelList,
                              RoundRobinSubchannelData> {
   public:
    RoundRobinSubchannelData(RoundRobinSubchannelList* subchannel_list,
                             const grpc_lb_user_data_vtable* user_data_vtable,
                             const grpc_lb_address& address,
                             grpc_subchannel* subchannel,
                             grpc_combiner* combiner)
        : SubchannelData(subchannel_list, user_data_vtable, address, subchannel,
                         combiner),
          user_data_vtable_(user_data_vtable),
          user_data_(user_data_vtable_ != nullptr
                         ? user_data_vtable_->copy(address.user_data)
                         : nullptr) {}

    void UnrefSubchannelLocked(const char* reason) override {
      SubchannelData::UnrefSubchannelLocked(reason);
      if (user_data_ != nullptr) {
        GPR_ASSERT(user_data_vtable_ != nullptr);
        user_data_vtable_->destroy(user_data_);
        user_data_ = nullptr;
      }
    }

    void* user_data() const { return user_data_; }

    grpc_connectivity_state connectivity_state() const {
      return last_connectivity_state_;
    }

    void UpdateConnectivityStateLocked(
        grpc_connectivity_state connectivity_state, grpc_error* error);

   private:
    void ProcessConnectivityChangeLocked(
        grpc_connectivity_state connectivity_state, grpc_error* error) override;

    const grpc_lb_user_data_vtable* user_data_vtable_;
    void* user_data_ = nullptr;
    grpc_connectivity_state last_connectivity_state_ = GRPC_CHANNEL_IDLE;
  };

  // A list of subchannels.
  class RoundRobinSubchannelList
      : public SubchannelList<RoundRobinSubchannelList,
                              RoundRobinSubchannelData> {
   public:
    RoundRobinSubchannelList(
        RoundRobin* policy, TraceFlag* tracer,
        const grpc_lb_addresses* addresses, grpc_combiner* combiner,
        grpc_client_channel_factory* client_channel_factory,
        const grpc_channel_args& args)
        : SubchannelList(policy, tracer, addresses, combiner,
                         client_channel_factory, args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
    }

    ~RoundRobinSubchannelList() {
      GRPC_ERROR_UNREF(last_transient_failure_error_);
      RoundRobin* p = static_cast<RoundRobin*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    // Starts watching the subchannels in this list.
    void StartWatchingLocked();

    // Updates the counters of subchannels in each state when a
    // subchannel transitions from old_state to new_state.
    // transient_failure_error is the error that is reported when
    // new_state is TRANSIENT_FAILURE.
    void UpdateStateCountersLocked(grpc_connectivity_state old_state,
                                   grpc_connectivity_state new_state,
                                   grpc_error* transient_failure_error);

    // If this subchannel list is the RR policy's current subchannel
    // list, updates the RR policy's connectivity state based on the
    // subchannel list's state counters.
    void MaybeUpdateRoundRobinConnectivityStateLocked();

    // Updates the RR policy's overall state based on the counters of
    // subchannels in each state.
    void UpdateRoundRobinStateFromSubchannelStateCountsLocked();

    size_t GetNextReadySubchannelIndexLocked();
    void UpdateLastReadySubchannelIndexLocked(size_t last_ready_index);

   private:
    size_t num_ready_ = 0;
    size_t num_connecting_ = 0;
    size_t num_transient_failure_ = 0;
    grpc_error* last_transient_failure_error_ = GRPC_ERROR_NONE;
    size_t last_ready_index_ = -1;  // Index into list of last pick.
  };

  void ShutdownLocked() override;

  void StartPickingLocked();
  bool DoPickLocked(PickState* pick);
  void DrainPendingPicksLocked();

  /** list of subchannels */
  OrphanablePtr<RoundRobinSubchannelList> subchannel_list_;
  /** Latest version of the subchannel list.
   * Subchannel connectivity callbacks will only promote updated subchannel
   * lists if they equal \a latest_pending_subchannel_list. In other words,
   * racing callbacks that reference outdated subchannel lists won't perform any
   * update. */
  OrphanablePtr<RoundRobinSubchannelList> latest_pending_subchannel_list_;
  /** have we started picking? */
  bool started_picking_ = false;
  /** are we shutting down? */
  bool shutdown_ = false;
  /** List of picks that are waiting on connectivity */
  PickState* pending_picks_ = nullptr;
  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker_;
};

RoundRobin::RoundRobin(const Args& args) : LoadBalancingPolicy(args) {
  GPR_ASSERT(args.client_channel_factory != nullptr);
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "round_robin");
  UpdateLocked(*args.args);
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO, "[RR %p] Created with %" PRIuPTR " subchannels", this,
            subchannel_list_->num_subchannels());
  }
  grpc_subchannel_index_ref();
}

RoundRobin::~RoundRobin() {
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO, "[RR %p] Destroying Round Robin policy", this);
  }
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
  GPR_ASSERT(pending_picks_ == nullptr);
  grpc_connectivity_state_destroy(&state_tracker_);
  grpc_subchannel_index_unref();
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
    gpr_log(GPR_INFO, "[RR %p] Shutting down", this);
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
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
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

void RoundRobin::StartPickingLocked() {
  started_picking_ = true;
  subchannel_list_->StartWatchingLocked();
}

void RoundRobin::ExitIdleLocked() {
  if (!started_picking_) {
    StartPickingLocked();
  }
}

bool RoundRobin::DoPickLocked(PickState* pick) {
  const size_t next_ready_index =
      subchannel_list_->GetNextReadySubchannelIndexLocked();
  if (next_ready_index < subchannel_list_->num_subchannels()) {
    /* readily available, report right away */
    RoundRobinSubchannelData* sd =
        subchannel_list_->subchannel(next_ready_index);
    GPR_ASSERT(sd->connected_subchannel() != nullptr);
    pick->connected_subchannel = sd->connected_subchannel()->Ref();
    if (pick->user_data != nullptr) {
      *pick->user_data = sd->user_data();
    }
    if (grpc_lb_round_robin_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[RR %p] Picked target <-- Subchannel %p (connected %p) (sl %p, "
              "index %" PRIuPTR ")",
              this, sd->subchannel(), pick->connected_subchannel.get(),
              sd->subchannel_list(), next_ready_index);
    }
    /* only advance the last picked pointer if the selection was used */
    subchannel_list_->UpdateLastReadySubchannelIndexLocked(next_ready_index);
    return true;
  }
  return false;
}

void RoundRobin::DrainPendingPicksLocked() {
  PickState* pick;
  while ((pick = pending_picks_)) {
    pending_picks_ = pick->next;
    GPR_ASSERT(DoPickLocked(pick));
    GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
  }
}

bool RoundRobin::PickLocked(PickState* pick) {
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO, "[RR %p] Trying to pick (shutdown: %d)", this, shutdown_);
  }
  GPR_ASSERT(!shutdown_);
  if (subchannel_list_ != nullptr) {
    if (DoPickLocked(pick)) return true;
  }
  /* no pick currently available. Save for later in list of pending picks */
  pick->next = pending_picks_;
  pending_picks_ = pick;
  if (!started_picking_) {
    StartPickingLocked();
  }
  return false;
}

void RoundRobin::RoundRobinSubchannelList::StartWatchingLocked() {
  if (num_subchannels() == 0) return;
  // Check current state of each subchannel synchronously, since any
  // subchannel already used by some other channel may have a non-IDLE
  // state.
  for (size_t i = 0; i < num_subchannels(); ++i) {
    grpc_error* error = GRPC_ERROR_NONE;
    grpc_connectivity_state state =
        subchannel(i)->CheckConnectivityStateLocked(&error);
    if (state != GRPC_CHANNEL_IDLE) {
      subchannel(i)->UpdateConnectivityStateLocked(state, error);
    }
  }
  // Now set the LB policy's state based on the subchannels' states.
  UpdateRoundRobinStateFromSubchannelStateCountsLocked();
  // Start connectivity watch for each subchannel.
  for (size_t i = 0; i < num_subchannels(); i++) {
    if (subchannel(i)->subchannel() != nullptr) {
      subchannel(i)->StartConnectivityWatchLocked();
    }
  }
}

void RoundRobin::RoundRobinSubchannelList::UpdateStateCountersLocked(
    grpc_connectivity_state old_state, grpc_connectivity_state new_state,
    grpc_error* transient_failure_error) {
  GPR_ASSERT(old_state != GRPC_CHANNEL_SHUTDOWN);
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  if (old_state == GRPC_CHANNEL_READY) {
    GPR_ASSERT(num_ready_ > 0);
    --num_ready_;
  } else if (old_state == GRPC_CHANNEL_CONNECTING) {
    GPR_ASSERT(num_connecting_ > 0);
    --num_connecting_;
  } else if (old_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    GPR_ASSERT(num_transient_failure_ > 0);
    --num_transient_failure_;
  }
  if (new_state == GRPC_CHANNEL_READY) {
    ++num_ready_;
  } else if (new_state == GRPC_CHANNEL_CONNECTING) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
  GRPC_ERROR_UNREF(last_transient_failure_error_);
  last_transient_failure_error_ = transient_failure_error;
}

// Sets the RR policy's connectivity state based on the current
// subchannel list.
void RoundRobin::RoundRobinSubchannelList::
    MaybeUpdateRoundRobinConnectivityStateLocked() {
  RoundRobin* p = static_cast<RoundRobin*>(policy());
  // Only set connectivity state if this is the current subchannel list.
  if (p->subchannel_list_.get() != this) return;
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
  if (num_ready_ > 0) {
    /* 1) READY */
    grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_READY,
                                GRPC_ERROR_NONE, "rr_ready");
  } else if (num_connecting_ > 0) {
    /* 2) CONNECTING */
    grpc_connectivity_state_set(&p->state_tracker_, GRPC_CHANNEL_CONNECTING,
                                GRPC_ERROR_NONE, "rr_connecting");
  } else if (num_transient_failure_ == num_subchannels()) {
    /* 3) TRANSIENT_FAILURE */
    grpc_connectivity_state_set(&p->state_tracker_,
                                GRPC_CHANNEL_TRANSIENT_FAILURE,
                                GRPC_ERROR_REF(last_transient_failure_error_),
                                "rr_exhausted_subchannels");
  }
}

void RoundRobin::RoundRobinSubchannelList::
    UpdateRoundRobinStateFromSubchannelStateCountsLocked() {
  RoundRobin* p = static_cast<RoundRobin*>(policy());
  if (num_ready_ > 0) {
    if (p->subchannel_list_.get() != this) {
      // Promote this list to p->subchannel_list_.
      // This list must be p->latest_pending_subchannel_list_, because
      // any previous update would have been shut down already and
      // therefore we would not be receiving a notification for them.
      GPR_ASSERT(p->latest_pending_subchannel_list_.get() == this);
      GPR_ASSERT(!shutting_down());
      if (grpc_lb_round_robin_trace.enabled()) {
        const size_t old_num_subchannels =
            p->subchannel_list_ != nullptr
                ? p->subchannel_list_->num_subchannels()
                : 0;
        gpr_log(GPR_INFO,
                "[RR %p] phasing out subchannel list %p (size %" PRIuPTR
                ") in favor of %p (size %" PRIuPTR ")",
                p, p->subchannel_list_.get(), old_num_subchannels, this,
                num_subchannels());
      }
      p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
    }
    // Drain pending picks.
    p->DrainPendingPicksLocked();
  }
  // Update the RR policy's connectivity state if needed.
  MaybeUpdateRoundRobinConnectivityStateLocked();
}

void RoundRobin::RoundRobinSubchannelData::UpdateConnectivityStateLocked(
    grpc_connectivity_state connectivity_state, grpc_error* error) {
  RoundRobin* p = static_cast<RoundRobin*>(subchannel_list()->policy());
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(
        GPR_INFO,
        "[RR %p] connectivity changed for subchannel %p, subchannel_list %p "
        "(index %" PRIuPTR " of %" PRIuPTR "): prev_state=%s new_state=%s",
        p, subchannel(), subchannel_list(), Index(),
        subchannel_list()->num_subchannels(),
        grpc_connectivity_state_name(last_connectivity_state_),
        grpc_connectivity_state_name(connectivity_state));
  }
  subchannel_list()->UpdateStateCountersLocked(last_connectivity_state_,
                                               connectivity_state, error);
  last_connectivity_state_ = connectivity_state;
}

void RoundRobin::RoundRobinSubchannelData::ProcessConnectivityChangeLocked(
    grpc_connectivity_state connectivity_state, grpc_error* error) {
  RoundRobin* p = static_cast<RoundRobin*>(subchannel_list()->policy());
  GPR_ASSERT(subchannel() != nullptr);
  // If the new state is TRANSIENT_FAILURE, re-resolve.
  // Only do this if we've started watching, not at startup time.
  // Otherwise, if the subchannel was already in state TRANSIENT_FAILURE
  // when the subchannel list was created, we'd wind up in a constant
  // loop of re-resolution.
  if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    if (grpc_lb_round_robin_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[RR %p] Subchannel %p has gone into TRANSIENT_FAILURE. "
              "Requesting re-resolution",
              p, subchannel());
    }
    p->TryReresolutionLocked(&grpc_lb_round_robin_trace, GRPC_ERROR_NONE);
  }
  // Update state counters.
  UpdateConnectivityStateLocked(connectivity_state, error);
  // Update overall state and renew notification.
  subchannel_list()->UpdateRoundRobinStateFromSubchannelStateCountsLocked();
  RenewConnectivityWatchLocked();
}

/** Returns the index into p->subchannel_list->subchannels of the next
 * subchannel in READY state, or p->subchannel_list->num_subchannels if no
 * subchannel is READY.
 *
 * Note that this function does *not* update p->last_ready_subchannel_index.
 * The caller must do that if it returns a pick. */
size_t
RoundRobin::RoundRobinSubchannelList::GetNextReadySubchannelIndexLocked() {
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[RR %p] getting next ready subchannel (out of %" PRIuPTR
            "), last_ready_index=%" PRIuPTR,
            policy(), num_subchannels(), last_ready_index_);
  }
  for (size_t i = 0; i < num_subchannels(); ++i) {
    const size_t index = (i + last_ready_index_ + 1) % num_subchannels();
    if (grpc_lb_round_robin_trace.enabled()) {
      gpr_log(
          GPR_INFO,
          "[RR %p] checking subchannel %p, subchannel_list %p, index %" PRIuPTR
          ": state=%s",
          policy(), subchannel(index)->subchannel(), this, index,
          grpc_connectivity_state_name(
              subchannel(index)->connectivity_state()));
    }
    if (subchannel(index)->connectivity_state() == GRPC_CHANNEL_READY) {
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[RR %p] found next ready subchannel (%p) at index %" PRIuPTR
                " of subchannel_list %p",
                policy(), subchannel(index)->subchannel(), index, this);
      }
      return index;
    }
  }
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO, "[RR %p] no subchannels in ready state", this);
  }
  return num_subchannels();
}

// Sets last_ready_index_ to last_ready_index.
void RoundRobin::RoundRobinSubchannelList::UpdateLastReadySubchannelIndexLocked(
    size_t last_ready_index) {
  GPR_ASSERT(last_ready_index < num_subchannels());
  last_ready_index_ = last_ready_index;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[RR %p] setting last_ready_subchannel_index=%" PRIuPTR
            " (SC %p, CSC %p)",
            policy(), last_ready_index,
            subchannel(last_ready_index)->subchannel(),
            subchannel(last_ready_index)->connected_subchannel());
  }
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
  const size_t next_ready_index =
      subchannel_list_->GetNextReadySubchannelIndexLocked();
  if (next_ready_index < subchannel_list_->num_subchannels()) {
    RoundRobinSubchannelData* selected =
        subchannel_list_->subchannel(next_ready_index);
    selected->connected_subchannel()->Ping(on_initiate, on_ack);
  } else {
    GRPC_CLOSURE_SCHED(on_initiate, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                        "Round Robin not connected"));
    GRPC_CLOSURE_SCHED(on_ack, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                   "Round Robin not connected"));
  }
}

void RoundRobin::UpdateLocked(const grpc_channel_args& args) {
  const grpc_arg* arg = grpc_channel_args_find(&args, GRPC_ARG_LB_ADDRESSES);
  if (GPR_UNLIKELY(arg == nullptr || arg->type != GRPC_ARG_POINTER)) {
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
  grpc_lb_addresses* addresses =
      static_cast<grpc_lb_addresses*>(arg->value.pointer.p);
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO, "[RR %p] received update with %" PRIuPTR " addresses",
            this, addresses->num_addresses);
  }
  // Replace latest_pending_subchannel_list_.
  if (latest_pending_subchannel_list_ != nullptr) {
    if (grpc_lb_round_robin_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[RR %p] Shutting down previous pending subchannel list %p", this,
              latest_pending_subchannel_list_.get());
    }
  }
  latest_pending_subchannel_list_ = MakeOrphanable<RoundRobinSubchannelList>(
      this, &grpc_lb_round_robin_trace, addresses, combiner(),
      client_channel_factory(), args);
  // If we haven't started picking yet or the new list is empty,
  // immediately promote the new list to the current list.
  if (!started_picking_ ||
      latest_pending_subchannel_list_->num_subchannels() == 0) {
    if (latest_pending_subchannel_list_->num_subchannels() == 0) {
      grpc_connectivity_state_set(
          &state_tracker_, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
          "rr_update_empty");
    }
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
  } else {
    // If we've started picking, start watching the new list.
    latest_pending_subchannel_list_->StartWatchingLocked();
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
