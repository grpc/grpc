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

#include "src/core/client_config/lb_policy_factory.h"
#include "src/core/client_config/lb_policies/pick_first.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include "src/core/transport/connectivity_state.h"

typedef struct pending_pick {
  struct pending_pick *next;
  grpc_pollset *pollset;
  grpc_subchannel **target;
  grpc_closure *on_complete;
} pending_pick;

typedef struct {
  /** base policy: must be first */
  grpc_lb_policy base;
  /** all our subchannels */
  grpc_subchannel **subchannels;
  size_t num_subchannels;

  grpc_closure connectivity_changed;

  /** mutex protecting remaining members */
  gpr_mu mu;
  /** the selected channel
      TODO(ctiller): this should be atomically set so we don't
                     need to take a mutex in the common case */
  grpc_subchannel *selected;
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

static void del_interested_parties_locked(pick_first_lb_policy *p,
                                          grpc_call_list *call_list) {
  pending_pick *pp;
  for (pp = p->pending_picks; pp; pp = pp->next) {
    grpc_subchannel_del_interested_party(p->subchannels[p->checking_subchannel],
                                         pp->pollset, call_list);
  }
}

static void add_interested_parties_locked(pick_first_lb_policy *p,
                                          grpc_call_list *call_list) {
  pending_pick *pp;
  for (pp = p->pending_picks; pp; pp = pp->next) {
    grpc_subchannel_add_interested_party(p->subchannels[p->checking_subchannel],
                                         pp->pollset, call_list);
  }
}

void pf_destroy(grpc_lb_policy *pol, grpc_call_list *call_list) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  size_t i;
  GPR_ASSERT(p->shutdown);
  for (i = 0; i < p->num_subchannels; i++) {
    GRPC_SUBCHANNEL_UNREF(p->subchannels[i], "pick_first", call_list);
  }
  grpc_connectivity_state_destroy(&p->state_tracker, call_list);
  gpr_free(p->subchannels);
  gpr_mu_destroy(&p->mu);
  gpr_free(p);
}

void pf_shutdown(grpc_lb_policy *pol, grpc_call_list *call_list) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  gpr_mu_lock(&p->mu);
  del_interested_parties_locked(p, call_list);
  p->shutdown = 1;
  pp = p->pending_picks;
  p->pending_picks = NULL;
  grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_FATAL_FAILURE,
                              "shutdown", call_list);
  gpr_mu_unlock(&p->mu);
  while (pp != NULL) {
    pending_pick *next = pp->next;
    *pp->target = NULL;
    grpc_call_list_add(call_list, pp->on_complete, 1);
    gpr_free(pp);
    pp = next;
  }
}

static void start_picking(pick_first_lb_policy *p, grpc_call_list *call_list) {
  p->started_picking = 1;
  p->checking_subchannel = 0;
  p->checking_connectivity = GRPC_CHANNEL_IDLE;
  GRPC_LB_POLICY_REF(&p->base, "pick_first_connectivity");
  grpc_subchannel_notify_on_state_change(p->subchannels[p->checking_subchannel],
                                         &p->checking_connectivity,
                                         &p->connectivity_changed, call_list);
}

void pf_exit_idle(grpc_lb_policy *pol, grpc_call_list *call_list) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  if (!p->started_picking) {
    start_picking(p, call_list);
  }
  gpr_mu_unlock(&p->mu);
}

void pf_pick(grpc_lb_policy *pol, grpc_pollset *pollset,
             grpc_metadata_batch *initial_metadata, grpc_subchannel **target,
             grpc_closure *on_complete, grpc_call_list *call_list) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  gpr_mu_lock(&p->mu);
  if (p->selected) {
    gpr_mu_unlock(&p->mu);
    *target = p->selected;
    grpc_call_list_add(call_list, on_complete, 1);
  } else {
    if (!p->started_picking) {
      start_picking(p, call_list);
    }
    grpc_subchannel_add_interested_party(p->subchannels[p->checking_subchannel],
                                         pollset, call_list);
    pp = gpr_malloc(sizeof(*pp));
    pp->next = p->pending_picks;
    pp->pollset = pollset;
    pp->target = target;
    pp->on_complete = on_complete;
    p->pending_picks = pp;
    gpr_mu_unlock(&p->mu);
  }
}

