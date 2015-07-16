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

#include "src/core/client_config/lb_policies/pick_first.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include "src/core/transport/connectivity_state.h"

typedef struct pending_pick {
  struct pending_pick *next;
  grpc_pollset *pollset;
  grpc_subchannel **target;
  grpc_iomgr_closure *on_complete;
} pending_pick;

typedef struct {
  /** base policy: must be first */
  grpc_lb_policy base;
  /** all our subchannels */
  grpc_subchannel **subchannels;
  size_t num_subchannels;

  grpc_iomgr_closure connectivity_changed;

  /** mutex protecting remaining members */
  gpr_mu mu;
  /** the selected channel
      TODO(ctiller): this should be atomically set so we don't
                     need to take a mutex in the common case */
  grpc_subchannel *selected;
  /** have we started picking? */
  int started_picking;
  /** which subchannel are we watching? */
  size_t checking_subchannel;
  /** what is the connectivity of that channel? */
  grpc_connectivity_state checking_connectivity;
  /** list of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;
} pick_first_lb_policy;

static void del_interested_parties_locked(pick_first_lb_policy *p) {
  pending_pick *pp;
  for (pp = p->pending_picks; pp; pp = pp->next) {
    grpc_subchannel_del_interested_party(p->subchannels[p->checking_subchannel],
                                         pp->pollset);
  }
}

static void add_interested_parties_locked(pick_first_lb_policy *p) {
  pending_pick *pp;
  for (pp = p->pending_picks; pp; pp = pp->next) {
    grpc_subchannel_add_interested_party(p->subchannels[p->checking_subchannel],
                                         pp->pollset);
  }
}

void pf_destroy(grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  size_t i;
  del_interested_parties_locked(p);
  for (i = 0; i < p->num_subchannels; i++) {
    GRPC_SUBCHANNEL_UNREF(p->subchannels[i], "pick_first");
  }
  grpc_connectivity_state_destroy(&p->state_tracker);
  gpr_free(p->subchannels);
  gpr_mu_destroy(&p->mu);
  gpr_free(p);
}

void pf_shutdown(grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  gpr_mu_lock(&p->mu);
  del_interested_parties_locked(p);
  while ((pp = p->pending_picks)) {
    p->pending_picks = pp->next;
    *pp->target = NULL;
    grpc_iomgr_add_delayed_callback(pp->on_complete, 0);
    gpr_free(pp);
  }
  gpr_mu_unlock(&p->mu);
}

static void start_picking(pick_first_lb_policy *p) {
  p->started_picking = 1;
  p->checking_subchannel = 0;
  p->checking_connectivity = GRPC_CHANNEL_IDLE;
  GRPC_LB_POLICY_REF(&p->base, "pick_first_connectivity");
  grpc_subchannel_notify_on_state_change(p->subchannels[p->checking_subchannel],
                                         &p->checking_connectivity,
                                         &p->connectivity_changed);
}

void pf_exit_idle(grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  if (!p->started_picking) {
    start_picking(p);
  }
  gpr_mu_unlock(&p->mu);
}

void pf_pick(grpc_lb_policy *pol, grpc_pollset *pollset,
             grpc_metadata_batch *initial_metadata, grpc_subchannel **target,
             grpc_iomgr_closure *on_complete) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  pending_pick *pp;
  gpr_mu_lock(&p->mu);
  if (p->selected) {
    gpr_mu_unlock(&p->mu);
    *target = p->selected;
    on_complete->cb(on_complete->cb_arg, 1);
  } else {
    if (!p->started_picking) {
      start_picking(p);
    }
    grpc_subchannel_add_interested_party(p->subchannels[p->checking_subchannel],
                                         pollset);
    pp = gpr_malloc(sizeof(*pp));
    pp->next = p->pending_picks;
    pp->pollset = pollset;
    pp->target = target;
    pp->on_complete = on_complete;
    p->pending_picks = pp;
    gpr_mu_unlock(&p->mu);
  }
}

static void pf_connectivity_changed(void *arg, int iomgr_success) {
  pick_first_lb_policy *p = arg;
  pending_pick *pp;
  int unref = 0;

  gpr_mu_lock(&p->mu);

  if (p->selected != NULL) {
    grpc_connectivity_state_set(&p->state_tracker, p->checking_connectivity);
    if (p->checking_connectivity != GRPC_CHANNEL_FATAL_FAILURE) {
      grpc_subchannel_notify_on_state_change(p->selected, &p->checking_connectivity, &p->connectivity_changed);
    } else {
      unref = 1;
    }
  } else {
loop:
    switch (p->checking_connectivity) {
      case GRPC_CHANNEL_READY:
        grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_READY);
        p->selected = p->subchannels[p->checking_subchannel];
        while ((pp = p->pending_picks)) {
          p->pending_picks = pp->next;
          *pp->target = p->selected;
          grpc_subchannel_del_interested_party(p->selected, pp->pollset);
          grpc_iomgr_add_delayed_callback(pp->on_complete, 1);
          gpr_free(pp);
        }
        grpc_subchannel_notify_on_state_change(p->selected, &p->checking_connectivity, &p->connectivity_changed);
        break;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE);
        del_interested_parties_locked(p);
        p->checking_subchannel =
            (p->checking_subchannel + 1) % p->num_subchannels;
        p->checking_connectivity = grpc_subchannel_check_connectivity(
            p->subchannels[p->checking_subchannel]);
        add_interested_parties_locked(p);
        if (p->checking_connectivity == GRPC_CHANNEL_TRANSIENT_FAILURE) {
          grpc_subchannel_notify_on_state_change(p->subchannels[p->checking_subchannel], &p->checking_connectivity, &p->connectivity_changed);
        } else {
          goto loop;
        }
        break;
      case GRPC_CHANNEL_CONNECTING:
      case GRPC_CHANNEL_IDLE:
        grpc_connectivity_state_set(&p->state_tracker, p->checking_connectivity);
        grpc_subchannel_notify_on_state_change(
            p->subchannels[p->checking_subchannel], &p->checking_connectivity,
            &p->connectivity_changed);
        break;
      case GRPC_CHANNEL_FATAL_FAILURE:
        del_interested_parties_locked(p);
        GPR_SWAP(grpc_subchannel *, p->subchannels[p->checking_subchannel],
                 p->subchannels[p->num_subchannels - 1]);
        p->num_subchannels--;
        GRPC_SUBCHANNEL_UNREF(p->subchannels[p->num_subchannels], "pick_first");
        if (p->num_subchannels == 0) {
          grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_FATAL_FAILURE);
          while ((pp = p->pending_picks)) {
            p->pending_picks = pp->next;
            *pp->target = NULL;
            grpc_iomgr_add_delayed_callback(pp->on_complete, 1);
            gpr_free(pp);
          }
          unref = 1;
        } else {
          grpc_connectivity_state_set(&p->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE);
          p->checking_subchannel %= p->num_subchannels;
          p->checking_connectivity = grpc_subchannel_check_connectivity(
              p->subchannels[p->checking_subchannel]);
          add_interested_parties_locked(p);
          goto loop;
        }
    }
  }

  gpr_mu_unlock(&p->mu);

  if (unref) {
    GRPC_LB_POLICY_UNREF(&p->base, "pick_first_connectivity");
  }
}

static void pf_broadcast(grpc_lb_policy *pol, grpc_transport_op *op) {
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
    grpc_subchannel_process_transport_op(subchannels[i], op);
    GRPC_SUBCHANNEL_UNREF(subchannels[i], "pf_broadcast");
  }
  gpr_free(subchannels);
}

static grpc_connectivity_state pf_check_connectivity(grpc_lb_policy *pol) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  grpc_connectivity_state st;
  gpr_mu_lock(&p->mu);
  st = grpc_connectivity_state_check(&p->state_tracker);
  gpr_mu_unlock(&p->mu);
  return st;
}

static void pf_notify_on_state_change(grpc_lb_policy *pol,
                                      grpc_connectivity_state *current,
                                      grpc_iomgr_closure *notify) {
  pick_first_lb_policy *p = (pick_first_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  grpc_connectivity_state_notify_on_state_change(&p->state_tracker, current,
                                                 notify);
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

grpc_lb_policy *grpc_create_pick_first_lb_policy(grpc_subchannel **subchannels,
                                                 size_t num_subchannels) {
  pick_first_lb_policy *p = gpr_malloc(sizeof(*p));
  GPR_ASSERT(num_subchannels);
  memset(p, 0, sizeof(*p));
  grpc_lb_policy_init(&p->base, &pick_first_lb_policy_vtable);
  p->subchannels = gpr_malloc(sizeof(grpc_subchannel *) * num_subchannels);
  p->num_subchannels = num_subchannels;
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE, "pick_first");
  memcpy(p->subchannels, subchannels,
         sizeof(grpc_subchannel *) * num_subchannels);
  grpc_iomgr_closure_init(&p->connectivity_changed, pf_connectivity_changed, p);
  gpr_mu_init(&p->mu);
  return &p->base;
}
