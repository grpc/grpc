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

#include <string.h>

#include <grpc/support/alloc.h>
#include "src/core/ext/client_config/lb_policy_registry.h"
#include "src/core/lib/transport/connectivity_state.h"

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
  size_t num_subchannels;

  grpc_closure connectivity_changed;

  /** the selected channel (a grpc_connected_subchannel) */
  gpr_atm selected;

  /** mutex protecting remaining members */
  gpr_mu mu;
  /** have we started picking? */
  int started_picking;
  /** are we shut down? */
  int shutdown;
  /** which subchannel are we watching? */
  size_t checking_subchannel;
  /** what is the connectivity of that channel? */
  grpc_connectivity_state checking_connectivity;
  /** list of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;
} pick_first_lb_policy;

#define GET_SELECTED(p) \
  ((grpc_connected_subchannel *)gpr_atm_acq_load(&(p)->selected))

static void pf_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  grpc_connected_subchannel *selected = GET_SELECTED(p);
  size_t i;
  GPR_ASSERT(p->pending_picks == NULL);
  for (i = 0; i < p->num_subchannels; i++) {
    GRPC_SUBCHANNEL_UNREF(exec_ctx, p->subchannels[i], "pick_first");
  }
  if (selected != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, selected, "picked_first");
  }
  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  gpr_free(p->subchannels);
  gpr_mu_destroy(&p->mu);
  gpr_free(p);
}

static void pf_shutdown(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  grpc_connected_subchannel *selected;
  gpr_mu_lock(&p->mu);
  selected = GET_SELECTED(p);
  p->shutdown = 1;
  pp = p->pending_picks;
  p->pending_picks = NULL;
  grpc_connectivity_state_set(
      exec_ctx, &p->state_tracker, GRPC_CHANNEL_SHUTDOWN,
      GRPC_ERROR_CREATE("Channel shutdown"), "shutdown");
  /* cancel subscription */
  if (selected != NULL) {
    grpc_connected_subchannel_notify_on_state_change(
        exec_ctx, selected, NULL, NULL, &p->connectivity_changed);
  } else if (p->num_subchannels > 0) {
    grpc_subchannel_notify_on_state_change(
        exec_ctx, p->subchannels[p->checking_subchannel], NULL, NULL,
        &p->connectivity_changed);
  }
  gpr_mu_unlock(&p->mu);
  while (pp != NULL) {
    pending_pick *next = pp->next;
    *pp->target = NULL;
    grpc_exec_ctx_sched(exec_ctx, pp->on_complete, GRPC_ERROR_NONE, NULL);
    gpr_free(pp);
    pp = next;
  }
}

static void pf_cancel_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                           grpc_connected_subchannel **target) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  gpr_mu_lock(&p->mu);
  pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      *target = NULL;
      grpc_exec_ctx_sched(exec_ctx, pp->on_complete,
                          GRPC_ERROR_CREATE("Pick Cancelled"), NULL);
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  gpr_mu_unlock(&p->mu);
}

static void pf_cancel_picks(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                            uint32_t initial_metadata_flags_mask,
                            uint32_t initial_metadata_flags_eq) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  gpr_mu_lock(&p->mu);
  pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if ((pp->initial_metadata_flags & initial_metadata_flags_mask) ==
        initial_metadata_flags_eq) {
      grpc_exec_ctx_sched(exec_ctx, pp->on_complete,
                          GRPC_ERROR_CREATE("Pick Cancelled"), NULL);
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  gpr_mu_unlock(&p->mu);
}

static void start_picking(grpc_exec_ctx *exec_ctx, pick_first_lb_policy *p) {
  p->started_picking = 1;
  p->checking_subchannel = 0;
  p->checking_connectivity = GRPC_CHANNEL_IDLE;
  GRPC_LB_POLICY_WEAK_REF(&p->base, "pick_first_connectivity");
  grpc_subchannel_notify_on_state_change(
      exec_ctx, p->subchannels[p->checking_subchannel],
      p->base.interested_parties, &p->checking_connectivity,
      &p->connectivity_changed);
}

static void pf_exit_idle(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  if (!p->started_picking) {
    start_picking(exec_ctx, p);
  }
  gpr_mu_unlock(&p->mu);
}

