/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/** Round Robin Policy.
 *
 * This policy keeps:
 * - A circular list of ready (connected) subchannels, the *readylist*. An empty
 *   readylist consists solely of its root (dummy) node.
 * - A pointer to the last element picked from the readylist, the *lastpick*.
 *   Initially set to point to the readylist's root.
 *
 * Behavior:
 * - When a subchannel connects, it's *prepended* to the readylist's root node.
 *   Ie, if readylist = A <-> B <-> ROOT <-> C
 *                      ^                    ^
 *                      |____________________|
 *   and subchannel D becomes connected, the addition of D to the readylist
 *   results in  readylist = A <-> B <-> D <-> ROOT <-> C
 *                           ^                          ^
 *                           |__________________________|
 * - When a subchannel disconnects, it's removed from the readylist. If the
 *   subchannel being removed was the most recently picked, the *lastpick*
 *   pointer moves to the removed node's previous element. Note that if the
 *   readylist only had one element, this is still legal, as the lastpick would
 *   point to the dummy root node, for an empty readylist.
 * - Upon picking, *lastpick* is updated to point to the returned (connected)
 *   subchannel. Note that it's possible that the selected subchannel becomes
 *   disconnected in the interim between the selection and the actual usage of
 *   the subchannel by the caller.
 */

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/client_channel/lb_policy_registry.h"
#include "src/core/ext/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/static_metadata.h"

typedef struct round_robin_lb_policy round_robin_lb_policy;

int grpc_lb_round_robin_trace = 0;

/** List of entities waiting for a pick.
 *
 * Once a pick is available, \a target is updated and \a on_complete called. */
typedef struct pending_pick {
  struct pending_pick *next;

  /* output argument where to store the pick()ed user_data. It'll be NULL if no
   * such data is present or there's an error (the definite test for errors is
   * \a target being NULL). */
  void **user_data;

  /* bitmask passed to pick() and used for selective cancelling. See
   * grpc_lb_policy_cancel_picks() */
  uint32_t initial_metadata_flags;

  /* output argument where to store the pick()ed connected subchannel, or NULL
   * upon error. */
  grpc_connected_subchannel **target;

  /* to be invoked once the pick() has completed (regardless of success) */
  grpc_closure *on_complete;
} pending_pick;

/** List of subchannels in a connectivity READY state */
typedef struct ready_list {
  grpc_subchannel *subchannel;
  /* references namesake entry in subchannel_data */
  void *user_data;
  struct ready_list *next;
  struct ready_list *prev;
} ready_list;

typedef struct {
  /** index within policy->subchannels */
  size_t index;
  /** backpointer to owning policy */
  round_robin_lb_policy *policy;
  /** subchannel itself */
  grpc_subchannel *subchannel;
  /** notification that connectivity has changed on subchannel */
  grpc_closure connectivity_changed_closure;
  /** this subchannels current position in subchannel->ready_list */
  ready_list *ready_list_node;
  /** last observed connectivity. Not updated by
   * \a grpc_subchannel_notify_on_state_change. Used to determine the previous
   * state while processing the new state in \a rr_connectivity_changed */
  grpc_connectivity_state prev_connectivity_state;
  /** current connectivity state. Updated by \a
   * grpc_subchannel_notify_on_state_change */
  grpc_connectivity_state curr_connectivity_state;
  /** the subchannel's target user data */
  void *user_data;
  /** vtable to operate over \a user_data */
  const grpc_lb_user_data_vtable *user_data_vtable;
} subchannel_data;

struct round_robin_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  /** total number of addresses received at creation time */
  size_t num_addresses;

  /** all our subchannels */
  size_t num_subchannels;
  subchannel_data **subchannels;

  /** how many subchannels are in TRANSIENT_FAILURE */
  size_t num_transient_failures;
  /** how many subchannels are IDLE */
  size_t num_idle;

  /** have we started picking? */
  int started_picking;
  /** are we shutting down? */
  int shutdown;
  /** List of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  /** (Dummy) root of the doubly linked list containing READY subchannels */
  ready_list ready_list;
  /** Last pick from the ready list. */
  ready_list *ready_list_last_pick;
};

/** Returns the next subchannel from the connected list or NULL if the list is
 * empty.
 *
 * Note that this function does *not* advance p->ready_list_last_pick. Use \a
 * advance_last_picked_locked() for that. */
