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
 * Before every pick, the \a get_next_ready_subchannel_index_locked function
 * returns the p->subchannels index for next subchannel, respecting the relative
 * order of the addresses provided upon creation or updates. Note however that
 * updates will start picking from the beginning of the updated list. */

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/static_metadata.h"

typedef struct round_robin_lb_policy round_robin_lb_policy;

grpc_tracer_flag grpc_lb_round_robin_trace = GRPC_TRACER_INITIALIZER(false);

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

typedef struct {
  /** backpointer to owning policy */
  round_robin_lb_policy *policy;
  /** subchannel itself */
  grpc_subchannel *subchannel;
  /** notification that connectivity has changed on subchannel */
  grpc_closure connectivity_changed_closure;
  /** last observed connectivity. Not updated by
   * \a grpc_subchannel_notify_on_state_change. Used to determine the previous
   * state while processing the new state in \a rr_connectivity_changed */
  grpc_connectivity_state prev_connectivity_state;
  /** current connectivity state. Updated by \a
   * grpc_subchannel_notify_on_state_change */
  grpc_connectivity_state curr_connectivity_state;
  /** connectivity state to be updated by the watcher, not guarded by
   * the combiner.  Will be moved to curr_connectivity_state inside of
   * the combiner by rr_connectivity_changed_locked(). */
  grpc_connectivity_state pending_connectivity_state_unsafe;
  /** the subchannel's target user data */
  void *user_data;
  /** vtable to operate over \a user_data */
  const grpc_lb_user_data_vtable *user_data_vtable;
  /** we refcount in order to know when to start shutting down a subchannel and
   * its associted data (this struct). */
  gpr_refcount refcount;
  /** true if the subchannel isn't used by the policy anymore. */
  bool shutting_down;
} subchannel_data;

struct round_robin_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  /** total number of addresses received at creation time */
  size_t num_addresses;

  /** all our subchannels */
  size_t num_subchannels;
  subchannel_data **subchannels;

  /** how many subchannels are in state READY */
  size_t num_ready;
  /** how many subchannels are in state TRANSIENT_FAILURE */
  size_t num_transient_failures;
  /** how many subchannels are in state IDLE */
  size_t num_idle;

  /** have we started picking? */
  bool started_picking;
  /** are we shutting down? */
  bool shutdown;
  /** List of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  /** Index into subchannels for last pick. */
  size_t last_ready_subchannel_index;
};

/** Returns the index into p->subchannels of the next subchannel in
 * READY state, or p->num_subchannels if no subchannel is READY.
 *
 * Note that this function does *not* update p->last_ready_subchannel_index.
 * The caller must do that if it returns a pick. */
static size_t get_next_ready_subchannel_index_locked(
    const round_robin_lb_policy *p) {
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR: %p] getting next ready subchannel (out of %lu), "
            "last_ready_subchannel_index=%lu",
            (void *)p, (unsigned long)p->num_subchannels,
            (unsigned long)p->last_ready_subchannel_index);
  }
  for (size_t i = 0; i < p->num_subchannels; ++i) {
    const size_t index =
        (i + p->last_ready_subchannel_index + 1) % p->num_subchannels;
    if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_DEBUG, "[RR %p] checking subchannel %p, index %lu: state=%d",
              (void *)p, (void *)p->subchannels[index]->subchannel,
              (unsigned long)index,
              p->subchannels[index]->curr_connectivity_state);
    }
    if (p->subchannels[index]->curr_connectivity_state == GRPC_CHANNEL_READY) {
      if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
        gpr_log(GPR_DEBUG, "[RR %p] found next ready subchannel at index %lu",
                (void *)p, (unsigned long)index);
      }
      return index;
    }
  }
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "[RR %p] no subchannels in ready state", (void *)p);
  }
  return p->num_subchannels;
}

// Sets p->last_ready_subchannel_index to last_ready_index.
static void update_last_ready_subchannel_index_locked(round_robin_lb_policy *p,
                                                      size_t last_ready_index) {
  GPR_ASSERT(last_ready_index < p->num_subchannels);
  p->last_ready_subchannel_index = last_ready_index;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG,
            "[RR: %p] setting last_ready_subchannel_index=%lu (SC %p, CSC %p)",
            (void *)p, (unsigned long)last_ready_index,
            (void *)p->subchannels[last_ready_index]->subchannel,
            (void *)grpc_subchannel_get_connected_subchannel(
                p->subchannels[last_ready_index]->subchannel));
  }
}

