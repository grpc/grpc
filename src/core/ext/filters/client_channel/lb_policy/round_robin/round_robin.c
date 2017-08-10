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
 * respecting the relative
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

grpc_tracer_flag grpc_lb_round_robin_trace =
    GRPC_TRACER_INITIALIZER(false, "round_robin");

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

typedef struct rr_subchannel_list rr_subchannel_list;
typedef struct round_robin_lb_policy {
  /** base policy: must be first */
  grpc_lb_policy base;

  rr_subchannel_list *subchannel_list;

  /** have we started picking? */
  bool started_picking;
  /** are we shutting down? */
  bool shutdown;
  /** has the policy gotten into the GRPC_CHANNEL_SHUTDOWN? No picks can be
   * service after this point, the policy will never transition out. */
  bool in_connectivity_shutdown;
  /** List of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  /** Index into subchannels for last pick. */
  size_t last_ready_subchannel_index;

  /** Latest version of the subchannel list.
   * Subchannel connectivity callbacks will only promote updated subchannel
   * lists if they equal \a latest_pending_subchannel_list. In other words,
   * racing callbacks that reference outdated subchannel lists won't perform any
   * update. */
  rr_subchannel_list *latest_pending_subchannel_list;
} round_robin_lb_policy;

typedef struct {
  /** backpointer to owning subchannel list */
  rr_subchannel_list *subchannel_list;
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
} subchannel_data;

struct rr_subchannel_list {
  /** backpointer to owning policy */
  round_robin_lb_policy *policy;

  /** all our subchannels */
  size_t num_subchannels;
  subchannel_data *subchannels;

  /** how many subchannels are in state READY */
  size_t num_ready;
  /** how many subchannels are in state TRANSIENT_FAILURE */
  size_t num_transient_failures;
  /** how many subchannels are in state SHUTDOWN */
  size_t num_shutdown;
  /** how many subchannels are in state IDLE */
  size_t num_idle;

  /** There will be one ref for each entry in subchannels for which there is a
   * pending connectivity state watcher callback. */
  gpr_refcount refcount;

  /** Is this list shutting down? This may be true due to the shutdown of the
   * policy itself or because a newer update has arrived while this one hadn't
   * finished processing. */
  bool shutting_down;
};

static rr_subchannel_list *rr_subchannel_list_create(round_robin_lb_policy *p,
                                                     size_t num_subchannels) {
  rr_subchannel_list *subchannel_list = gpr_zalloc(sizeof(*subchannel_list));
  subchannel_list->policy = p;
  subchannel_list->subchannels =
      gpr_zalloc(sizeof(subchannel_data) * num_subchannels);
  subchannel_list->num_subchannels = num_subchannels;
  gpr_ref_init(&subchannel_list->refcount, 1);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Created subchannel list %p for %lu subchannels",
            (void *)p, (void *)subchannel_list, (unsigned long)num_subchannels);
  }
  return subchannel_list;
}

static void rr_subchannel_list_destroy(grpc_exec_ctx *exec_ctx,
                                       rr_subchannel_list *subchannel_list) {
  GPR_ASSERT(subchannel_list->shutting_down);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Destroying subchannel_list %p",
            (void *)subchannel_list->policy, (void *)subchannel_list);
  }
  for (size_t i = 0; i < subchannel_list->num_subchannels; i++) {
    subchannel_data *sd = &subchannel_list->subchannels[i];
    if (sd->subchannel != NULL) {
      GRPC_SUBCHANNEL_UNREF(exec_ctx, sd->subchannel,
                            "rr_subchannel_list_destroy");
    }
    sd->subchannel = NULL;
    if (sd->user_data != NULL) {
      GPR_ASSERT(sd->user_data_vtable != NULL);
      sd->user_data_vtable->destroy(exec_ctx, sd->user_data);
      sd->user_data = NULL;
    }
  }
  gpr_free(subchannel_list->subchannels);
  gpr_free(subchannel_list);
}

static void rr_subchannel_list_ref(rr_subchannel_list *subchannel_list,
                                   const char *reason) {
  gpr_ref_non_zero(&subchannel_list->refcount);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    const gpr_atm count = gpr_atm_acq_load(&subchannel_list->refcount.count);
    gpr_log(GPR_INFO, "[RR %p] subchannel_list %p REF %lu->%lu (%s)",
            (void *)subchannel_list->policy, (void *)subchannel_list,
            (unsigned long)(count - 1), (unsigned long)count, reason);
  }
}