static void pf_connectivity_changed(void *arg, int iomgr_success,
                                    grpc_call_list *call_list) {
  pick_first_lb_policy *p = arg;
  pending_pick *pp;

  gpr_mu_lock(&p->mu);

  if (p->shutdown) {
    GRPC_LB_POLICY_UNREF(&p->base, "pick_first_connectivity", call_list);
  } else if (p->selected != NULL) {
    grpc_connectivity_state_set(&p->state_tracker, p->checking_connectivity,
                                "selected_changed", call_list);
    if (p->checking_connectivity != GRPC_CHANNEL_FATAL_FAILURE) {
      grpc_subchannel_notify_on_state_change(
          p->selected, &p->checking_connectivity, &p->connectivity_changed,
          call_list);
    } else {
      GRPC_LB_POLICY_UNREF(&p->base, "pick_first_connectivity", call_list);
    }
  } else {
  loop:
    switch (p->checking_connectivity) {
      case GRPC_CHANNEL_READY:
        grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_READY,
                                    "connecting_ready", call_list);
        p->selected = p->subchannels[p->checking_subchannel];
        while ((pp = p->pending_picks)) {
          p->pending_picks = pp->next;
          *pp->target = p->selected;
          grpc_subchannel_del_interested_party(p->selected, pp->pollset,
                                               call_list);
          grpc_call_list_add(call_list, pp->on_complete, 1);
          gpr_free(pp);
        }
        grpc_subchannel_notify_on_state_change(
            p->selected, &p->checking_connectivity, &p->connectivity_changed,
            call_list);
        break;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        grpc_connectivity_state_set(&p->state_tracker,
                                    GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    "connecting_transient_failure", call_list);
        del_interested_parties_locked(p, call_list);
        p->checking_subchannel =
            (p->checking_subchannel + 1) % p->num_subchannels;
        p->checking_connectivity = grpc_subchannel_check_connectivity(
            p->subchannels[p->checking_subchannel]);
        add_interested_parties_locked(p, call_list);
        if (p->checking_connectivity == GRPC_CHANNEL_TRANSIENT_FAILURE) {
          grpc_subchannel_notify_on_state_change(
              p->subchannels[p->checking_subchannel], &p->checking_connectivity,
              &p->connectivity_changed, call_list);
        } else {
          goto loop;
        }
        break;
      case GRPC_CHANNEL_CONNECTING:
      case GRPC_CHANNEL_IDLE:
        grpc_connectivity_state_set(&p->state_tracker, p->checking_connectivity,
                                    "connecting_changed", call_list);
        grpc_subchannel_notify_on_state_change(
            p->subchannels[p->checking_subchannel], &p->checking_connectivity,
            &p->connectivity_changed, call_list);
        break;
      case GRPC_CHANNEL_FATAL_FAILURE:
        del_interested_parties_locked(p, call_list);
        GPR_SWAP(grpc_subchannel *, p->subchannels[p->checking_subchannel],
                 p->subchannels[p->num_subchannels - 1]);
        p->num_subchannels--;
        GRPC_SUBCHANNEL_UNREF(p->subchannels[p->num_subchannels], "pick_first",
                              call_list);
        if (p->num_subchannels == 0) {
          grpc_connectivity_state_set(&p->state_tracker,
                                      GRPC_CHANNEL_FATAL_FAILURE,
                                      "no_more_channels", call_list);
          while ((pp = p->pending_picks)) {
            p->pending_picks = pp->next;
            *pp->target = NULL;
            grpc_call_list_add(call_list, pp->on_complete, 1);
            gpr_free(pp);
          }
          GRPC_LB_POLICY_UNREF(&p->base, "pick_first_connectivity", call_list);
        } else {
          grpc_connectivity_state_set(&p->state_tracker,
                                      GRPC_CHANNEL_TRANSIENT_FAILURE,
                                      "subchannel_failed", call_list);
          p->checking_subchannel %= p->num_subchannels;
          p->checking_connectivity = grpc_subchannel_check_connectivity(
              p->subchannels[p->checking_subchannel]);
          add_interested_parties_locked(p, call_list);
          goto loop;
        }
    }
  }

  gpr_mu_unlock(&p->mu);
}