static void rr_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "Destroying Round Robin policy at %p", (void *)pol);
  }
  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  gpr_free(p->subchannels);
  gpr_free(p);
}

static void rr_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "Shutting down Round Robin policy at %p", (void *)pol);
  }
  p->shutdown = true;
  pending_pick *pp;
  while ((pp = p->pending_picks)) {
    p->pending_picks = pp->next;
    *pp->target = NULL;
    grpc_closure_sched(
        exec_ctx, pp->on_complete,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel Shutdown"));
    gpr_free(pp);
  }
  grpc_connectivity_state_set(
      exec_ctx, &p->state_tracker, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel Shutdown"), "rr_shutdown");
  for (size_t i = 0; i < p->num_subchannels; i++) {
    subchannel_data *sd = p->subchannels[i];
    if (sd->subchannel != NULL) {
      // unsubscribe
      sd->shutting_down = true;
      grpc_subchannel_notify_on_state_change(exec_ctx, sd->subchannel, NULL,
                                             NULL,
                                             &sd->connectivity_changed_closure);
    }
  }
}

static void rr_cancel_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                                  grpc_connected_subchannel **target,
                                  grpc_error *error) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      *target = NULL;
      grpc_closure_sched(exec_ctx, pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick cancelled", &error, 1));
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
  pending_pick *pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if ((pp->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      *pp->target = NULL;
      grpc_closure_sched(exec_ctx, pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick cancelled", &error, 1));
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
  p->started_picking = true;
  for (size_t i = 0; i < p->num_subchannels; i++) {
    subchannel_data *sd = p->subchannels[i];
    if (sd->subchannel != NULL) {
      GRPC_LB_POLICY_WEAK_REF(&p->base, "rr_connectivity");
      grpc_subchannel_notify_on_state_change(
          exec_ctx, sd->subchannel, p->base.interested_parties,
          &sd->pending_connectivity_state_unsafe,
          &sd->connectivity_changed_closure);
    }
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
                          grpc_connected_subchannel **target,
                          grpc_call_context_element *context, void **user_data,
                          grpc_closure *on_complete) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "Round Robin %p trying to pick", (void *)pol);
  }
  const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
  if (next_ready_index < p->num_subchannels) {
    /* readily available, report right away */
    subchannel_data *sd = p->subchannels[next_ready_index];
    *target = GRPC_CONNECTED_SUBCHANNEL_REF(
        grpc_subchannel_get_connected_subchannel(sd->subchannel), "rr_picked");
    if (user_data != NULL) {
      *user_data = sd->user_data;
    }
    if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_DEBUG,
              "[RR PICK] TARGET <-- CONNECTED SUBCHANNEL %p (INDEX %lu)",
              (void *)*target, (unsigned long)next_ready_index);
    }
    /* only advance the last picked pointer if the selection was used */
    update_last_ready_subchannel_index_locked(p, next_ready_index);
    return 1;
  } else {
    /* no pick currently available. Save for later in list of pending picks */
    if (!p->started_picking) {
      start_picking_locked(exec_ctx, p);
    }
    pending_pick *pp = gpr_malloc(sizeof(*pp));
    pp->next = p->pending_picks;
    pp->target = target;
    pp->on_complete = on_complete;
    pp->initial_metadata_flags = pick_args->initial_metadata_flags;
    pp->user_data = user_data;
    p->pending_picks = pp;
    return 0;
  }
}

static void update_state_counters_locked(subchannel_data *sd) {
  round_robin_lb_policy *p = sd->policy;
  if (sd->prev_connectivity_state == GRPC_CHANNEL_READY) {
    GPR_ASSERT(p->num_ready > 0);
    --p->num_ready;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    GPR_ASSERT(p->num_transient_failures > 0);
    --p->num_transient_failures;
  } else if (sd->prev_connectivity_state == GRPC_CHANNEL_IDLE) {
    GPR_ASSERT(p->num_idle > 0);
    --p->num_idle;
  }
  if (sd->curr_connectivity_state == GRPC_CHANNEL_READY) {
    ++p->num_ready;
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++p->num_transient_failures;
  } else if (sd->curr_connectivity_state == GRPC_CHANNEL_IDLE) {
    ++p->num_idle;
  }
}