static void rr_subchannel_list_unref(grpc_exec_ctx *exec_ctx,
                                     rr_subchannel_list *subchannel_list,
                                     const char *reason) {
  const bool done = gpr_unref(&subchannel_list->refcount);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    const gpr_atm count = gpr_atm_acq_load(&subchannel_list->refcount.count);
    gpr_log(GPR_INFO, "[RR %p] subchannel_list %p UNREF %lu->%lu (%s)",
            (void *)subchannel_list->policy, (void *)subchannel_list,
            (unsigned long)(count + 1), (unsigned long)count, reason);
  }
  if (done) {
    rr_subchannel_list_destroy(exec_ctx, subchannel_list);
  }
}

/** Mark \a subchannel_list as discarded. Unsubscribes all its subchannels. The
 * watcher's callback will ultimately unref \a subchannel_list.  */
static void rr_subchannel_list_shutdown_and_unref(
    grpc_exec_ctx *exec_ctx, rr_subchannel_list *subchannel_list,
    const char *reason) {
  GPR_ASSERT(!subchannel_list->shutting_down);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "[RR %p] Shutting down subchannel_list %p (%s)",
            (void *)subchannel_list->policy, (void *)subchannel_list, reason);
  }
  GPR_ASSERT(!subchannel_list->shutting_down);
  subchannel_list->shutting_down = true;
  for (size_t i = 0; i < subchannel_list->num_subchannels; i++) {
    subchannel_data *sd = &subchannel_list->subchannels[i];
    if (sd->subchannel != NULL) {  // if subchannel isn't shutdown, unsubscribe.
      if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
        gpr_log(
            GPR_DEBUG,
            "[RR %p] Unsubscribing from subchannel %p as part of shutting down "
            "subchannel_list %p",
            (void *)subchannel_list->policy, (void *)sd->subchannel,
            (void *)subchannel_list);
      }
      grpc_subchannel_notify_on_state_change(exec_ctx, sd->subchannel, NULL,
                                             NULL,
                                             &sd->connectivity_changed_closure);
    }
  }
  rr_subchannel_list_unref(exec_ctx, subchannel_list, reason);
}

/** Returns the index into p->subchannel_list->subchannels of the next
 * subchannel in READY state, or p->subchannel_list->num_subchannels if no
 * subchannel is READY.
 *
 * Note that this function does *not* update p->last_ready_subchannel_index.
 * The caller must do that if it returns a pick. */
static size_t get_next_ready_subchannel_index_locked(
    const round_robin_lb_policy *p) {
  GPR_ASSERT(p->subchannel_list != NULL);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p] getting next ready subchannel (out of %lu), "
            "last_ready_subchannel_index=%lu",
            (void *)p, (unsigned long)p->subchannel_list->num_subchannels,
            (unsigned long)p->last_ready_subchannel_index);
  }
  for (size_t i = 0; i < p->subchannel_list->num_subchannels; ++i) {
    const size_t index = (i + p->last_ready_subchannel_index + 1) %
                         p->subchannel_list->num_subchannels;
    if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
      gpr_log(
          GPR_DEBUG,
          "[RR %p] checking subchannel %p, subchannel_list %p, index %lu: "
          "state=%s",
          (void *)p, (void *)p->subchannel_list->subchannels[index].subchannel,
          (void *)p->subchannel_list, (unsigned long)index,
          grpc_connectivity_state_name(
              p->subchannel_list->subchannels[index].curr_connectivity_state));
    }
    if (p->subchannel_list->subchannels[index].curr_connectivity_state ==
        GRPC_CHANNEL_READY) {
      if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
        gpr_log(GPR_DEBUG,
                "[RR %p] found next ready subchannel (%p) at index %lu of "
                "subchannel_list %p",
                (void *)p,
                (void *)p->subchannel_list->subchannels[index].subchannel,
                (unsigned long)index, (void *)p->subchannel_list);
      }
      return index;
    }
  }
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "[RR %p] no subchannels in ready state", (void *)p);
  }
  return p->subchannel_list->num_subchannels;
}

// Sets p->last_ready_subchannel_index to last_ready_index.
static void update_last_ready_subchannel_index_locked(round_robin_lb_policy *p,
                                                      size_t last_ready_index) {
  GPR_ASSERT(last_ready_index < p->subchannel_list->num_subchannels);
  p->last_ready_subchannel_index = last_ready_index;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(
        GPR_DEBUG,
        "[RR %p] setting last_ready_subchannel_index=%lu (SC %p, CSC %p)",
        (void *)p, (unsigned long)last_ready_index,
        (void *)p->subchannel_list->subchannels[last_ready_index].subchannel,
        (void *)grpc_subchannel_get_connected_subchannel(
            p->subchannel_list->subchannels[last_ready_index].subchannel));
  }
}