static ready_list *peek_next_connected_locked(const round_robin_lb_policy *p) {
  ready_list *selected;
  selected = p->ready_list_last_pick->next;

  while (selected != NULL) {
    if (selected == &p->ready_list) {
      GPR_ASSERT(selected->subchannel == NULL);
      /* skip dummy root */
      selected = selected->next;
    } else {
      GPR_ASSERT(selected->subchannel != NULL);
      return selected;
    }
  }
  return NULL;
}

/** Advance the \a ready_list picking head. */
static void advance_last_picked_locked(round_robin_lb_policy *p) {
  if (p->ready_list_last_pick->next != NULL) { /* non-empty list */
    p->ready_list_last_pick = p->ready_list_last_pick->next;
    if (p->ready_list_last_pick == &p->ready_list) {
      /* skip dummy root */
      p->ready_list_last_pick = p->ready_list_last_pick->next;
    }
  } else { /* should be an empty list */
    GPR_ASSERT(p->ready_list_last_pick == &p->ready_list);
  }

  if (grpc_lb_round_robin_trace) {
    gpr_log(GPR_DEBUG,
            "[READYLIST, RR: %p] ADVANCED LAST PICK. NOW AT NODE %p (SC %p, "
            "CSC %p)",
            (void *)p, (void *)p->ready_list_last_pick,
            (void *)p->ready_list_last_pick->subchannel,
            (void *)grpc_subchannel_get_connected_subchannel(
                p->ready_list_last_pick->subchannel));
  }
}

/** Prepends (relative to the root at p->ready_list) the connected subchannel \a
 * csc to the list of ready subchannels. */
static ready_list *add_connected_sc_locked(round_robin_lb_policy *p,
                                           subchannel_data *sd) {
  ready_list *new_elem = gpr_zalloc(sizeof(ready_list));
  new_elem->subchannel = sd->subchannel;
  new_elem->user_data = sd->user_data;
  if (p->ready_list.prev == NULL) {
    /* first element */
    new_elem->next = &p->ready_list;
    new_elem->prev = &p->ready_list;
    p->ready_list.next = new_elem;
    p->ready_list.prev = new_elem;
  } else {
    new_elem->next = &p->ready_list;
    new_elem->prev = p->ready_list.prev;
    p->ready_list.prev->next = new_elem;
    p->ready_list.prev = new_elem;
  }
  if (grpc_lb_round_robin_trace) {
    gpr_log(GPR_DEBUG, "[READYLIST] ADDING NODE %p (Conn. SC %p)",
            (void *)new_elem, (void *)sd->subchannel);
  }
  return new_elem;
}

/** Removes \a node from the list of connected subchannels */
static void remove_disconnected_sc_locked(round_robin_lb_policy *p,
                                          ready_list *node) {
  if (node == NULL) {
    return;
  }
  if (node == p->ready_list_last_pick) {
    p->ready_list_last_pick = p->ready_list_last_pick->prev;
  }

  /* removing last item */
  if (node->next == &p->ready_list && node->prev == &p->ready_list) {
    GPR_ASSERT(p->ready_list.next == node);
    GPR_ASSERT(p->ready_list.prev == node);
    p->ready_list.next = NULL;
    p->ready_list.prev = NULL;
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;
  }

  if (grpc_lb_round_robin_trace) {
    gpr_log(GPR_DEBUG, "[READYLIST] REMOVED NODE %p (SC %p)", (void *)node,
            (void *)node->subchannel);
  }

  node->next = NULL;
  node->prev = NULL;
  node->subchannel = NULL;

  gpr_free(node);
}

static bool is_ready_list_empty(round_robin_lb_policy *p) {
  return p->ready_list.prev == NULL;
}

static void rr_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  ready_list *elem;

  if (grpc_lb_round_robin_trace) {
    gpr_log(GPR_DEBUG, "Destroying Round Robin policy at %p", (void *)pol);
  }

  for (size_t i = 0; i < p->num_subchannels; i++) {
    subchannel_data *sd = p->subchannels[i];
    GRPC_SUBCHANNEL_UNREF(exec_ctx, sd->subchannel, "rr_destroy");
    if (sd->user_data != NULL) {
      GPR_ASSERT(sd->user_data_vtable != NULL);
      sd->user_data_vtable->destroy(exec_ctx, sd->user_data);
    }
    gpr_free(sd);
  }

  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  gpr_free(p->subchannels);

  elem = p->ready_list.next;
  while (elem != NULL && elem != &p->ready_list) {
    ready_list *tmp;
    tmp = elem->next;
    elem->next = NULL;
    elem->prev = NULL;
    elem->subchannel = NULL;
    gpr_free(elem);
    elem = tmp;
  }

  gpr_free(p);
}

