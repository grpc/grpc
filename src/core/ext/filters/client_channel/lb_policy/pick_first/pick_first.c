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

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_index.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"

grpc_tracer_flag grpc_lb_pick_first_trace =
    GRPC_TRACER_INITIALIZER(false, "pick_first");

typedef struct pending_pick {
  struct pending_pick *next;
  uint32_t initial_metadata_flags;
  grpc_connected_subchannel **target;
  grpc_closure *on_complete;
} pending_pick;

typedef struct {
  /** base policy: must be first */
  grpc_lb_policy base;
  /** all our subchannels */
  grpc_subchannel **subchannels;
  grpc_subchannel **new_subchannels;
  size_t num_subchannels;
  size_t num_new_subchannels;

  grpc_closure connectivity_changed;

  /** remaining members are protected by the combiner */

  /** the selected channel */
  grpc_connected_subchannel *selected;

  /** the subchannel key for \a selected, or NULL if \a selected not set */
  const grpc_subchannel_key *selected_key;

  /** have we started picking? */
  bool started_picking;
  /** are we shut down? */
  bool shutdown;
  /** are we updating the selected subchannel? */
  bool updating_selected;
  /** are we updating the subchannel candidates? */
  bool updating_subchannels;
  /** args from the latest update received while already updating, or NULL */
  grpc_lb_policy_args *pending_update_args;
  /** which subchannel are we watching? */
  size_t checking_subchannel;
  /** what is the connectivity of that channel? */
  grpc_connectivity_state checking_connectivity;
  /** list of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;
} pick_first_lb_policy;

static void pf_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  GPR_ASSERT(p->pending_picks == NULL);
  for (size_t i = 0; i < p->num_subchannels; i++) {
    GRPC_SUBCHANNEL_UNREF(exec_ctx, p->subchannels[i], "pick_first_destroy");
  }
  if (p->selected != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, p->selected,
                                    "picked_first_destroy");
  }
  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  if (p->pending_update_args != NULL) {
    grpc_channel_args_destroy(exec_ctx, p->pending_update_args->args);
    gpr_free(p->pending_update_args);
  }
  gpr_free(p->subchannels);
  gpr_free(p->new_subchannels);
  gpr_free(p);
  if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_DEBUG, "Pick First %p destroyed.", (void *)p);
  }
}

static void pf_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  p->shutdown = true;
  pp = p->pending_picks;
  p->pending_picks = NULL;
  grpc_connectivity_state_set(
      exec_ctx, &p->state_tracker, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Channel shutdown"), "shutdown");
  /* cancel subscription */
  if (p->selected != NULL) {
    grpc_connected_subchannel_notify_on_state_change(
        exec_ctx, p->selected, NULL, NULL, &p->connectivity_changed);
  } else if (p->num_subchannels > 0 && p->started_picking) {
    grpc_subchannel_notify_on_state_change(
        exec_ctx, p->subchannels[p->checking_subchannel], NULL, NULL,
        &p->connectivity_changed);
  }
  while (pp != NULL) {
    pending_pick *next = pp->next;
    *pp->target = NULL;
    GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
    gpr_free(pp);
    pp = next;
  }
}

static void pf_cancel_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                                  grpc_connected_subchannel **target,
                                  grpc_error *error) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      *target = NULL;
      GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  GRPC_ERROR_UNREF(error);
}

static void pf_cancel_picks_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                                   uint32_t initial_metadata_flags_mask,
                                   uint32_t initial_metadata_flags_eq,
                                   grpc_error *error) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if ((pp->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete,
                         GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Pick Cancelled", &error, 1));
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
                                 pick_first_lb_policy *p) {
  p->started_picking = true;
  if (p->subchannels != NULL) {
    GPR_ASSERT(p->num_subchannels > 0);
    p->checking_subchannel = 0;
    p->checking_connectivity = GRPC_CHANNEL_IDLE;
    GRPC_LB_POLICY_WEAK_REF(&p->base, "pick_first_connectivity");
    grpc_subchannel_notify_on_state_change(
        exec_ctx, p->subchannels[p->checking_subchannel],
        p->base.interested_parties, &p->checking_connectivity,
        &p->connectivity_changed);
  }
}

static void pf_exit_idle_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  if (!p->started_picking) {
    start_picking_locked(exec_ctx, p);
  }
}