static void rr_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "[RR %p] Destroying Round Robin policy at %p",
            (void *)pol, (void *)pol);
  }
  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  gpr_free(p);
}

static void rr_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "[RR %p] Shutting down Round Robin policy at %p",
            (void *)pol, (void *)pol);
  }
  p->shutdown = true;
  pending_pick *pp;
  while ((pp = p->pending_picks)) {
    p->pending_picks = pp->next;
    *pp->target = NULL;
    GRPC_CLOSURE_SCHED(
        exec_ctx, pp->on_complete,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel Shutdown"));
    gpr_free(pp);
  }
  grpc_connectivity_state_set(
      exec_ctx, &p->state_tracker, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel Shutdown"), "rr_shutdown");
  const bool latest_is_current =
      p->subchannel_list == p->latest_pending_subchannel_list;
  rr_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                        "sl_shutdown_rr_shutdown");
  p->subchannel_list = NULL;
  if (!latest_is_current && p->latest_pending_subchannel_list != NULL &&
      !p->latest_pending_subchannel_list->shutting_down) {
    rr_subchannel_list_shutdown_and_unref(exec_ctx,
                                          p->latest_pending_subchannel_list,
                                          "sl_shutdown_pending_rr_shutdown");
    p->latest_pending_subchannel_list = NULL;
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
      GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete,
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
      GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete,
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
  for (size_t i = 0; i < p->subchannel_list->num_subchannels; i++) {
    subchannel_data *sd = &p->subchannel_list->subchannels[i];
    GRPC_LB_POLICY_WEAK_REF(&p->base, "start_picking_locked");
    rr_subchannel_list_ref(sd->subchannel_list, "started_picking");
    grpc_subchannel_notify_on_state_change(
        exec_ctx, sd->subchannel, p->base.interested_parties,
        &sd->pending_connectivity_state_unsafe,
        &sd->connectivity_changed_closure);
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
  GPR_ASSERT(!p->shutdown);
  GPR_ASSERT(!p->in_connectivity_shutdown);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Trying to pick", (void *)pol);
  }
  if (p->subchannel_list != NULL) {
    const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
    if (next_ready_index < p->subchannel_list->num_subchannels) {
      /* readily available, report right away */
      subchannel_data *sd = &p->subchannel_list->subchannels[next_ready_index];
      *target = GRPC_CONNECTED_SUBCHANNEL_REF(
          grpc_subchannel_get_connected_subchannel(sd->subchannel),
          "rr_picked");
      if (user_data != NULL) {
        *user_data = sd->user_data;
      }
      if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
        gpr_log(
            GPR_DEBUG,
            "[RR %p] Picked target <-- Subchannel %p (connected %p) (sl %p, "
            "index %lu)",
            (void *)p, (void *)sd->subchannel, (void *)*target,
            (void *)sd->subchannel_list, (unsigned long)next_ready_index);
      }
      /* only advance the last picked pointer if the selection was used */
      update_last_ready_subchannel_index_locked(p, next_ready_index);
      return 1;
    }
  }
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

static void update_state_counters_locked(subchannel_data *sd) {
  rr_subchannel_list *subchannel_list = sd->subchannel_list;
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
 * (the subchannel_data associted with the updated subchannel) and the
 * subchannel list \a sd belongs to (sd->subchannel_list). \a error will only be
 * used upon policy transition to TRANSIENT_FAILURE or SHUTDOWN. Returns the
 * connectivity status set. */
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
   *    CHECK: p->subchannel_list->num_shutdown ==
   *           p->subchannel_list->num_subchannels.
   *
   * 4) RULE: ALL subchannels are TRANSIENT_FAILURE => policy is
   *    TRANSIENT_FAILURE.
   *    CHECK: p->num_transient_failures == p->subchannel_list->num_subchannels.
   *
   * 5) RULE: ALL subchannels are IDLE => policy is IDLE.
   *    CHECK: p->num_idle == p->subchannel_list->num_subchannels.
   */
  grpc_connectivity_state new_state = sd->curr_connectivity_state;
  rr_subchannel_list *subchannel_list = sd->subchannel_list;
  round_robin_lb_policy *p = subchannel_list->policy;
  if (subchannel_list->num_ready > 0) { /* 1) READY */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker, GRPC_CHANNEL_READY,
                                GRPC_ERROR_NONE, "rr_ready");
    new_state = GRPC_CHANNEL_READY;
  } else if (sd->curr_connectivity_state ==
             GRPC_CHANNEL_CONNECTING) { /* 2) CONNECTING */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE,
                                "rr_connecting");
    new_state = GRPC_CHANNEL_CONNECTING;
  } else if (p->subchannel_list->num_shutdown ==
             p->subchannel_list->num_subchannels) { /* 3) SHUTDOWN */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                GRPC_CHANNEL_SHUTDOWN, GRPC_ERROR_REF(error),
                                "rr_shutdown");
    p->in_connectivity_shutdown = true;
    new_state = GRPC_CHANNEL_SHUTDOWN;
  } else if (subchannel_list->num_transient_failures ==
             p->subchannel_list->num_subchannels) { /* 4) TRANSIENT_FAILURE */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                GRPC_CHANNEL_TRANSIENT_FAILURE,
                                GRPC_ERROR_REF(error), "rr_transient_failure");
    new_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  } else if (subchannel_list->num_idle ==
             p->subchannel_list->num_subchannels) { /* 5) IDLE */
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker, GRPC_CHANNEL_IDLE,
                                GRPC_ERROR_NONE, "rr_idle");
    new_state = GRPC_CHANNEL_IDLE;
  }
  GRPC_ERROR_UNREF(error);
  return new_state;
}

