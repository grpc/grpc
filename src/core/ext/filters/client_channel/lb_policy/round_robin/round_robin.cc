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
#include "src/core/lib/gpr++/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/static_metadata.h"

grpc_core::TraceFlag grpc_lb_round_robin_trace(false, "round_robin");

typedef struct round_robin_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  grpc_lb_subchannel_list* subchannel_list;

  /** have we started picking? */
  bool started_picking;
  /** are we shutting down? */
  bool shutdown;
  /** List of picks that are waiting on connectivity */
  grpc_lb_policy_pick_state* pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  /** Index into subchannels for last pick. */
  size_t last_ready_subchannel_index;

  /** Latest version of the subchannel list.
   * Subchannel connectivity callbacks will only promote updated subchannel
   * lists if they equal \a latest_pending_subchannel_list. In other words,
   * racing callbacks that reference outdated subchannel lists won't perform any
   * update. */
  grpc_lb_subchannel_list* latest_pending_subchannel_list;
} round_robin_lb_policy;

/** Returns the index into p->subchannel_list->subchannels of the next
 * subchannel in READY state, or p->subchannel_list->num_subchannels if no
 * subchannel is READY.
 *
 * Note that this function does *not* update p->last_ready_subchannel_index.
 * The caller must do that if it returns a pick. */
static size_t get_next_ready_subchannel_index_locked(
    const round_robin_lb_policy* p) {
  GPR_ASSERT(p->subchannel_list != nullptr);
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[RR %p] getting next ready subchannel (out of %lu), "
            "last_ready_subchannel_index=%lu",
            (void*)p, (unsigned long)p->subchannel_list->num_subchannels,
            (unsigned long)p->last_ready_subchannel_index);
  }
  for (size_t i = 0; i < p->subchannel_list->num_subchannels; ++i) {
    const size_t index = (i + p->last_ready_subchannel_index + 1) %
                         p->subchannel_list->num_subchannels;
    if (grpc_lb_round_robin_trace.enabled()) {
      gpr_log(
          GPR_DEBUG,
          "[RR %p] checking subchannel %p, subchannel_list %p, index %lu: "
          "state=%s",
          (void*)p, (void*)p->subchannel_list->subchannels[index].subchannel,
          (void*)p->subchannel_list, (unsigned long)index,
          grpc_connectivity_state_name(
              p->subchannel_list->subchannels[index].curr_connectivity_state));
    }
    if (p->subchannel_list->subchannels[index].curr_connectivity_state ==
        GRPC_CHANNEL_READY) {
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "[RR %p] found next ready subchannel (%p) at index %lu of "
                "subchannel_list %p",
                (void*)p,
                (void*)p->subchannel_list->subchannels[index].subchannel,
                (unsigned long)index, (void*)p->subchannel_list);
      }
      return index;
    }
  }
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] no subchannels in ready state", (void*)p);
  }
  return p->subchannel_list->num_subchannels;
}

// Sets p->last_ready_subchannel_index to last_ready_index.
static void update_last_ready_subchannel_index_locked(round_robin_lb_policy* p,
                                                      size_t last_ready_index) {
  GPR_ASSERT(last_ready_index < p->subchannel_list->num_subchannels);
  p->last_ready_subchannel_index = last_ready_index;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "[RR %p] setting last_ready_subchannel_index=%lu (SC %p, CSC %p)",
            (void*)p, (unsigned long)last_ready_index,
            (void*)p->subchannel_list->subchannels[last_ready_index].subchannel,
            (void*)p->subchannel_list->subchannels[last_ready_index]
                .connected_subchannel);
  }
}

static void rr_destroy(grpc_lb_policy* pol) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] Destroying Round Robin policy at %p",
            (void*)pol, (void*)pol);
  }
  GPR_ASSERT(p->subchannel_list == nullptr);
  GPR_ASSERT(p->latest_pending_subchannel_list == nullptr);
  grpc_connectivity_state_destroy(&p->state_tracker);
  grpc_subchannel_index_unref();
  gpr_free(p);
}