static void rr_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp;
  size_t i;

  if (grpc_lb_round_robin_trace) {
    gpr_log(GPR_DEBUG, "Shutting down Round Robin policy at %p", (void *)pol);
  }

  p->shutdown = 1;
  while ((pp = p->pending_picks)) {
    p->pending_picks = pp->next;
    *pp->target = NULL;
    grpc_closure_sched(exec_ctx, pp->on_complete,
                       GRPC_ERROR_CREATE("Channel Shutdown"));
    gpr_free(pp);
  }
  grpc_connectivity_state_set(
      exec_ctx, &p->state_tracker, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE("Channel Shutdown"), "rr_shutdown");
  for (i = 0; i < p->num_subchannels; i++) {
    subchannel_data *sd = p->subchannels[i];
    grpc_subchannel_notify_on_state_change(exec_ctx, sd->subchannel, NULL, NULL,
                                           &sd->connectivity_changed_closure);
  }
}

static void rr_cancel_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                                  grpc_connected_subchannel **target,
                                  grpc_error *error) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp;
  pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      *target = NULL;
      grpc_closure_sched(
          exec_ctx, pp->on_complete,
          GRPC_ERROR_CREATE_REFERENCING("Pick cancelled", &error, 1));
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void rr_cancel_picks_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                                   uint32_t initial_metadata_flags_mask,
                                   uint32_t initial_metadata_flags_eq,
                                   grpc_error *error) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp;
  pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if ((pp->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      *pp->target = NULL;
      grpc_closure_sched(
          exec_ctx, pp->on_complete,
          GRPC_ERROR_CREATE_REFERENCING("Pick cancelled", &error, 1));
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void start_picking_locked(grpc_exec_ctx *exec_ctx,
                                 round_robin_lb_policy *p) {
  size_t i;
  p->started_picking = 1;

  for (i = 0; i < p->num_subchannels; i++) {
    subchannel_data *sd = p->subchannels[i];
    /* use some sentinel value outside of the range of grpc_connectivity_state
     * to signal an undefined previous state. We won't be referring to this
     * value again and it'll be overwritten after the first call to
     * rr_connectivity_changed */
    sd->prev_connectivity_state = GRPC_CHANNEL_INIT;
    sd->curr_connectivity_state = GRPC_CHANNEL_IDLE;
    GRPC_LB_POLICY_WEAK_REF(&p->base, "rr_connectivity");
    grpc_subchannel_notify_on_state_change(
        exec_ctx, sd->subchannel, p->base.interested_parties,
        &sd->curr_connectivity_state, &sd->connectivity_changed_closure);
  }
}

static void rr_exit_idle_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  if (!p->started_picking) {
    start_picking_locked(exec_ctx, p);
  }
}

static int rr_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                          const grpc_lb_policy_pick_args *pick_args,
                          grpc_connected_subchannel **target, void **user_data,
                          grpc_closure *on_complete) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp;
  ready_list *selected;

  if (grpc_lb_round_robin_trace) {
    gpr_log(GPR_INFO, "Round Robin %p trying to pick", (void *)pol);
  }

  if ((selected = peek_next_connected_locked(p))) {
    /* readily available, report right away */
    *target = GRPC_CONNECTED_SUBCHANNEL_REF(
        grpc_subchannel_get_connected_subchannel(selected->subchannel),
        "rr_picked");

    if (user_data != NULL) {
      *user_data = selected->user_data;
    }
    if (grpc_lb_round_robin_trace) {
      gpr_log(GPR_DEBUG,
              "[RR PICK] TARGET <-- CONNECTED SUBCHANNEL %p (NODE %p)",
              (void *)*target, (void *)selected);
    }
    /* only advance the last picked pointer if the selection was used */
    advance_last_picked_locked(p);
    return 1;
  } else {
    /* no pick currently available. Save for later in list of pending picks */
    if (!p->started_picking) {
      start_picking_locked(exec_ctx, p);
    }
    pp = gpr_malloc(sizeof(*pp));
    pp->next = p->pending_picks;
    pp->target = target;
    pp->on_complete = on_complete;
    pp->initial_metadata_flags = pick_args->initial_metadata_flags;
    pp->user_data = user_data;
    p->pending_picks = pp;
    return 0;
  }
}