static int pf_pick_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                          const grpc_lb_policy_pick_args *pick_args,
                          grpc_connected_subchannel **target,
                          grpc_call_context_element *context, void **user_data,
                          grpc_closure *on_complete) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;

  /* Check atomically for a selected channel */
  if (p->selected != NULL) {
    *target = GRPC_CONNECTED_SUBCHANNEL_REF(p->selected, "picked");
    return 1;
  }

  /* No subchannel selected yet, so try again */
  if (!p->started_picking) {
    start_picking_locked(exec_ctx, p);
  }
  pp = gpr_malloc(sizeof(*pp));
  pp->next = p->pending_picks;
  pp->target = target;
  pp->initial_metadata_flags = pick_args->initial_metadata_flags;
  pp->on_complete = on_complete;
  p->pending_picks = pp;
  return 0;
}

static void destroy_subchannels_locked(grpc_exec_ctx *exec_ctx,
                                       pick_first_lb_policy *p) {
  size_t num_subchannels = p->num_subchannels;
  grpc_subchannel **subchannels = p->subchannels;

  p->num_subchannels = 0;
  p->subchannels = NULL;
  GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "destroy_subchannels");

  for (size_t i = 0; i < num_subchannels; i++) {
    GRPC_SUBCHANNEL_UNREF(exec_ctx, subchannels[i], "pick_first");
  }
  gpr_free(subchannels);
}

static grpc_connectivity_state pf_check_connectivity_locked(
    grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol, grpc_error **error) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  return grpc_connectivity_state_get(&p->state_tracker, error);
}

static void pf_notify_on_state_change_locked(grpc_exec_ctx *exec_ctx,
                                             grpc_lb_policy *pol,
                                             grpc_connectivity_state *current,
                                             grpc_closure *notify) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  grpc_connectivity_state_notify_on_state_change(exec_ctx, &p->state_tracker,
                                                 current, notify);
}

static void pf_ping_one_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                               grpc_closure *closure) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  if (p->selected) {
    grpc_connected_subchannel_ping(exec_ctx, p->selected, closure);
  } else {
    GRPC_CLOSURE_SCHED(exec_ctx, closure,
                       GRPC_ERROR_CREATE_FROM_STATIC_STRING("Not connected"));
  }
}

/* unsubscribe all subchannels */
static void stop_connectivity_watchers(grpc_exec_ctx *exec_ctx,
                                       pick_first_lb_policy *p) {
  if (p->num_subchannels > 0) {
    GPR_ASSERT(p->selected == NULL);
    if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_DEBUG, "Pick First %p unsubscribing from subchannel %p",
              (void *)p, (void *)p->subchannels[p->checking_subchannel]);
    }
    grpc_subchannel_notify_on_state_change(
        exec_ctx, p->subchannels[p->checking_subchannel], NULL, NULL,
        &p->connectivity_changed);
    p->updating_subchannels = true;
  } else if (p->selected != NULL) {
    if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_DEBUG,
              "Pick First %p unsubscribing from selected subchannel %p",
              (void *)p, (void *)p->selected);
    }
    grpc_connected_subchannel_notify_on_state_change(
        exec_ctx, p->selected, NULL, NULL, &p->connectivity_changed);
    p->updating_selected = true;
  }
}