static void rr_shutdown_locked(grpc_lb_policy* pol,
                               grpc_lb_policy* new_policy) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  grpc_error* error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown");
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] Shutting down", p);
  }
  p->shutdown = true;
  grpc_lb_policy_pick_state* pick;
  while ((pick = p->pending_picks) != nullptr) {
    p->pending_picks = pick->next;
    if (new_policy != nullptr) {
      // Hand off to new LB policy.
      if (grpc_lb_policy_pick_locked(new_policy, pick)) {
        // Synchronous return; schedule callback.
        GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
      }
    } else {
      pick->connected_subchannel = nullptr;
      GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_REF(error));
    }
  }
  grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_SHUTDOWN,
                              GRPC_ERROR_REF(error), "rr_shutdown");
  if (p->subchannel_list != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(p->subchannel_list,
                                               "sl_shutdown_rr_shutdown");
    p->subchannel_list = nullptr;
  }
  if (p->latest_pending_subchannel_list != nullptr) {
    grpc_lb_subchannel_list_shutdown_and_unref(
        p->latest_pending_subchannel_list, "sl_shutdown_pending_rr_shutdown");
    p->latest_pending_subchannel_list = nullptr;
  }
  grpc_lb_policy_try_reresolve(&p->base, &grpc_lb_round_robin_trace,
                               GRPC_ERROR_CANCELLED);
  GRPC_ERROR_UNREF(error);
}