static void update_state_counters(subchannel_data *sd) {
  round_robin_lb_policy *p = sd->policy;

  /* update p->num_transient_failures (resp. p->num_idle): if the previous
   * state was TRANSIENT_FAILURE (resp. IDLE), decrement
   * p->num_transient_failures (resp. p->num_idle). */
  if (sd->prev_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    GPR_ASSERT(p->num_transient_failures > 0);
    --p->num_transient_failures;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_IDLE) {
    GPR_ASSERT(p->num_idle > 0);
    --p->num_idle;
  }
}

/* sd is the subchannel_data associted with the updated subchannel.
 * shutdown_error will only be used upon policy transition to TRANSIENT_FAILURE
 * or SHUTDOWN */
static grpc_connectivity_state update_lb_connectivity_status(
    grpc_exec_ctx *exec_ctx, subchannel_data *sd, grpc_error *error) {
  /* In priority order. The first rule to match terminates the search (ie, if we
   * are on rule n, all previous rules were unfulfilled).
   *
   * 1) RULE: ANY subchannel is READY => policy is READY.
   *    CHECK: At least one subchannel is ready iff p->ready_list is NOT empty.
   *
   * 2) RULE: ANY subchannel is CONNECTING => policy is CONNECTING.
   *    CHECK: sd->curr_connectivity_state == CONNECTING.
   *
   * 3) RULE: ALL subchannels are SHUTDOWN => policy is SHUTDOWN.
   *    CHECK: p->num_subchannels = 0.
   *
   * 4) RULE: ALL subchannels are TRANSIENT_FAILURE => policy is
   *    TRANSIENT_FAILURE.
   *    CHECK: p->num_transient_failures == p->num_subchannels.
   *
   * 5) RULE: ALL subchannels are IDLE => policy is IDLE.
   *    CHECK: p->num_idle == p->num_subchannels.
   */
  round_robin_lb_policy *p = sd->policy;
  if (!is_ready_list_empty(p)) { /* 1) READY */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker, GRPC_CHANNEL_READY,
                                GRPC_ERROR_NONE, "rr_ready");
    return GRPC_CHANNEL_READY;
  } else if (sd->curr_connectivity_state ==
             GRPC_CHANNEL_CONNECTING) { /* 2) CONNECTING */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE,
                                "rr_connecting");
    return GRPC_CHANNEL_CONNECTING;
  } else if (p->num_subchannels == 0) { /* 3) SHUTDOWN */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                GRPC_CHANNEL_SHUTDOWN, GRPC_ERROR_REF(error),
                                "rr_shutdown");
    return GRPC_CHANNEL_SHUTDOWN;
  } else if (p->num_transient_failures ==
             p->num_subchannels) { /* 4) TRANSIENT_FAILURE */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                GRPC_CHANNEL_TRANSIENT_FAILURE,
                                GRPC_ERROR_REF(error), "rr_transient_failure");
    return GRPC_CHANNEL_TRANSIENT_FAILURE;
  } else if (p->num_idle == p->num_subchannels) { /* 5) IDLE */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker, GRPC_CHANNEL_IDLE,
                                GRPC_ERROR_NONE, "rr_idle");
    return GRPC_CHANNEL_IDLE;
  }
  /* no change */
  return sd->curr_connectivity_state;
}