static int pf_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                   const grpc_lb_policy_pick_args *pick_args,
                   grpc_connected_subchannel **target, void **user_data,
                   grpc_closure *on_complete) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;

  /* Check atomically for a selected channel */
  grpc_connected_subchannel *selected = GET_SELECTED(p);
  if (selected != NULL) {
    *target = selected;
    return 1;
  }

  /* No subchannel selected yet, so acquire lock and then attempt again */
  gpr_mu_lock(&p->mu);
  selected = GET_SELECTED(p);
  if (selected) {
    gpr_mu_unlock(&p->mu);
    *target = selected;
    return 1;
  } else {
    if (!p->started_picking) {
      start_picking(exec_ctx, p);
    }
    pp = gpr_malloc(sizeof(*pp));
    pp->next = p->pending_picks;
    pp->target = target;
    pp->initial_metadata_flags = pick_args->initial_metadata_flags;
    pp->on_complete = on_complete;
    p->pending_picks = pp;
    gpr_mu_unlock(&p->mu);
    return 0;
  }
}

static void destroy_subchannels(grpc_exec_ctx *exec_ctx, void *arg,
                                grpc_error *error) {
  pick_first_lb_policy *p = arg;
  size_t i;
  size_t num_subchannels = p->num_subchannels;
  grpc_subchannel **subchannels;

  gpr_mu_lock(&p->mu);
  subchannels = p->subchannels;
  p->num_subchannels = 0;
  p->subchannels = NULL;
  gpr_mu_unlock(&p->mu);
  GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "destroy_subchannels");

  for (i = 0; i < num_subchannels; i++) {
    GRPC_SUBCHANNEL_UNREF(exec_ctx, subchannels[i], "pick_first");
  }

  gpr_free(subchannels);
}

static void pf_connectivity_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_error *error) {
  pick_first_lb_policy *p = arg;
  grpc_subchannel *selected_subchannel;
  pending_pick *pp;
  grpc_connected_subchannel *selected;

  GRPC_ERROR_REF(error);

  gpr_mu_lock(&p->mu);

  selected = GET_SELECTED(p);

  if (p->shutdown) {
    gpr_mu_unlock(&p->mu);
    GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "pick_first_connectivity");
    GRPC_ERROR_UNREF(error);
    return;
  } else if (selected != NULL) {
    if (p->checking_connectivity == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      /* if the selected channel goes bad, we're done */
      p->checking_connectivity = GRPC_CHANNEL_SHUTDOWN;
    }
    grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                p->checking_connectivity, GRPC_ERROR_REF(error),
                                "selected_changed");
    if (p->checking_connectivity != GRPC_CHANNEL_SHUTDOWN) {
      grpc_connected_subchannel_notify_on_state_change(
          exec_ctx, selected, p->base.interested_parties,
          &p->checking_connectivity, &p->connectivity_changed);
    } else {
      GRPC_LB_POLICY_WEAK_UNREF(exec_ctx, &p->base, "pick_first_connectivity");
    }
  } else {
  loop:
    switch (p->checking_connectivity) {
      case GRPC_CHANNEL_READY:
        grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                    GRPC_CHANNEL_READY, GRPC_ERROR_NONE,
                                    "connecting_ready");
        selected_subchannel = p->subchannels[p->checking_subchannel];
        selected =
            grpc_subchannel_get_connected_subchannel(selected_subchannel);
        GPR_ASSERT(selected != NULL);
        GRPC_CONNECTED_SUBCHANNEL_REF(selected, "picked_first");
        /* drop the pick list: we are connected now */
        GRPC_LB_POLICY_WEAK_REF(&p->base, "destroy_subchannels");
        gpr_atm_rel_store(&p->selected, (gpr_atm)selected);
        grpc_exec_ctx_sched(exec_ctx,
                            grpc_closure_create(destroy_subchannels, p),
                            GRPC_ERROR_NONE, NULL);
        /* update any calls that were waiting for a pick */
        while ((pp = p->pending_picks)) {
          p->pending_picks = pp->next;
          *pp->target = selected;
          grpc_exec_ctx_sched(exec_ctx, pp->on_complete, GRPC_ERROR_NONE, NULL);
          gpr_free(pp);
        }
        grpc_connected_subchannel_notify_on_state_change(
            exec_ctx, selected, p->base.interested_parties,
            &p->checking_connectivity, &p->connectivity_changed);
        break;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        p->checking_subchannel =
            (p->checking_subchannel + 1) % p->num_subchannels;
        if (p->checking_subchannel == 0) {
          /* only trigger transient failure when we've tried all alternatives */
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
              GRPC_ERROR_CREATE_REFERENCING("Pick first exhausted channels",
                                            &error, 1),
              "no_more_channels");
          while ((pp = p->pending_picks)) {
            p->pending_picks = pp->next;
            *pp->target = NULL;
            grpc_exec_ctx_sched(exec_ctx, pp->on_complete, GRPC_ERROR_NONE,
                                NULL);
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

  gpr_mu_unlock(&p->mu);

  GRPC_ERROR_UNREF(error);
}

static grpc_connectivity_state pf_check_connectivity(grpc_exec_ctx *exec_ctx,
                                                     grpc_lb_policy *pol,
                                                     grpc_error **error) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  grpc_connectivity_state st;
  gpr_mu_lock(&p->mu);
  st = grpc_connectivity_state_check(&p->state_tracker, error);
  gpr_mu_unlock(&p->mu);
  return st;
}