/* sd is the subchannel_data associted with the updated subchannel.
 * shutdown_error will only be used upon policy transition to TRANSIENT_FAILURE
 * or SHUTDOWN */
static grpc_connectivity_state update_lb_connectivity_status_locked(
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
  if (p->num_ready > 0) { /* 1) READY */
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
  if (sd->shutting_down) {
    if (sd->subchannel != NULL) {
      // Note that sd->subchannel could be NULL if the subchannel had shutdown
      // between the setting of sd->shutting_down and the invocation of this
      // callback. In that case, sd->user_data is already destroyed.
      GRPC_SUBCHANNEL_UNREF(exec_ctx, sd->subchannel, "rr_sd_shutdown");
      if (sd->user_data != NULL) {
        GPR_ASSERT(sd->user_data_vtable != NULL);
        sd->user_data_vtable->destroy(exec_ctx, sd->user_data);
      }
    }
    // If sd->shutting_down is true, the subchannel has already
    // been unsubscribed from connectivity updates: it's safe to free "sd".
    gpr_free(sd);
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "rr_connectivity");
    return;
  }
  // Now that we're inside the combiner, copy the pending connectivity
  // state (which was set by the connectivity state watcher) to
  // curr_connectivity_state, which is what we use inside of the combiner.
  sd->curr_connectivity_state = sd->pending_connectivity_state_unsafe;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG,
            "[RR %p] connectivity changed for subchannel %p: "
            "prev_state=%d new_state=%d",
            (void *)p, (void *)sd->subchannel, sd->prev_connectivity_state,
            sd->curr_connectivity_state);
  }
  // If we're shutting down, unref and return.
  if (p->shutdown) {
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "rr_connectivity");
    return;
  }
  // Update state counters and determine new overall state.
  update_state_counters_locked(sd);
  sd->prev_connectivity_state = sd->curr_connectivity_state;
  grpc_connectivity_state new_connectivity_state =
      update_lb_connectivity_status_locked(exec_ctx, sd, GRPC_ERROR_REF(error));
  // If the new state is SHUTDOWN, unref the subchannel, and if the new
  // overall state is SHUTDOWN, clean up.
  if (sd->curr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
    GRPC_SUBCHANNEL_UNREF(exec_ctx, sd->subchannel, "rr_subchannel_shutdown");
    sd->subchannel = NULL;
    if (sd->user_data != NULL) {
      GPR_ASSERT(sd->user_data_vtable != NULL);
      sd->user_data_vtable->destroy(exec_ctx, sd->user_data);
    }
    if (new_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
      /* the policy is shutting down. Flush all the pending picks... */
      pending_pick *pp;
      while ((pp = p->pending_picks)) {
        p->pending_picks = pp->next;
        *pp->target = NULL;
        grpc_closure_sched(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
        gpr_free(pp);
      }
    }
    /* unref the "rr_connectivity" weak ref from start_picking */
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "rr_connectivity");
  } else {
    if (sd->curr_connectivity_state == GRPC_CHANNEL_READY) {
      /* at this point we know there's at least one suitable subchannel. Go
       * ahead and pick one and notify the pending suitors in
       * p->pending_picks. This preemtively replicates rr_pick()'s actions. */
      const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
      GPR_ASSERT(next_ready_index < p->num_subchannels);
      subchannel_data *selected = p->subchannels[next_ready_index];
      if (p->pending_picks != NULL) {
        /* if the selected subchannel is going to be used for the pending
         * picks, update the last picked pointer */
        update_last_ready_subchannel_index_locked(p, next_ready_index);
      }
      pending_pick *pp;
      while ((pp = p->pending_picks)) {
        p->pending_picks = pp->next;
        *pp->target = GRPC_CONNECTED_SUBCHANNEL_REF(
            grpc_subchannel_get_connected_subchannel(selected->subchannel),
            "rr_picked");
        if (pp->user_data != NULL) {
          *pp->user_data = selected->user_data;
        }
        if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
          gpr_log(GPR_DEBUG,
                  "[RR CONN CHANGED] TARGET <-- SUBCHANNEL %p (INDEX %lu)",
                  (void *)selected->subchannel,
                  (unsigned long)next_ready_index);
        }
        grpc_closure_sched(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
        gpr_free(pp);
      }
    }
    /* renew notification: reuses the "rr_connectivity" weak ref */
    grpc_subchannel_notify_on_state_change(
        exec_ctx, sd->subchannel, p->base.interested_parties,
        &sd->pending_connectivity_state_unsafe,
        &sd->connectivity_changed_closure);
  }
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
  const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
  if (next_ready_index < p->num_subchannels) {
    subchannel_data *selected = p->subchannels[next_ready_index];
    grpc_connected_subchannel *target = GRPC_CONNECTED_SUBCHANNEL_REF(
        grpc_subchannel_get_connected_subchannel(selected->subchannel),
        "rr_picked");
    grpc_connected_subchannel_ping(exec_ctx, target, closure);
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, target, "rr_picked");
  } else {
    grpc_closure_sched(exec_ctx, closure, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                              "Round Robin not connected"));
  }
}