static void rr_connectivity_changed_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  subchannel_data *sd = arg;
  round_robin_lb_policy *p = sd->policy;
  pending_pick *pp;

  GRPC_ERROR_REF(error);

  if (p->shutdown) {
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "rr_connectivity");
    GRPC_ERROR_UNREF(error);
    return;
  }
  switch (sd->curr_connectivity_state) {
    case GRPC_CHANNEL_INIT:
      GPR_UNREACHABLE_CODE(return );
    case GRPC_CHANNEL_READY:
      /* add the newly connected subchannel to the list of connected ones.
       * Note that it goes to the "end of the line". */
      sd->ready_list_node = add_connected_sc_locked(p, sd);
      /* at this point we know there's at least one suitable subchannel. Go
       * ahead and pick one and notify the pending suitors in
       * p->pending_picks. This preemtively replicates rr_pick()'s actions. */
      ready_list *selected = peek_next_connected_locked(p);
      GPR_ASSERT(selected != NULL);
      if (p->pending_picks != NULL) {
        /* if the selected subchannel is going to be used for the pending
         * picks, update the last picked pointer */
        advance_last_picked_locked(p);
      }
      while ((pp = p->pending_picks)) {
        p->pending_picks = pp->next;
        *pp->target = GRPC_CONNECTED_SUBCHANNEL_REF(
            grpc_subchannel_get_connected_subchannel(selected->subchannel),
            "rr_picked");
        if (pp->user_data != NULL) {
          *pp->user_data = selected->user_data;
        }
        if (grpc_lb_round_robin_trace) {
          gpr_log(GPR_DEBUG,
                  "[RR CONN CHANGED] TARGET <-- SUBCHANNEL %p (NODE %p)",
                  (void *)selected->subchannel, (void *)selected);
        }
        grpc_closure_sched(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
        gpr_free(pp);
      }
      update_lb_connectivity_status(exec_ctx, sd, error);
      sd->prev_connectivity_state = sd->curr_connectivity_state;
      /* renew notification: reuses the "rr_connectivity" weak ref */
      grpc_subchannel_notify_on_state_change(
          exec_ctx, sd->subchannel, p->base.interested_parties,
          &sd->curr_connectivity_state, &sd->connectivity_changed_closure);
      break;
    case GRPC_CHANNEL_IDLE:
      ++p->num_idle;
    /* fallthrough */
    case GRPC_CHANNEL_CONNECTING:
      update_state_counters(sd);
      update_lb_connectivity_status(exec_ctx, sd, error);
      sd->prev_connectivity_state = sd->curr_connectivity_state;
      /* renew notification: reuses the "rr_connectivity" weak ref */
      grpc_subchannel_notify_on_state_change(
          exec_ctx, sd->subchannel, p->base.interested_parties,
          &sd->curr_connectivity_state, &sd->connectivity_changed_closure);
      break;
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      ++p->num_transient_failures;
      /* remove from ready list if still present */
      if (sd->ready_list_node != NULL) {
        remove_disconnected_sc_locked(p, sd->ready_list_node);
        sd->ready_list_node = NULL;
      }
      update_lb_connectivity_status(exec_ctx, sd, error);
      sd->prev_connectivity_state = sd->curr_connectivity_state;
      /* renew notification: reuses the "rr_connectivity" weak ref */
      grpc_subchannel_notify_on_state_change(
          exec_ctx, sd->subchannel, p->base.interested_parties,
          &sd->curr_connectivity_state, &sd->connectivity_changed_closure);
      break;
    case GRPC_CHANNEL_SHUTDOWN:
      update_state_counters(sd);
      if (sd->ready_list_node != NULL) {
        remove_disconnected_sc_locked(p, sd->ready_list_node);
        sd->ready_list_node = NULL;
      }
      --p->num_subchannels;
      GPR_SWAP(subchannel_data *, p->subchannels[sd->index],
               p->subchannels[p->num_subchannels]);
      GRPC_SUBCHANNEL_UNREF(exec_ctx, sd->subchannel, "rr_subchannel_shutdown");
      p->subchannels[sd->index]->index = sd->index;
      if (update_lb_connectivity_status(exec_ctx, sd, error) ==
          GRPC_CHANNEL_SHUTDOWN) {
        /* the policy is shutting down. Flush all the pending picks... */
        while ((pp = p->pending_picks)) {
          p->pending_picks = pp->next;
          *pp->target = NULL;
          grpc_closure_sched(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
          gpr_free(pp);
        }
      }
      gpr_free(sd);
      /* unref the "rr_connectivity" weak ref from start_picking */
      GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "rr_connectivity");
      break;
  }
  GRPC_ERROR_UNREF(error);
}

static grpc_connectivity_state rr_check_connectivity_locked(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol, grpc_error **error) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  return grpc_connectivity_state_get(&p->state_tracker, error);
}

static void rr_notify_on_state_change_locked(grpc_exec_ctx *exec_ctx,
                                             grpc_lb_policy *pol,
                                             grpc_connectivity_state *current,
                                             grpc_closure *notify) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  grpc_connectivity_state_notify_on_state_change(exec_ctx, &p->state_tracker,
                                                 current, notify);
}

static void rr_ping_one_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                               grpc_closure *closure) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  ready_list *selected;
  grpc_connected_subchannel *target;
  if ((selected = peek_next_connected_locked(p))) {
    target = GRPC_CONNECTED_SUBCHANNEL_REF(
        grpc_subchannel_get_connected_subchannel(selected->subchannel),
        "rr_picked");
    grpc_connected_subchannel_ping(exec_ctx, target, closure);
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, target, "rr_picked");
  } else {
    grpc_closure_sched(exec_ctx, closure,
                       GRPC_ERROR_CREATE("Round Robin not connected"));
  }
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
    rr_notify_on_state_change_locked};