/* true upon success */
static void pf_update_locked(grpc_exec_ctx *exec_ctx, grpc_lb_policy *policy,
                             const grpc_lb_policy_args *args) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)policy;
  /* Find the number of backend addresses. We ignore balancer
   * addresses, since we don't know how to handle them. */
  const grpc_arg *arg =
      grpc_channel_args_find(args->args, GRPC_ARG_LB_ADDRESSES);
  if (arg == NULL || arg->type != GRPC_ARG_POINTER) {
    if (p->subchannels == NULL) {
      // If we don't have a current subchannel list, go into TRANSIENT FAILURE.
      grpc_connectivity_state_set(
          exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Missing update in args"),
          "pf_update_missing");
    } else {
      // otherwise, keep using the current subchannel list (ignore this update).
      gpr_log(GPR_ERROR,
              "No valid LB addresses channel arg for Pick First %p update, "
              "ignoring.",
              (void *)p);
    }
    return;
  }
  const grpc_lb_addresses *addresses = arg->value.pointer.p;
  size_t num_addrs = 0;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    if (!addresses->addresses[i].is_balancer) ++num_addrs;
  }
  if (num_addrs == 0) {
    // Empty update. Unsubscribe from all current subchannels and put the
    // channel in TRANSIENT_FAILURE.
    grpc_connectivity_state_set(
        exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
        "pf_update_empty");
    stop_connectivity_watchers(exec_ctx, p);
    return;
  }
  if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "Pick First %p received update with %lu addresses",
            (void *)p, (unsigned long)num_addrs);
  }
  grpc_subchannel_args *sc_args = gpr_zalloc(sizeof(*sc_args) * num_addrs);
  /* We remove the following keys in order for subchannel keys belonging to
   * subchannels point to the same address to match. */
  static const char *keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS,
                                         GRPC_ARG_LB_ADDRESSES};
  size_t sc_args_count = 0;

  /* Create list of subchannel args for new addresses in \a args. */
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    if (addresses->addresses[i].is_balancer) continue;
    if (addresses->addresses[i].user_data != NULL) {
      gpr_log(GPR_ERROR,
              "This LB policy doesn't support user data. It will be ignored");
    }
    grpc_arg addr_arg =
        grpc_create_subchannel_address_arg(&addresses->addresses[i].address);
    grpc_channel_args *new_args = grpc_channel_args_copy_and_add_and_remove(
        args->args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), &addr_arg,
        1);
    gpr_free(addr_arg.value.string);
    sc_args[sc_args_count++].args = new_args;
  }

  /* Check if p->selected is amongst them. If so, we are done. */
  if (p->selected != NULL) {
    GPR_ASSERT(p->selected_key != NULL);
    for (size_t i = 0; i < sc_args_count; i++) {
      grpc_subchannel_key *ith_sc_key = grpc_subchannel_key_create(&sc_args[i]);
      const bool found_selected =
          grpc_subchannel_key_compare(p->selected_key, ith_sc_key) == 0;
      grpc_subchannel_key_destroy(exec_ctx, ith_sc_key);
      if (found_selected) {
        // The currently selected subchannel is in the update: we are done.
        if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
          gpr_log(GPR_INFO,
                  "Pick First %p found already selected subchannel %p amongst "
                  "updates. Update done.",
                  (void *)p, (void *)p->selected);
        }
        for (size_t j = 0; j < sc_args_count; j++) {
          grpc_channel_args_destroy(exec_ctx,
                                    (grpc_channel_args *)sc_args[j].args);
        }
        gpr_free(sc_args);
        return;
      }
    }
  }
  // We only check for already running updates here because if the previous
  // steps were successful, the update can be considered done without any
  // interference (ie, no callbacks were scheduled).
  if (p->updating_selected || p->updating_subchannels) {
    if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "Update already in progress for pick first %p. Deferring update.",
              (void *)p);
    }
    if (p->pending_update_args != NULL) {
      grpc_channel_args_destroy(exec_ctx, p->pending_update_args->args);
      gpr_free(p->pending_update_args);
    }
    p->pending_update_args = gpr_zalloc(sizeof(*p->pending_update_args));
    p->pending_update_args->client_channel_factory =
        args->client_channel_factory;
    p->pending_update_args->args = grpc_channel_args_copy(args->args);
    p->pending_update_args->combiner = args->combiner;
    return;
  }
  /* Create the subchannels for the new subchannel args/addresses. */
  grpc_subchannel **new_subchannels =
      gpr_zalloc(sizeof(*new_subchannels) * sc_args_count);
  size_t num_new_subchannels = 0;
  for (size_t i = 0; i < sc_args_count; i++) {
    grpc_subchannel *subchannel = grpc_client_channel_factory_create_subchannel(
        exec_ctx, args->client_channel_factory, &sc_args[i]);
    if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
      char *address_uri =
          grpc_sockaddr_to_uri(&addresses->addresses[i].address);
      gpr_log(GPR_INFO,
              "Pick First %p created subchannel %p for address uri %s",
              (void *)p, (void *)subchannel, address_uri);
      gpr_free(address_uri);
    }
    grpc_channel_args_destroy(exec_ctx, (grpc_channel_args *)sc_args[i].args);
    if (subchannel != NULL) new_subchannels[num_new_subchannels++] = subchannel;
  }
  gpr_free(sc_args);
  if (num_new_subchannels == 0) {
    gpr_free(new_subchannels);
    // Empty update. Unsubscribe from all current subchannels and put the
    // channel in TRANSIENT_FAILURE.
    grpc_connectivity_state_set(
        exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No valid addresses in update"),
        "pf_update_no_valid_addresses");
    stop_connectivity_watchers(exec_ctx, p);
    return;
  }

  /* Destroy the current subchannels. Repurpose pf_shutdown/destroy. */
  stop_connectivity_watchers(exec_ctx, p);

  /* Save new subchannels. The switch over will happen in
   * pf_connectivity_changed_locked */
  if (p->updating_selected || p->updating_subchannels) {
    p->num_new_subchannels = num_new_subchannels;
    p->new_subchannels = new_subchannels;
  } else { /* nothing is updating. Get things moving from here */
    p->num_subchannels = num_new_subchannels;
    p->subchannels = new_subchannels;
    p->new_subchannels = NULL;
    p->num_new_subchannels = 0;
    if (p->started_picking) {
      p->checking_subchannel = 0;
      p->checking_connectivity = GRPC_CHANNEL_IDLE;
      grpc_subchannel_notify_on_state_change(
          exec_ctx, p->subchannels[p->checking_subchannel],
          p->base.interested_parties, &p->checking_connectivity,
          &p->connectivity_changed);
    }
  }
}