static void pf_broadcast(grpc_lb_policy *pol, grpc_transport_op *op,
                         grpc_call_list *call_list) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  size_t i;
  size_t n;
  grpc_subchannel **subchannels;

  gpr_mu_lock(&p->mu);
  n = p->num_subchannels;
  subchannels = gpr_malloc(n * sizeof(*subchannels));
  for (i = 0; i < n; i++) {
    subchannels[i] = p->subchannels[i];
    GRPC_SUBCHANNEL_REF(subchannels[i], "pf_broadcast");
  }
  gpr_mu_unlock(&p->mu);

  for (i = 0; i < n; i++) {
    grpc_subchannel_process_transport_op(subchannels[i], op, call_list);
    GRPC_SUBCHANNEL_UNREF(subchannels[i], "pf_broadcast", call_list);
  }
  gpr_free(subchannels);
}

static grpc_connectivity_state pf_check_connectivity(
    grpc_lb_policy *pol, grpc_call_list *call_list) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  grpc_connectivity_state st;
  gpr_mu_lock(&p->mu);
  st = grpc_connectivity_state_check(&p->state_tracker);
  gpr_mu_unlock(&p->mu);
  return st;
}

void pf_notify_on_state_change(grpc_lb_policy *pol,
                               grpc_connectivity_state *current,
                               grpc_closure *notify,
                               grpc_call_list *call_list) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  grpc_connectivity_state_notify_on_state_change(&p->state_tracker, current,
                                                 notify, call_list);
  gpr_mu_unlock(&p->mu);
}

static const grpc_lb_policy_vtable pick_first_lb_policy_vtable = {
    pf_destroy,
    pf_shutdown,
    pf_pick,
    pf_exit_idle,
    pf_broadcast,
    pf_check_connectivity,
    pf_notify_on_state_change};

static void pick_first_factory_ref(grpc_lb_policy_factory *factory) {}

static void pick_first_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *create_pick_first(grpc_lb_policy_factory *factory,
                                         grpc_lb_policy_args *args) {
  pick_first_lb_policy *p = gpr_malloc(sizeof(*p));
  GPR_ASSERT(args->num_subchannels > 0);
  memset(p, 0, sizeof(*p));
  grpc_lb_policy_init(&p->base, &pick_first_lb_policy_vtable);
  p->subchannels = gpr_malloc(sizeof(grpc_subchannel *) * args->num_subchannels);
  p->num_subchannels = args->num_subchannels;
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE,
                               "pick_first");
  memcpy(p->subchannels, args->subchannels,
         sizeof(grpc_subchannel *) * args->num_subchannels);
  grpc_closure_init(&p->connectivity_changed, pf_connectivity_changed, p);
  gpr_mu_init(&p->mu);
  return &p->base;
}

static const grpc_lb_policy_factory_vtable pick_first_factory_vtable = {
    pick_first_factory_ref, pick_first_factory_unref, create_pick_first,
    "pick_first"};

static grpc_lb_policy_factory pick_first_lb_policy_factory = {
    &pick_first_factory_vtable};

grpc_lb_policy_factory *grpc_pick_first_lb_factory_create() {
  return &pick_first_lb_policy_factory;
}