static void round_robin_factory_ref(grpc_lb_policy_factory *factory) {}

static void round_robin_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *round_robin_create(grpc_exec_ctx *exec_ctx,
                                          grpc_lb_policy_factory *factory,
                                          grpc_lb_policy_args *args) {
  GPR_ASSERT(args->client_channel_factory != NULL);

  /* Find the number of backend addresses. We ignore balancer
   * addresses, since we don't know how to handle them. */
  const grpc_arg *arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  GPR_ASSERT(arg != NULL && arg->type == GRPC_ARG_POINTER);
  grpc_lb_addresses *addresses = arg->value.pointer.p;
  size_t num_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    if (!addresses->addresses[i].is_balancer) ++num_addrs;
  }
  if (num_addrs == 0) return NULL;

  round_robin_lb_policy *p = gpr_zalloc(sizeof(*p));

  p->num_addresses = num_addrs;
  p->subchannels = gpr_zalloc(sizeof(*p->subchannels) * num_addrs);

  grpc_subchannel_args sc_args;
  size_t subchannel_idx = 0;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    /* Skip balancer addresses, since we only know how to handle backends. */
    if (addresses->addresses[i].is_balancer) continue;

    memset(&sc_args, 0, sizeof(grpc_subchannel_args));
    grpc_arg addr_arg =
        grpc_create_subchannel_address_arg(&addresses->addresses[i].address);
    grpc_channel_args *new_args =
        grpc_channel_args_copy_and_add(args->args, &addr_arg, 1);
    gpr_free(addr_arg.value.string);
    sc_args.args = new_args;
    grpc_subchannel *subchannel = grpc_client_channel_factory_create_subchannel(
        exec_ctx, args->client_channel_factory, &sc_args);
    if (grpc_lb_round_robin_trace) {
      char *address_uri =
          grpc_sockaddr_to_uri(&addresses->addresses[i].address);
      gpr_log(GPR_DEBUG, "Created subchannel %p for address uri %s",
              (void *)subchannel, address_uri);
      gpr_free(address_uri);
    }
    grpc_channel_args_destroy(exec_ctx, new_args);

    if (subchannel != NULL) {
      subchannel_data *sd = gpr_zalloc(sizeof(*sd));
      p->subchannels[subchannel_idx] = sd;
      sd->policy = p;
      sd->index = subchannel_idx;
      sd->subchannel = subchannel;
      sd->user_data_vtable = addresses->user_data_vtable;
      if (sd->user_data_vtable != NULL) {
        sd->user_data =
            sd->user_data_vtable->copy(addresses->addresses[i].user_data);
      }
      ++subchannel_idx;
      grpc_closure_init(&sd->connectivity_changed_closure,
                        rr_connectivity_changed_locked, sd,
                        grpc_combiner_scheduler(args->combiner, false));
    }
  }
  if (subchannel_idx == 0) {
    /* couldn't create any subchannel. Bail out */
    gpr_free(p->subchannels);
    gpr_free(p);
    return NULL;
  }
  p->num_subchannels = subchannel_idx;

  /* The (dummy node) root of the ready list */
  p->ready_list.subchannel = NULL;
  p->ready_list.prev = NULL;
  p->ready_list.next = NULL;
  p->ready_list_last_pick = &p->ready_list;

  grpc_lb_policy_init(&p->base, &round_robin_lb_policy_vtable, args->combiner);
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE,
                               "round_robin");

  if (grpc_lb_round_robin_trace) {
    gpr_log(GPR_DEBUG, "Created RR policy at %p with %lu subchannels",
            (void *)p, (unsigned long)p->num_subchannels);
  }
  return &p->base;
}

static const grpc_lb_policy_factory_vtable round_robin_factory_vtable = {
    round_robin_factory_ref, round_robin_factory_unref, round_robin_create,
    "round_robin"};

static grpc_lb_policy_factory round_robin_lb_policy_factory = {
    &round_robin_factory_vtable};

static grpc_lb_policy_factory *round_robin_lb_factory_create() {
  return &round_robin_lb_policy_factory;
}

/* Plugin registration */

void grpc_lb_policy_round_robin_init() {
  grpc_register_lb_policy(round_robin_lb_factory_create());
  grpc_register_tracer("round_robin", &grpc_lb_round_robin_trace);
}

void grpc_lb_policy_round_robin_shutdown() {}