static void pf_connectivity_changed_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  pick_first_lb_policy *p = arg;
  grpc_subchannel *selected_subchannel;
  pending_pick *pp;

  if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
    gpr_log(
        GPR_DEBUG,
        "Pick First %p connectivity changed. Updating selected: %d; Updating "
        "subchannels: %d; Checking %lu index (%lu total); State: %d; ",
        (void *)p, p->updating_selected, p->updating_subchannels,
        (unsigned long)p->checking_subchannel,
        (unsigned long)p->num_subchannels, p->checking_connectivity);
  }
  bool restart = false;
  if (p->updating_selected && error != GRPC_ERROR_NONE) {
    /* Captured the unsubscription for p->selected */
    GPR_ASSERT(p->selected != NULL);
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, p->selected,
                                    "pf_update_connectivity");
    if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_DEBUG, "Pick First %p unreffing selected subchannel %p",
              (void *)p, (void *)p->selected);
    }
    p->updating_selected = false;
    if (p->num_new_subchannels == 0) {
      p->selected = NULL;
      return;
    }
    restart = true;
  }
  if (p->updating_subchannels && error != GRPC_ERROR_NONE) {
    /* Captured the unsubscription for the checking subchannel */
    GPR_ASSERT(p->selected == NULL);
    for (size_t i = 0; i < p->num_subchannels; i++) {
      GRPC_SUBCHANNEL_UNREF(exec_ctx, p->subchannels[i],
                            "pf_update_connectivity");
      if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
        gpr_log(GPR_DEBUG, "Pick First %p unreffing subchannel %p", (void *)p,
                (void *)p->subchannels[i]);
      }
    }
    gpr_free(p->subchannels);
    p->subchannels = NULL;
    p->num_subchannels = 0;
    p->updating_subchannels = false;
    if (p->num_new_subchannels == 0) return;
    restart = true;
  }
  if (restart) {
    p->selected = NULL;
    p->selected_key = NULL;
    GPR_ASSERT(p->new_subchannels != NULL);
    GPR_ASSERT(p->num_new_subchannels > 0);
    p->num_subchannels = p->num_new_subchannels;
    p->subchannels = p->new_subchannels;
    p->num_new_subchannels = 0;
    p->new_subchannels = NULL;
    if (p->started_picking) {
      /* If we were picking, continue to do so over the new subchannels,
       * starting from the 0th index. */
      p->checking_subchannel = 0;
      p->checking_connectivity = GRPC_CHANNEL_IDLE;
      /* reuses the weak ref from start_picking_locked */
      grpc_subchannel_notify_on_state_change(
          exec_ctx, p->subchannels[p->checking_subchannel],
          p->base.interested_parties, &p->checking_connectivity,
          &p->connectivity_changed);
    }
    if (p->pending_update_args != NULL) {
      const grpc_lb_policy_args *args = p->pending_update_args;
      p->pending_update_args = NULL;
      pf_update_locked(exec_ctx, &p->base, args);
    }
    return;
  }
  GRPC_ERROR_REF(error);
  if (p->shutdown) {
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "pick_first_connectivity");
    GRPC_ERROR_UNREF(error);
    return;
  } else if (p->selected != NULL) {
    if (p->checking_connectivity == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      /* if the selected channel goes bad, we're done */
      p->checking_connectivity = GRPC_CHANNEL_SHUTDOWN;
    }
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                p->checking_connectivity, GRPC_ERROR_REF(error),
                                "selected_changed");
    if (p->checking_connectivity != GRPC_CHANNEL_SHUTDOWN) {
      grpc_connected_subchannel_notify_on_state_change(
          exec_ctx, p->selected, p->base.interested_parties,
          &p->checking_connectivity, &p->connectivity_changed);
    } else {
      GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "pick_first_connectivity");
    }
  } else {
  loop:
    switch (p->checking_connectivity) {
      case GRPC_CHANNEL_INIT:
        GPR_UNREACHABLE_CODE(return );
      case GRPC_CHANNEL_READY:
        grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                    GRPC_CHANNEL_READY, GRPC_ERROR_NONE,
                                    "connecting_ready");
        selected_subchannel = p->subchannels[p->checking_subchannel];
        p->selected = GRPC_CONNECTED_SUBCHANNEL_REF(
            grpc_subchannel_get_connected_subchannel(selected_subchannel),
            "picked_first");

        if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
          gpr_log(GPR_INFO,
                  "Pick First %p selected subchannel %p (connected %p)",
                  (void *)p, (void *)selected_subchannel, (void *)p->selected);
        }
        p->selected_key = grpc_subchannel_get_key(selected_subchannel);
        /* drop the pick list: we are connected now */
        GRPC_LB_POLICY_WEAK_REF(&p->base, "destroy_subchannels");
        destroy_subchannels_locked(exec_ctx, p);
        /* update any calls that were waiting for a pick */
        while ((pp = p->pending_picks)) {
          p->pending_picks = pp->next;
          *pp->target = GRPC_CONNECTED_SUBCHANNEL_REF(p->selected, "picked");
          if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
            gpr_log(GPR_INFO,
                    "Servicing pending pick with selected subchannel %p",
                    (void *)p->selected);
          }
          GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
          gpr_free(pp);
        }
        grpc_connected_subchannel_notify_on_state_change(
            exec_ctx, p->selected, p->base.interested_parties,
            &p->checking_connectivity, &p->connectivity_changed);
        break;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        p->checking_subchannel =
            (p->checking_subchannel + 1) % p->num_subchannels;
        if (p->checking_subchannel == 0) {
          /* only trigger transient failure when we've tried all alternatives
           */
          grpc_connectivity_state_set(
              exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
              GRPC_ERROR_REF(error), "connecting_transient_failure");
        }
        GRPC_ERROR_UNREF(error);
        p->checking_connectivity = grpc_subchannel_check_connectivity(
            p->subchannels[p->checking_subchannel], &error);
        if (p->checking_connectivity == GRPC_CHANNEL_TRANSIENT_FAILURE) {
          grpc_subchannel_notify_on_state_change(
              exec_ctx, p->subchannels[p->checking_subchannel],
              p->base.interested_parties, &p->checking_connectivity,
              &p->connectivity_changed);
        } else {
          goto loop;
        }
        break;
      case GRPC_CHANNEL_CONNECTING:
      case GRPC_CHANNEL_IDLE:
        grpc_connectivity_state_set(
            exec_ctx, &p->state_tracker, GRPC_CHANNEL_CONNECTING,
            GRPC_ERROR_REF(error), "connecting_changed");
        grpc_subchannel_notify_on_state_change(
            exec_ctx, p->subchannels[p->checking_subchannel],
            p->base.interested_parties, &p->checking_connectivity,
            &p->connectivity_changed);
        break;
      case GRPC_CHANNEL_SHUTDOWN:
        p->num_subchannels--;
        GPR_SWAP(grpc_subchannel *, p->subchannels[p->checking_subchannel],
                 p->subchannels[p->num_subchannels]);
        GRPC_SUBCHANNEL_UNREF(exec_ctx, p->subchannels[p->num_subchannels],
                              "pick_first");
        if (p->num_subchannels == 0) {
          grpc_connectivity_state_set(
              exec_ctx, &p->state_tracker, GRPC_CHANNEL_SHUTDOWN,
              GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                  "Pick first exhausted channels", &error, 1),
              "no_more_channels");
          while ((pp = p->pending_picks)) {
            p->pending_picks = pp->next;
            *pp->target = NULL;
            GRPC_CLOSURE_SCHED(exec_ctx, pp->on_complete, GRPC_ERROR_NONE);
            gpr_free(pp);
          }
          GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base,
                                    "pick_first_connectivity");
        } else {
          grpc_connectivity_state_set(
              exec_ctx, &p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
              GRPC_ERROR_REF(error), "subchannel_failed");
          p->checking_subchannel %= p->num_subchannels;
          GRPC_ERROR_UNREF(error);
          p->checking_connectivity = grpc_subchannel_check_connectivity(
              p->subchannels[p->checking_subchannel], &error);
          goto loop;
        }
    }
  }

  GRPC_ERROR_UNREF(error);
}