static bool rr_update_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                             const grpc_lb_policy_args *args) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)policy;
  /* Find the number of backend addresses. We ignore balancer addresses, since
   * we don't know how to handle them. */
  const grpc_arg *arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == NULL || arg->type != GRPC_ARG_POINTER) {
    return NULL;
  }
  grpc_lb_addresses *addresses = arg->value.pointer.p;
  size_t num_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    if (!addresses->addresses[i].is_balancer) ++num_addrs;
  }

  subchannel_data **update_subchannels = NULL;
  size_t subchannel_index = 0;
  if (num_addrs > 0) {
    update_subchannels = gpr_zalloc(sizeof(*update_subchannels) * num_addrs);
  } else {
    // an empty update skips to the unreffing of the current subchannels, if any
    goto unref;
  }

  grpc_subchannel_args sc_args;
  /* We need to remove the LB addresses in order to be able to compare the
   * subchannel keys of subchannels from a different batch of addresses. */
  static const char *keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS,
                                         GRPC_ARG_LB_ADDRESSES};
  /* 1. Create subchannels for addresses in the update. */
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    /* Skip balancer addresses, since we only know how to handle backends. */
    if (addresses->addresses[i].is_balancer) continue;
    GPR_ASSERT(i < num_addrs);
    memset(&sc_args, 0, sizeof(grpc_subchannel_args));
    grpc_arg addr_arg =
        grpc_create_subchannel_address_arg(&addresses->addresses[i].address);
    grpc_channel_args *new_args = grpc_channel_args_copy_and_add_and_remove(
        args->args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), &addr_arg,
        1);
    gpr_free(addr_arg.value.string);
    sc_args.args = new_args;
    grpc_subchannel *subchannel = grpc_client_channel_factory_create_subchannel(
        exec_ctx, args->client_channel_factory, &sc_args);
    if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
      char *address_uri =
          grpc_sockaddr_to_uri(&addresses->addresses[i].address);
      gpr_log(GPR_DEBUG, "index %lu: Created subchannel %p for address uri %s",
              (unsigned long)subchannel_index, (void *)subchannel, address_uri);
      gpr_free(address_uri);
    }
    grpc_channel_args_destroy(exec_ctx, new_args);

    if (subchannel != NULL) {
      // Look for subchannel in current p->subchannels to increase its refcount.
      subchannel_data *curr_sd = NULL;
      for (size_t j = 0; j < p->num_subchannels; ++j) {
        if (p->subchannels[j]->subchannel == subchannel) {
          curr_sd = p->subchannels[j];
          gpr_ref(&curr_sd->refcount);
          break;  // there should be only one.
        }
      }
      if (curr_sd != NULL) {
        update_subchannels[subchannel_index] = curr_sd;
        if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
          char *address_uri =
              grpc_sockaddr_to_uri(&addresses->addresses[i].address);
          gpr_log(GPR_DEBUG, "Reusing subchannel %p for address uri %s",
                  (void *)subchannel, address_uri);
          gpr_free(address_uri);
        }
        // Discount the subchannel ref from the unnecessary creation in step 1
        // of the subchannel being resused.
        GRPC_SUBCHANNEL_UNREF(exec_ctx, subchannel, "rr_update_sc_known");
      } else {
        subchannel_data *sd = gpr_zalloc(sizeof(*sd));
        gpr_ref_init(&sd->refcount, 1);
        sd->policy = p;
        sd->subchannel = subchannel;
        /* use some sentinel value outside of the range of
         * grpc_connectivity_state to signal an undefined previous state. We
         * won't be referring to this value again and it'll be overwritten after
         * the first call to rr_connectivity_changed_locked */
        sd->prev_connectivity_state = GRPC_CHANNEL_INIT;
        sd->curr_connectivity_state = GRPC_CHANNEL_IDLE;
        sd->user_data_vtable = addresses->user_data_vtable;
        if (sd->user_data_vtable != NULL) {
          sd->user_data =
              sd->user_data_vtable->copy(addresses->addresses[i].user_data);
        }
        grpc_closure_init(&sd->connectivity_changed_closure,
                          rr_connectivity_changed_locked, sd,
                          grpc_combiner_scheduler(args->combiner, false));
        GRPC_LB_POLICY_WEAK_REF(&p->base, "rr_connectivity_update");
        if (p->started_picking) {
          grpc_subchannel_notify_on_state_change(
              exec_ctx, sd->subchannel, p->base.interested_parties,
              &sd->pending_connectivity_state_unsafe,
              &sd->connectivity_changed_closure);
        }
        update_subchannels[subchannel_index] = sd;
      }
      ++subchannel_index;
    }
  }