static void rr_connectivity_changed_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  subchannel_data *sd = arg;
  round_robin_lb_policy *p = sd->subchannel_list->policy;
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(
        GPR_DEBUG,
        "[RR %p] connectivity changed for subchannel %p, subchannel_list %p: "
        "prev_state=%s new_state=%s p->shutdown=%d "
        "sd->subchannel_list->shutting_down=%d error=%s",
        (void *)p, (void *)sd->subchannel, (void *)sd->subchannel_list,
        grpc_connectivity_state_name(sd->prev_connectivity_state),
        grpc_connectivity_state_name(sd->pending_connectivity_state_unsafe),
        p->shutdown, sd->subchannel_list->shutting_down,
        grpc_error_string(error));
  }
  // If the policy is shutting down, unref and return.
  if (p->shutdown) {
    rr_subchannel_list_unref(exec_ctx, sd->subchannel_list,
                             "pol_shutdown+started_picking");
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "pol_shutdown");
    return;
  }
  if (sd->subchannel_list->shutting_down && error == GRPC_ERROR_CANCELLED) {
    // the subchannel list associated with sd has been discarded. This callback
    // corresponds to the unsubscription. The unrefs correspond to the picking
    // ref (start_picking_locked or update_started_picking).
    rr_subchannel_list_unref(exec_ctx, sd->subchannel_list,
                             "sl_shutdown+started_picking");
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "sl_shutdown+picking");
    return;
  }
  // Dispose of outdated subchannel lists.
  if (sd->subchannel_list != p->subchannel_list &&
      sd->subchannel_list != p->latest_pending_subchannel_list) {
    char *reason = NULL;
    if (sd->subchannel_list->shutting_down) {
      reason = "sl_outdated_straggler";
      rr_subchannel_list_unref(exec_ctx, sd->subchannel_list, reason);
    } else {
      reason = "sl_outdated";
      rr_subchannel_list_shutdown_and_unref(exec_ctx, sd->subchannel_list,
                                            reason);
    }
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, reason);
    return;
  }
  // Now that we're inside the combiner, copy the pending connectivity
  // state (which was set by the connectivity state watcher) to
  // curr_connectivity_state, which is what we use inside of the combiner.
  sd->curr_connectivity_state = sd->pending_connectivity_state_unsafe;
  // Update state counters and determine new overall state.
  update_state_counters_locked(sd);
  sd->prev_connectivity_state = sd->curr_connectivity_state;
  const grpc_connectivity_state new_policy_connectivity_state =
      update_lb_connectivity_status_locked(exec_ctx, sd, GRPC_ERROR_REF(error));
  // If the sd's new state is SHUTDOWN, unref the subchannel, and if the new
  // policy's state is SHUTDOWN, clean up.
  if (sd->curr_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
    GRPC_SUBCHANNEL_UNREF(exec_ctx, sd->subchannel, "rr_subchannel_shutdown");
    sd->subchannel = NULL;
    if (sd->user_data != NULL) {
      GPR_ASSERT(sd->user_data_vtable != NULL);
      sd->user_data_vtable->destroy(exec_ctx, sd->user_data);
      sd->user_data = NULL;
    }
    if (new_policy_connectivity_state == GRPC_CHANNEL_SHUTDOWN) {
      // the policy is shutting down. Flush all the pending picks...
      pending_pick *pp;
      while ((pp = p->pending_picks)) {
        p->pending_picks = pp->next;
        *pp->target = NULL;
        GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
        gpr_free(pp);
      }
    }
    rr_subchannel_list_unref(exec_ctx, sd->subchannel_list,
                             "sd_shutdown+started_picking");
    // unref the "rr_connectivity_update" weak ref from start_picking.
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base,
                              "rr_connectivity_sd_shutdown");
  } else {  // sd not in SHUTDOWN
    if (sd->curr_connectivity_state == GRPC_CHANNEL_READY) {
      if (sd->subchannel_list != p->subchannel_list) {
        // promote sd->subchannel_list to p->subchannel_list.
        // sd->subchannel_list must be equal to
        // p->latest_pending_subchannel_list because we have already filtered
        // for sds belonging to outdated subchannel lists.
        GPR_ASSERT(sd->subchannel_list == p->latest_pending_subchannel_list);
        GPR_ASSERT(!sd->subchannel_list->shutting_down);
        if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
          const unsigned long num_subchannels =
              p->subchannel_list != NULL
                  ? (unsigned long)p->subchannel_list->num_subchannels
                  : 0;
          gpr_log(GPR_DEBUG,
                  "[RR %p] phasing out subchannel list %p (size %lu) in favor "
                  "of %p (size %lu)",
                  (void *)p, (void *)p->subchannel_list, num_subchannels,
                  (void *)sd->subchannel_list, num_subchannels);
        }
        if (p->subchannel_list != NULL) {
          // dispose of the current subchannel_list
          rr_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                                "sl_phase_out_shutdown");
        }
        p->subchannel_list = p->latest_pending_subchannel_list;
        p->latest_pending_subchannel_list = NULL;
      }
      /* at this point we know there's at least one suitable subchannel. Go
       * ahead and pick one and notify the pending suitors in
       * p->pending_picks. This preemtively replicates rr_pick()'s actions. */
      const size_t next_ready_index = get_next_ready_subchannel_index_locked(p);
      GPR_ASSERT(next_ready_index < p->subchannel_list->num_subchannels);
      subchannel_data *selected =
          &p->subchannel_list->subchannels[next_ready_index];
      if (p->pending_picks != NULL) {
        // if the selected subchannel is going to be used for the pending
        // picks, update the last picked pointer
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
                  "[RR %p] Fulfilling pending pick. Target <-- subchannel %p "
                  "(subchannel_list %p, index %lu)",
                  (void *)p, (void *)selected->subchannel,
                  (void *)p->subchannel_list, (unsigned long)next_ready_index);
        }
        GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
        gpr_free(pp);
      }
    }
    /* renew notification: reuses the "rr_connectivity_update" weak ref on the
     * policy as well as the sd->subchannel_list ref. */
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
  if (next_ready_index < p->subchannel_list->num_subchannels) {
    subchannel_data *selected =
        &p->subchannel_list->subchannels[next_ready_index];
    grpc_connected_subchannel *target = GRPC_CONNECTED_SUBCHANNEL_REF(
        grpc_subchannel_get_connected_subchannel(selected->subchannel),
        "rr_picked");
    grpc_connected_subchannel_ping(exec_ctx, target, closure);
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, target, "rr_picked");
  } else {
    GRPC_CLOSURE_SCHED(exec_ctx, closure, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                              "Round Robin not connected"));
  }
}