static void rr_cancel_pick_locked(grpc_lb_policy* pol,
                                  grpc_lb_policy_pick_state* pick,
                                  grpc_error* error) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  grpc_lb_policy_pick_state* pp = p->pending_picks;
  p->pending_picks = nullptr;
  while (pp != nullptr) {
    grpc_lb_policy_pick_state* next = pp->next;
    if (pp == pick) {
      pick->connected_subchannel = nullptr;
      GRPC_CLOSURE_SCHED(pick->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick cancelled", &error, 1));
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void rr_cancel_picks_locked(grpc_lb_policy* pol,
                                   uint32_t initial_metadata_flags_mask,
                                   uint32_t initial_metadata_flags_eq,
                                   grpc_error* error) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  grpc_lb_policy_pick_state* pick = p->pending_picks;
  p->pending_picks = nullptr;
  while (pick != nullptr) {
    grpc_lb_policy_pick_state* next = pick->next;
    if ((pick->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      pick->connected_subchannel = nullptr;
      GRPC_CLOSURE_SCHED(pick->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick cancelled", &error, 1));
    } else {
      pick->next = p->pending_picks;
      p->pending_picks = pick;
    }
    pick = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void start_picking_locked(round_robin_lb_policy* p) {
  p->started_picking = true;
  for (size_t i = 0; i < p->subchannel_list->num_subchannels; i++) {
    if (p->subchannel_list->subchannels[i].subchannel != nullptr) {
      grpc_lb_subchannel_list_ref_for_connectivity_watch(p->subchannel_list,
                                                         "connectivity_watch");
      grpc_lb_subchannel_data_start_connectivity_watch(
          &p->subchannel_list->subchannels[i]);
    }
  }
}

static void rr_exit_idle_locked(grpc_lb_policy* pol) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  if (!p->started_picking) {
    start_picking_locked(p);
  }
}

static int rr_pick_locked(grpc_lb_policy* pol,
                          grpc_lb_policy_pick_state* pick) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_INFO, "[RR %p] Trying to pick (shutdown: %d)", pol,
            p->shutdown);
  }
  GPR_ASSERT(!p->shutdown);
  if (p->subchannel_list != nullptr) {
    const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
    if (next_ready_index < p->subchannel_list->num_subchannels) {
      /* readily available, report right away */
      grpc_lb_subchannel_data* sd =
          &p->subchannel_list->subchannels[next_ready_index];
      pick->connected_subchannel =
          GRPC_CONNECTED_SUBCHANNEL_REF(sd->connected_subchannel, "rr_picked");
      if (pick->user_data != nullptr) {
        *pick->user_data = sd->user_data;
      }
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(
            GPR_DEBUG,
            "[RR %p] Picked target <-- Subchannel %p (connected %p) (sl %p, "
            "index %" PRIuPTR ")",
            p, sd->subchannel, pick->connected_subchannel, sd->subchannel_list,
            next_ready_index);
      }
      /* only advance the last picked pointer if the selection was used */
      update_last_ready_subchannel_index_locked(p, next_ready_index);
      return 1;
    }
  }
  /* no pick currently available. Save for later in list of pending picks */
  if (!p->started_picking) {
    start_picking_locked(p);
  }
  pick->next = p->pending_picks;
  p->pending_picks = pick;
  return 0;
}

static void update_state_counters_locked(grpc_lb_subchannel_data* sd) {
  grpc_lb_subchannel_list* subchannel_list = sd->subchannel_list;
  if (sd->prev_connectivity_state == GRPC_CHANNEL_READY) {
    GPR_ASSERT(subchannel_list->num_ready > 0);
    --subchannel_list->num_ready;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    GPR_ASSERT(subchannel_list->num_transient_failures > 0);
    --subchannel_list->num_transient_failures;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
    GPR_ASSERT(subchannel_list->num_shutdown > 0);
    --subchannel_list->num_shutdown;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_IDLE) {
    GPR_ASSERT(subchannel_list->num_idle > 0);
    --subchannel_list->num_idle;
  }
  sd->prev_connectivity_state = sd->curr_connectivity_state;
  if (sd->curr_connectivity_state == GRPC_CHANNEL_READY) {
    ++subchannel_list->num_ready;
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++subchannel_list->num_transient_failures;
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
    ++subchannel_list->num_shutdown;
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_IDLE) {
    ++subchannel_list->num_idle;
  }
}

/** Sets the policy's connectivity status based on that of the passed-in \a sd
 * (the grpc_lb_subchannel_data associated with the updated subchannel) and the
 * subchannel list \a sd belongs to (sd->subchannel_list). \a error will be used
 * only if the policy transitions to state TRANSIENT_FAILURE. */
static void update_lb_connectivity_status_locked(grpc_lb_subchannel_data* sd,
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
   * 3) RULE: ALL subchannels are SHUTDOWN => policy is IDLE (and requests
   *          re-resolution).
   *    CHECK: subchannel_list->num_shutdown ==
   *           subchannel_list->num_subchannels.
   *
   * 4) RULE: ALL subchannels are SHUTDOWN or TRANSIENT_FAILURE => policy is
   *          TRANSIENT_FAILURE.
   *    CHECK: subchannel_list->num_shutdown +
   *             subchannel_list->num_transient_failures ==
   *           subchannel_list->num_subchannels.
   */
  // TODO(juanlishen): For rule 4, we may want to re-resolve instead.
  grpc_lb_subchannel_list* subchannel_list = sd->subchannel_list;
  round_robin_lb_policy* p = (round_robin_lb_policy*)subchannel_list->policy;
  GPR_ASSERT(sd->curr_connectivity_state != GRPC_CHANNEL_IDLE);
  if (subchannel_list->num_ready > 0) {
    /* 1) READY */
    grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_READY,
                                GRPC_ERROR_NONE, "rr_ready");
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_CONNECTING) {
    /* 2) CONNECTING */
    grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_CONNECTING,
                                GRPC_ERROR_NONE, "rr_connecting");
  } else if (subchannel_list->num_shutdown ==
             subchannel_list->num_subchannels) {
    /* 3) IDLE and re-resolve */
    grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_IDLE,
                                GRPC_ERROR_NONE,
                                "rr_exhausted_subchannels+reresolve");
    p->started_picking = false;
    grpc_lb_policy_try_reresolve(&p->base, &grpc_lb_round_robin_trace,
                                 GRPC_ERROR_NONE);
  } else if (subchannel_list->num_shutdown +
                 subchannel_list->num_transient_failures ==
             subchannel_list->num_subchannels) {
    /* 4) TRANSIENT_FAILURE */
    grpc_connectivity_state_set(&p->state_tracker,
                                GRPC_CHANNEL_TRANSIENT_FAILURE,
                                GRPC_ERROR_REF(error), "rr_transient_failure");
  }
  GRPC_ERROR_UNREF(error);
}