static const grpc_lb_policy_vtable pick_first_lb_policy_vtable = {
    pf_destroy,
    pf_shutdown_locked,
    pf_pick_locked,
    pf_cancel_pick_locked,
    pf_cancel_picks_locked,
    pf_ping_one_locked,
    pf_exit_idle_locked,
    pf_check_connectivity_locked,
    pf_notify_on_state_change_locked,
    pf_update_locked};

static void pick_first_factory_ref(grpc_lb_policy_factory *factory) {}

static void pick_first_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *create_pick_first(grpc_exec_ctx *exec_ctx,
                                         grpc_lb_policy_factory *factory,
                                         grpc_lb_policy_args *args) {
  GPR_ASSERT(args->client_channel_factory != NULL);
  pick_first_lb_policy *p = gpr_zalloc(sizeof(*p));
  if (GRPC_TRACER_ON(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_DEBUG, "Pick First %p created.", (void *)p);
  }
  pf_update_locked(exec_ctx, &p->base, args);
  grpc_lb_policy_init(&p->base, &pick_first_lb_policy_vtable, args->combiner);
  GRPC_CLOSURE_INIT(&p->connectivity_changed, pf_connectivity_changed_locked, p,
                    grpc_combiner_scheduler(args->combiner));
  return &p->base;
}

static const grpc_lb_policy_factory_vtable pick_first_factory_vtable = {
    pick_first_factory_ref, pick_first_factory_unref, create_pick_first,
    "pick_first"};

static grpc_lb_policy_factory pick_first_lb_policy_factory = {
    &pick_first_factory_vtable};

static grpc_lb_policy_factory *pick_first_lb_factory_create() {
  return &pick_first_lb_policy_factory;
}

/* Plugin registration */

void grpc_lb_policy_pick_first_init() {
  grpc_register_lb_policy(pick_first_lb_factory_create());
  grpc_register_tracer(&grpc_lb_pick_first_trace);
}

void grpc_lb_policy_pick_first_shutdown() {}