static void rr_update_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                             const grpc_lb_policy_args *args) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)policy;
  /* Find the number of backend addresses. We ignore balancer addresses, since
   * we don't know how to handle them. */
  const grpc_arg *arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == NULL || arg->type != GRPC_ARG_POINTER) {
    if (p->subchannel_list == NULL) {
      // If we don't have a current subchannel list, go into TRANSIENT FAILURE.
      grpc_connectivity_state_set(
          exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "rr_update_missing");
    } else {
      // otherwise, keep using the current subchannel list (ignore this update).
      gpr_log(GPR_ERROR,
              "[RR %p] No valid LB addresses channel arg for update, ignoring.",
              (void *)p);
    }
    return;
  }
  grpc_lb_addresses *addresses = arg->value.pointer.p;
  size_t num_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    if (!addresses->addresses[i].is_balancer) ++num_addrs;
  }
  rr_subchannel_list *subchannel_list = rr_subchannel_list_create(p, num_addrs);
  if (num_addrs == 0) {
    grpc_connectivity_state_set(
        exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
        "rr_update_empty");
    if (p->subchannel_list != NULL) {
      rr_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                            "sl_shutdown_empty_update");
    }
    p->subchannel_list = subchannel_list;  // empty list
    return;
  }
  size_t subchannel_index = 0;
  if (p->latest_pending_subchannel_list != NULL && p->started_picking) {
    if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_DEBUG,
              "[RR %p] Shutting down latest pending subchannel list %p, about "
              "to be replaced by newer latest %p",
              (void *)p, (void *)p->latest_pending_subchannel_list,
              (void *)subchannel_list);
    }
    rr_subchannel_list_shutdown_and_unref(
        exec_ctx, p->latest_pending_subchannel_list, "sl_outdated_dont_smash");
  }
  p->latest_pending_subchannel_list = subchannel_list;
  grpc_subchannel_args sc_args;
  /* We need to remove the LB addresses in order to be able to compare the
   * subchannel keys of subchannels from a different batch of addresses. */
  static const char *keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS,
                                         GRPC_ARG_LB_ADDRESSES};
  /* Create subchannels for addresses in the update. */
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
      gpr_log(
          GPR_DEBUG,
          "[RR %p] index %lu: Created subchannel %p for address uri %s into "
          "subchannel_list %p",
          (void *)p, (unsigned long)subchannel_index, (void *)subchannel,
          address_uri, (void *)subchannel_list);
      gpr_free(address_uri);
    }
    grpc_channel_args_destroy(exec_ctx, new_args);

    subchannel_data *sd = &subchannel_list->subchannels[subchannel_index++];
    sd->subchannel_list = subchannel_list;
    sd->subchannel = subchannel;
    GRPC_CLOSURE_INIT(&sd->connectivity_changed_closure,
                      rr_connectivity_changed_locked, sd,
                      grpc_combiner_scheduler(args->combiner));
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
    if (p->started_picking) {
      rr_subchannel_list_ref(sd->subchannel_list, "update_started_picking");
      GRPC_LB_POLICY_WEAK_REF(&p->base, "rr_connectivity_update");
      /* 2. Watch every new subchannel. A subchannel list becomes active the
       * moment one of its subchannels is READY. At that moment, we swap
       * p->subchannel_list for sd->subchannel_list, provided the subchannel
       * list is still valid (ie, isn't shutting down) */
      grpc_subchannel_notify_on_state_change(
          exec_ctx, sd->subchannel, p->base.interested_parties,
          &sd->pending_connectivity_state_unsafe,
          &sd->connectivity_changed_closure);
    }
  }
  if (!p->started_picking) {
    // The policy isn't picking yet. Save the update for later, disposing of
    // previous version if any.
    if (p->subchannel_list != NULL) {
      rr_subchannel_list_shutdown_and_unref(exec_ctx, p->subchannel_list,
                                            "rr_update_before_started_picking");
    }
    p->subchannel_list = subchannel_list;
    p->latest_pending_subchannel_list = NULL;
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
    rr_notify_on_state_change_locked,
    rr_update_locked};

static void round_robin_factory_ref(grpc_lb_policy_factory *factory) {}

static void round_robin_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *round_robin_create(grpc_exec_ctx *exec_ctx,
                                          grpc_lb_policy_factory *factory,
                                          grpc_lb_policy_args *args) {
  GPR_ASSERT(args->client_channel_factory != NULL);
  round_robin_lb_policy *p = gpr_zalloc(sizeof(*p));
  grpc_lb_policy_init(&p->base, &round_robin_lb_policy_vtable, args->combiner);
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE,
                               "round_robin");
  rr_update_locked(exec_ctx, &p->base, args);
  if (GRPC_TRACER_ON(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_DEBUG, "[RR %p] Created with %lu subchannels", (void *)p,
            (unsigned long)p->subchannel_list->num_subchannels);
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
  grpc_register_tracer(&grpc_lb_round_robin_trace);
}

void grpc_lb_policy_round_robin_shutdown() {}