static void rr_connectivity_changed_locked(void* arg, grpc_error* error) {
  grpc_lb_subchannel_data* sd = (grpc_lb_subchannel_data*)arg;
  round_robin_lb_policy* p =
      (round_robin_lb_policy*)sd->subchannel_list->policy;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(
        GPR_DEBUG,
        "[RR %p] connectivity changed for subchannel %p, subchannel_list %p: "
        "prev_state=%s new_state=%s p->shutdown=%d "
        "sd->subchannel_list->shutting_down=%d error=%s",
        (void*)p, (void*)sd->subchannel, (void*)sd->subchannel_list,
        grpc_connectivity_state_name(sd->prev_connectivity_state),
        grpc_connectivity_state_name(sd->pending_connectivity_state_unsafe),
        p->shutdown, sd->subchannel_list->shutting_down,
        grpc_error_string(error));
  }
  // If the policy is shutting down, unref and return.
  if (p->shutdown) {
    grpc_lb_subchannel_data_stop_connectivity_watch(sd);
    grpc_lb_subchannel_data_unref_subchannel(sd, "rr_shutdown");
    grpc_lb_subchannel_list_unref_for_connectivity_watch(sd->subchannel_list,
                                                         "rr_shutdown");
    return;
  }
  // If the subchannel list is shutting down, stop watching.
  if (sd->subchannel_list->shutting_down || error == GRPC_ERROR_CANCELLED) {
    grpc_lb_subchannel_data_stop_connectivity_watch(sd);
    grpc_lb_subchannel_data_unref_subchannel(sd, "rr_sl_shutdown");
    grpc_lb_subchannel_list_unref_for_connectivity_watch(sd->subchannel_list,
                                                         "rr_sl_shutdown");
    return;
  }
  // If we're still here, the notification must be for a subchannel in
  // either the current or latest pending subchannel lists.
  GPR_ASSERT(sd->subchannel_list == p->subchannel_list ||
             sd->subchannel_list == p->latest_pending_subchannel_list);
  // Now that we're inside the combiner, copy the pending connectivity
  // state (which was set by the connectivity state watcher) to
  // curr_connectivity_state, which is what we use inside of the combiner.
  sd->curr_connectivity_state = sd->pending_connectivity_state_unsafe;
  // Update state counters and new overall state.
  update_state_counters_locked(sd);
  update_lb_connectivity_status_locked(sd, GRPC_ERROR_REF(error));
  // If the sd's new state is SHUTDOWN, unref the subchannel.
  if (sd->curr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
    grpc_lb_subchannel_data_stop_connectivity_watch(sd);
    grpc_lb_subchannel_data_unref_subchannel(sd, "rr_connectivity_shutdown");
    grpc_lb_subchannel_list_unref_for_connectivity_watch(
        sd->subchannel_list, "rr_connectivity_shutdown");
  } else {  // sd not in SHUTDOWN
    if (sd->curr_connectivity_state == GRPC_CHANNEL_READY) {
      if (sd->connected_subchannel == nullptr) {
        sd->connected_subchannel = GRPC_CONNECTED_SUBCHANNEL_REF(
            grpc_subchannel_get_connected_subchannel(sd->subchannel),
            "connected");
      }
      if (sd->subchannel_list != p->subchannel_list) {
        // promote sd->subchannel_list to p->subchannel_list.
        // sd->subchannel_list must be equal to
        // p->latest_pending_subchannel_list because we have already filtered
        // for sds belonging to outdated subchannel lists.
        GPR_ASSERT(sd->subchannel_list == p->latest_pending_subchannel_list);
        GPR_ASSERT(!sd->subchannel_list->shutting_down);
        if (grpc_lb_round_robin_trace.enabled()) {
          const unsigned long num_subchannels =
              p->subchannel_list != nullptr
                  ? (unsigned long)p->subchannel_list->num_subchannels
                  : 0;
          gpr_log(GPR_DEBUG,
                  "[RR %p] phasing out subchannel list %p (size %lu) in favor "
                  "of %p (size %lu)",
                  (void*)p, (void*)p->subchannel_list, num_subchannels,
                  (void*)sd->subchannel_list, num_subchannels);
        }
        if (p->subchannel_list != nullptr) {
          // dispose of the current subchannel_list
          grpc_lb_subchannel_list_shutdown_and_unref(p->subchannel_list,
                                                     "sl_phase_out_shutdown");
        }
        p->subchannel_list = p->latest_pending_subchannel_list;
        p->latest_pending_subchannel_list = nullptr;
      }
      /* at this point we know there's at least one suitable subchannel. Go
       * ahead and pick one and notify the pending suitors in
       * p->pending_picks. This preemptively replicates rr_pick()'s actions. */
      const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
      GPR_ASSERT(next_ready_index < p->subchannel_list->num_subchannels);
      grpc_lb_subchannel_data* selected =
          &p->subchannel_list->subchannels[next_ready_index];
      if (p->pending_picks != nullptr) {
        // if the selected subchannel is going to be used for the pending
        // picks, update the last picked pointer
        update_last_ready_subchannel_index_locked(p, next_ready_index);
      }
      grpc_lb_policy_pick_state* pick;
      while ((pick = p->pending_picks)) {
        p->pending_picks = pick->next;
        pick->connected_subchannel = GRPC_CONNECTED_SUBCHANNEL_REF(
            selected->connected_subchannel, "rr_picked");
        if (pick->user_data != nullptr) {
          *pick->user_data = selected->user_data;
        }
        if (grpc_lb_round_robin_trace.enabled()) {
          gpr_log(GPR_DEBUG,
                  "[RR %p] Fulfilling pending pick. Target <-- subchannel %p "
                  "(subchannel_list %p, index %lu)",
                  (void*)p, (void*)selected->subchannel,
                  (void*)p->subchannel_list, (unsigned long)next_ready_index);
        }
        GRPC_CLOSURE_SCHED(pick->on_complete, GRPC_ERROR_NONE);
      }
    }
    // Renew notification.
    grpc_lb_subchannel_data_start_connectivity_watch(sd);
  }
}