unref:
  /* 2. Unref all subchannels in p->subchannels. Subchannels present in both the
   * update and p->subchannels will net +1 references. Those missing in the
   * update will net -1. */
  for (size_t i = 0; i < p->num_subchannels; ++i) {
    subchannel_data *sd = p->subchannels[i];
    if (sd->subchannel != NULL) {
      if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
        gpr_log(GPR_DEBUG, "Unreffing already present subchannel %p",
                (void *)sd->subchannel);
      }
      if (gpr_unref(&sd->refcount)) {
        // Once a subchannel isn't referenced, unsubscribe from its connectivity
        // status and mark it as shutting down. The \a sd->shutting_down bit
        // will be consumed in the sd->connectivity_changed_closure callback.
        if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
          gpr_log(GPR_DEBUG, "Subchannel %p shutting down!",
                  (void *)sd->subchannel);
        }
        sd->shutting_down = true;
        // unsubscribe
        grpc_subchannel_notify_on_state_change(
            exec_ctx, sd->subchannel, NULL, NULL,
            &sd->connectivity_changed_closure);
      }
    }
  }

  if (subchannel_index > 0) {
    if (p->subchannels != NULL) gpr_free(p->subchannels);
    p->subchannels = update_subchannels;
    p->num_subchannels = subchannel_index;
    // Initialize the last pick index to the last subchannel, so that the first
    // pick will start at the beginning of the list.
    p->last_ready_subchannel_index = subchannel_index - 1;
  } else {
    gpr_free(update_subchannels);
    return false;
  }
  return true;
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
    rr_update_locked};

static void round_robin_factory_ref(grpc_lb_policy_factory *factory) {}

static void round_robin_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *round_robin_create(grpc_exec_ctx *exec_ctx,
                                          grpc_lb_policy_factory *factory,
                                          grpc_lb_policy_args *args) {
  GPR_ASSERT(args->client_channel_factory != NULL);
  round_robin_lb_policy *p = gpr_zalloc(sizeof(*p));
  if (!rr_update_locked(exec_ctx, &p->base, args)) {
    for (size_t i = 0; i < p->num_subchannels; ++i) gpr_free(p->subchannels[i]);
    gpr_free(p->subchannels);
    gpr_free(p);
    return NULL;
  }
  grpc_lb_policy_init(&p->base, &round_robin_lb_policy_vtable, args->combiner);
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE,
                               "round_robin");
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "Created Round Robin %p with %lu subchannels", (void *)p,
            (unsigned long)p->num_subchannels);
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