static void pf_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                      grpc_lb_policy *pol,
                                      grpc_connectivity_state *current,
                                      grpc_closure *notify) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  grpc_connectivity_state_notify_on_state_change(exec_ctx, &p->state_tracker,
                                                 current, notify);
  gpr_mu_unlock(&p->mu);
}

static void pf_ping_one(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                        grpc_closure *closure) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  grpc_connected_subchannel *selected = GET_SELECTED(p);
  if (selected) {
    grpc_connected_subchannel_ping(exec_ctx, selected, closure);
  } else {
    grpc_exec_ctx_sched(exec_ctx, closure, GRPC_ERROR_CREATE("Not connected"),
                        NULL);
  }
}

static const grpc_lb_policy_vtable pick_first_lb_policy_vtable = {
    pf_destroy,     pf_shutdown,           pf_pick,
    pf_cancel_pick, pf_cancel_picks,       pf_ping_one,
    pf_exit_idle,   pf_check_connectivity, pf_notify_on_state_change};

static void pick_first_factory_ref(grpc_lb_policy_factory *factory) {}

static void pick_first_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *create_pick_first(grpc_exec_ctx *exec_ctx,
                                         grpc_lb_policy_factory *factory,
                                         grpc_lb_policy_args *args) {
  GPR_ASSERT(args->addresses != NULL);
  GPR_ASSERT(args->client_channel_factory != NULL);

  if (args->num_addresses == 0) return NULL;

  pick_first_lb_policy *p = gpr_malloc(sizeof(*p));
  memset(p, 0, sizeof(*p));

  p->subchannels = gpr_malloc(sizeof(grpc_subchannel *) * args->num_addresses);
  memset(p->subchannels, 0, sizeof(*p->subchannels) * args->num_addresses);
  grpc_subchannel_args sc_args;
  size_t subchannel_idx = 0;
  for (size_t i = 0; i < args->num_addresses; i++) {
    if (args->addresses[i].user_data != NULL) {
      gpr_log(GPR_ERROR,
              "This LB policy doesn't support user data. It will be ignored");
    }

    memset(&sc_args, 0, sizeof(grpc_subchannel_args));
    sc_args.addr =
        (struct sockaddr *)(args->addresses[i].resolved_address->addr);
    sc_args.addr_len = (size_t)args->addresses[i].resolved_address->len;

    grpc_subchannel *subchannel = grpc_client_channel_factory_create_subchannel(
        exec_ctx, args->client_channel_factory, &sc_args);

    if (subchannel != NULL) {
      p->subchannels[subchannel_idx++] = subchannel;
    }
  }
  if (subchannel_idx == 0) {
    gpr_free(p->subchannels);
    gpr_free(p);
    return NULL;
  }
  p->num_subchannels = subchannel_idx;

  grpc_lb_policy_init(&p->base, &pick_first_lb_policy_vtable);
  grpc_closure_init(&p->connectivity_changed, pf_connectivity_changed, p);
  gpr_mu_init(&p->mu);
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
}

void grpc_lb_policy_pick_first_shutdown() {}