static grpc_connectivity_state rr_check_connectivity_locked(
    grpc_lb_policy* pol, grpc_error** error) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  return grpc_connectivity_state_get(&p->state_tracker, error);
}

static void rr_notify_on_state_change_locked(grpc_lb_policy* pol,
                                             grpc_connectivity_state* current,
                                             grpc_closure* notify) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  grpc_connectivity_state_notify_on_state_change(&p->state_tracker, current,
                                                 notify);
}

static void rr_ping_one_locked(grpc_lb_policy* pol, grpc_closure* on_initiate,
                               grpc_closure* on_ack) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)pol;
  const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
  if (next_ready_index < p->subchannel_list->num_subchannels) {
    grpc_lb_subchannel_data* selected =
        &p->subchannel_list->subchannels[next_ready_index];
    grpc_connected_subchannel* target = GRPC_CONNECTED_SUBCHANNEL_REF(
        selected->connected_subchannel, "rr_ping");
    grpc_connected_subchannel_ping(target, on_initiate, on_ack);
    GRPC_CONNECTED_SUBCHANNEL_UNREF(target, "rr_ping");
  } else {
    GRPC_CLOSURE_SCHED(on_initiate, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                        "Round Robin not connected"));
    GRPC_CLOSURE_SCHED(on_ack, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                   "Round Robin not connected"));
  }
}

static void rr_update_locked(grpc_lb_policy* policy,
                             const grpc_lb_policy_args* args) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)policy;
  const grpc_arg* arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == nullptr || arg->type != GRPC_ARG_POINTER) {
    gpr_log(GPR_ERROR, "[RR %p] update provided no addresses; ignoring", p);
    // If we don't have a current subchannel list, go into TRANSIENT_FAILURE.
    // Otherwise, keep using the current subchannel list (ignore this update).
    if (p->subchannel_list == nullptr) {
      grpc_connectivity_state_set(
          &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "rr_update_missing");
    }
    return;
  }
  grpc_lb_addresses* addresses = (grpc_lb_addresses*)arg->value.pointer.p;
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] received update with %" PRIuPTR " addresses", p,
            addresses->num_addresses);
  }
  grpc_lb_subchannel_list* subchannel_list = grpc_lb_subchannel_list_create(
      &p->base, &grpc_lb_round_robin_trace, addresses, args,
      rr_connectivity_changed_locked);
  if (subchannel_list->num_subchannels == 0) {
    grpc_connectivity_state_set(
        &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
        "rr_update_empty");
    if (p->subchannel_list != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(p->subchannel_list,
                                                 "sl_shutdown_empty_update");
    }
    p->subchannel_list = subchannel_list;  // empty list
    return;
  }
  if (p->started_picking) {
    if (p->latest_pending_subchannel_list != nullptr) {
      if (grpc_lb_round_robin_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "[RR %p] Shutting down latest pending subchannel list %p, "
                "about to be replaced by newer latest %p",
                (void*)p, (void*)p->latest_pending_subchannel_list,
                (void*)subchannel_list);
      }
      grpc_lb_subchannel_list_shutdown_and_unref(
          p->latest_pending_subchannel_list, "sl_outdated");
    }
    p->latest_pending_subchannel_list = subchannel_list;
    for (size_t i = 0; i < subchannel_list->num_subchannels; ++i) {
      /* Watch every new subchannel. A subchannel list becomes active the
       * moment one of its subchannels is READY. At that moment, we swap
       * p->subchannel_list for sd->subchannel_list, provided the subchannel
       * list is still valid (ie, isn't shutting down) */
      grpc_lb_subchannel_list_ref_for_connectivity_watch(subchannel_list,
                                                         "connectivity_watch");
      grpc_lb_subchannel_data_start_connectivity_watch(
          &subchannel_list->subchannels[i]);
    }
  } else {
    // The policy isn't picking yet. Save the update for later, disposing of
    // previous version if any.
    if (p->subchannel_list != nullptr) {
      grpc_lb_subchannel_list_shutdown_and_unref(
          p->subchannel_list, "rr_update_before_started_picking");
    }
    p->subchannel_list = subchannel_list;
  }
}

static void rr_set_reresolve_closure_locked(
    grpc_lb_policy* policy, grpc_closure* request_reresolution) {
  round_robin_lb_policy* p = (round_robin_lb_policy*)policy;
  GPR_ASSERT(!p->shutdown);
  GPR_ASSERT(policy->request_reresolution == nullptr);
  policy->request_reresolution = request_reresolution;
}

static const grpc_lb_policy_vtable round_robin_lb_policy_vtable = {
    rr_destroy,
    rr_shutdown_locked,
    rr_pick_locked,
    rr_cancel_pick_locked,
    rr_cancel_picks_locked,
    rr_ping_one_locked,
    rr_exit_idle_locked,
    rr_check_connectivity_locked,
    rr_notify_on_state_change_locked,
    rr_update_locked,
    rr_set_reresolve_closure_locked};

static void round_robin_factory_ref(grpc_lb_policy_factory* factory) {}

static void round_robin_factory_unref(grpc_lb_policy_factory* factory) {}

static grpc_lb_policy* round_robin_create(grpc_lb_policy_factory* factory,
                                          grpc_lb_policy_args* args) {
  GPR_ASSERT(args->client_channel_factory != nullptr);
  round_robin_lb_policy* p = (round_robin_lb_policy*)gpr_zalloc(sizeof(*p));
  grpc_lb_policy_init(&p->base, &round_robin_lb_policy_vtable, args->combiner);
  grpc_subchannel_index_ref();
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE,
                               "round_robin");
  rr_update_locked(&p->base, args);
  if (grpc_lb_round_robin_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[RR %p] Created with %lu subchannels", (void*)p,
            (unsigned long)p->subchannel_list->num_subchannels);
  }
  return &p->base;
}

static const grpc_lb_policy_factory_vtable round_robin_factory_vtable = {
    round_robin_factory_ref, round_robin_factory_unref, round_robin_create,
    "round_robin"};

static grpc_lb_policy_factory round_robin_lb_policy_factory = {
    &round_robin_factory_vtable};

static grpc_lb_policy_factory* round_robin_lb_factory_create() {
  return &round_robin_lb_policy_factory;
}

/* Plugin registration */

void grpc_lb_policy_round_robin_init() {
  grpc_register_lb_policy(round_robin_lb_factory_create());
}

void grpc_lb_policy_round_robin_shutdown() {}
