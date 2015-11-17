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

#include "src/core/client_config/lb_policies/round_robin.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include "src/core/transport/connectivity_state.h"

int grpc_lb_round_robin_trace = 0;

/** List of entities waiting for a pick.
 *
 * Once a pick is available, \a target is updated and \a on_complete called. */
typedef struct pending_pick {
  struct pending_pick *next;
  grpc_pollset *pollset;
  grpc_connected_subchannel **target;
  grpc_closure *on_complete;
} pending_pick;

/** List of subchannels in a connectivity READY state */
typedef struct ready_list {
  grpc_subchannel *subchannel;
  struct ready_list *next;
  struct ready_list *prev;
} ready_list;

typedef struct {
  size_t subchannel_idx; /**< Index over p->subchannels */
  void *p;               /**< round_robin_lb_policy instance */
} connectivity_changed_cb_arg;

typedef struct {
  /** base policy: must be first */
  grpc_lb_policy base;

  /** all our subchannels */
  grpc_subchannel **subchannels;
  size_t num_subchannels;

  /** Callbacks, one per subchannel being watched, to be called when their
   * respective connectivity changes */
  grpc_closure *connectivity_changed_cbs;
  connectivity_changed_cb_arg *cb_args;

  /** mutex protecting remaining members */
  gpr_mu mu;
  /** have we started picking? */
  int started_picking;
  /** are we shutting down? */
  int shutdown;
  /** Connectivity state of the subchannels being watched */
  grpc_connectivity_state *subchannel_connectivity;
  /** List of picks that are waiting on connectivity */
  pending_pick *pending_picks;

  /** our connectivity state tracker */
  grpc_connectivity_state_tracker state_tracker;

  /** (Dummy) root of the doubly linked list containing READY subchannels */
  ready_list ready_list;
  /** Last pick from the ready list. */
  ready_list *ready_list_last_pick;

  /** Subchannel index to ready_list node.
   *
   * Kept in order to remove nodes from the ready list associated with a
   * subchannel */
  ready_list **subchannel_index_to_readylist_node;
} round_robin_lb_policy;

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
    gpr_log(GPR_DEBUG, "[READYLIST] ADVANCED LAST PICK. NOW AT NODE %p (SC %p)",
            p->ready_list_last_pick, p->ready_list_last_pick->subchannel);
  }
}

/** Prepends (relative to the root at p->ready_list) the connected subchannel \a
 * csc to the list of ready subchannels. */
static ready_list *add_connected_sc_locked(round_robin_lb_policy *p,
                                           grpc_subchannel *sc) {
  ready_list *new_elem = gpr_malloc(sizeof(ready_list));
  new_elem->subchannel = sc;
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
    gpr_log(GPR_DEBUG, "[READYLIST] ADDING NODE %p (SC %p)", new_elem, sc);
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
    /* If removing the lastly picked node, reset the last pick pointer to the
     * dummy root of the list */
    p->ready_list_last_pick = &p->ready_list;
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
    gpr_log(GPR_DEBUG, "[READYLIST] REMOVED NODE %p (SC %p)", node,
            node->subchannel);
  }

  node->next = NULL;
  node->prev = NULL;
  node->subchannel = NULL;

  gpr_free(node);
}

static void del_interested_parties_locked(grpc_exec_ctx *exec_ctx,
                                          round_robin_lb_policy *p,
                                          const size_t subchannel_idx) {
  pending_pick *pp;
  for (pp = p->pending_picks; pp; pp = pp->next) {
    grpc_subchannel_del_interested_party(
        exec_ctx, p->subchannels[subchannel_idx], pp->pollset);
  }
}

void rr_destroy(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  size_t i;
  ready_list *elem;
  for (i = 0; i < p->num_subchannels; i++) {
    del_interested_parties_locked(exec_ctx, p, i);
  }
  for (i = 0; i < p->num_subchannels; i++) {
    GRPC_SUBCHANNEL_UNREF(exec_ctx, p->subchannels[i], "round_robin");
  }
  gpr_free(p->connectivity_changed_cbs);
  gpr_free(p->subchannel_connectivity);

  grpc_connectivity_state_destroy(exec_ctx, &p->state_tracker);
  gpr_free(p->subchannels);
  gpr_mu_destroy(&p->mu);

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
  gpr_free(p->subchannel_index_to_readylist_node);
  gpr_free(p->cb_args);
  gpr_free(p);
}

void rr_shutdown(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  size_t i;
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp;
  gpr_mu_lock(&p->mu);

  for (i = 0; i < p->num_subchannels; i++) {
    del_interested_parties_locked(exec_ctx, p, i);
  }

  p->shutdown = 1;
  while ((pp = p->pending_picks)) {
    p->pending_picks = pp->next;
    *pp->target = NULL;
    grpc_exec_ctx_enqueue(exec_ctx, pp->on_complete, 0);
    gpr_free(pp);
  }
  grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                              GRPC_CHANNEL_FATAL_FAILURE, "shutdown");
  gpr_mu_unlock(&p->mu);
}

static void rr_cancel_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                           grpc_connected_subchannel **target) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp;
  size_t i;
  gpr_mu_lock(&p->mu);
  pp = p->pending_picks;
  p->pending_picks = NULL;
  while (pp != NULL) {
    pending_pick *next = pp->next;
    if (pp->target == target) {
      for (i = 0; i < p->num_subchannels; i++) {
        grpc_subchannel_add_interested_party(exec_ctx, p->subchannels[i],
                                             pp->pollset);
      }
      *target = NULL;
      grpc_exec_ctx_enqueue(exec_ctx, pp->on_complete, 0);
      gpr_free(pp);
    } else {
      pp->next = p->pending_picks;
      p->pending_picks = pp;
    }
    pp = next;
  }
  gpr_mu_unlock(&p->mu);
}

static void start_picking(grpc_exec_ctx *exec_ctx, round_robin_lb_policy *p) {
  size_t i;
  p->started_picking = 1;

  for (i = 0; i < p->num_subchannels; i++) {
    p->subchannel_connectivity[i] = GRPC_CHANNEL_IDLE;
    grpc_subchannel_notify_on_state_change(exec_ctx, p->subchannels[i],
                                           &p->subchannel_connectivity[i],
                                           &p->connectivity_changed_cbs[i]);
    GRPC_LB_POLICY_REF(&p->base, "round_robin_connectivity");
  }
}

void rr_exit_idle(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  if (!p->started_picking) {
    start_picking(exec_ctx, p);
  }
  gpr_mu_unlock(&p->mu);
}

int rr_pick(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol, grpc_pollset *pollset,
            grpc_metadata_batch *initial_metadata, grpc_connected_subchannel **target,
            grpc_closure *on_complete) {
  size_t i;
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  pending_pick *pp;
  ready_list *selected;
  gpr_mu_lock(&p->mu);
  if ((selected = peek_next_connected_locked(p))) {
    gpr_mu_unlock(&p->mu);
    *target = grpc_subchannel_get_connected_subchannel(selected->subchannel);
    if (grpc_lb_round_robin_trace) {
      gpr_log(GPR_DEBUG, "[RR PICK] TARGET <-- CONNECTED SUBCHANNEL %p (NODE %p)",
              selected->subchannel, selected);
    }
    /* only advance the last picked pointer if the selection was used */
    advance_last_picked_locked(p);
    return 1;
  } else {
    if (!p->started_picking) {
      start_picking(exec_ctx, p);
    }
    for (i = 0; i < p->num_subchannels; i++) {
      grpc_subchannel_add_interested_party(exec_ctx, p->subchannels[i],
                                           pollset);
    }
    pp = gpr_malloc(sizeof(*pp));
    pp->next = p->pending_picks;
    pp->pollset = pollset;
    pp->target = target;
    pp->on_complete = on_complete;
    p->pending_picks = pp;
    gpr_mu_unlock(&p->mu);
    return 0;
  }
}

static void rr_connectivity_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                    int iomgr_success) {
  connectivity_changed_cb_arg *cb_arg = arg;
  round_robin_lb_policy *p = cb_arg->p;
  /* index over p->subchannels of this cb's subchannel */
  const size_t this_idx = cb_arg->subchannel_idx;
  pending_pick *pp;
  ready_list *selected;

  int unref = 0;

  /* connectivity state of this cb's subchannel */
  grpc_connectivity_state *this_connectivity;

  gpr_mu_lock(&p->mu);

  this_connectivity = &p->subchannel_connectivity[this_idx];

  if (p->shutdown) {
    unref = 1;
  } else {
    switch (*this_connectivity) {
      case GRPC_CHANNEL_READY:
        grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                    GRPC_CHANNEL_READY, "connecting_ready");
        /* add the newly connected subchannel to the list of connected ones.
         * Note that it goes to the "end of the line". */
        p->subchannel_index_to_readylist_node[this_idx] =
            add_connected_sc_locked(p, p->subchannels[this_idx]);
        /* at this point we know there's at least one suitable subchannel. Go
         * ahead and pick one and notify the pending suitors in
         * p->pending_picks. This preemtively replicates rr_pick()'s actions. */
        selected = peek_next_connected_locked(p);
        if (p->pending_picks != NULL) {
          /* if the selected subchannel is going to be used for the pending
           * picks, update the last picked pointer */
          advance_last_picked_locked(p);
        }
        while ((pp = p->pending_picks)) {
          p->pending_picks = pp->next;
          *pp->target = grpc_subchannel_get_connected_subchannel(selected->subchannel);
          if (grpc_lb_round_robin_trace) {
            gpr_log(GPR_DEBUG,
                    "[RR CONN CHANGED] TARGET <-- SUBCHANNEL %p (NODE %p)",
                    selected->subchannel, selected);
          }
          grpc_subchannel_del_interested_party(exec_ctx, selected->subchannel,
                                               pp->pollset);
          grpc_exec_ctx_enqueue(exec_ctx, pp->on_complete, 1);
          gpr_free(pp);
        }
        grpc_subchannel_notify_on_state_change(
            exec_ctx, p->subchannels[this_idx], this_connectivity,
            &p->connectivity_changed_cbs[this_idx]);
        break;
      case GRPC_CHANNEL_CONNECTING:
      case GRPC_CHANNEL_IDLE:
        grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                    *this_connectivity, "connecting_changed");
        grpc_subchannel_notify_on_state_change(
            exec_ctx, p->subchannels[this_idx], this_connectivity,
            &p->connectivity_changed_cbs[this_idx]);
        break;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        del_interested_parties_locked(exec_ctx, p, this_idx);
        /* renew state notification */
        grpc_subchannel_notify_on_state_change(
            exec_ctx, p->subchannels[this_idx], this_connectivity,
            &p->connectivity_changed_cbs[this_idx]);

        /* remove from ready list if still present */
        if (p->subchannel_index_to_readylist_node[this_idx] != NULL) {
          remove_disconnected_sc_locked(
              p, p->subchannel_index_to_readylist_node[this_idx]);
          p->subchannel_index_to_readylist_node[this_idx] = NULL;
        }
        grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                    GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    "connecting_transient_failure");
        break;
      case GRPC_CHANNEL_FATAL_FAILURE:
        del_interested_parties_locked(exec_ctx, p, this_idx);
        if (p->subchannel_index_to_readylist_node[this_idx] != NULL) {
          remove_disconnected_sc_locked(
              p, p->subchannel_index_to_readylist_node[this_idx]);
          p->subchannel_index_to_readylist_node[this_idx] = NULL;
        }

        GPR_SWAP(grpc_subchannel *, p->subchannels[this_idx],
                 p->subchannels[p->num_subchannels - 1]);
        p->num_subchannels--;
        GRPC_SUBCHANNEL_UNREF(exec_ctx, p->subchannels[p->num_subchannels],
                              "round_robin");

        if (p->num_subchannels == 0) {
          grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                      GRPC_CHANNEL_FATAL_FAILURE,
                                      "no_more_channels");
          while ((pp = p->pending_picks)) {
            p->pending_picks = pp->next;
            *pp->target = NULL;
            grpc_exec_ctx_enqueue(exec_ctx, pp->on_complete, 1);
            gpr_free(pp);
          }
          unref = 1;
        } else {
          grpc_connectivity_state_set(exec_ctx, &p->state_tracker,
                                      GRPC_CHANNEL_TRANSIENT_FAILURE,
                                      "subchannel_failed");
        }
    } /* switch */
  }   /* !unref */

  gpr_mu_unlock(&p->mu);

  if (unref) {
    GRPC_LB_POLICY_UNREF(exec_ctx, &p->base, "round_robin_connectivity");
  }
}

static void rr_broadcast(grpc_exec_ctx *exec_ctx, grpc_lb_policy *pol,
                         grpc_transport_op *op) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  size_t i;
  size_t n;
  grpc_subchannel **subchannels;

  gpr_mu_lock(&p->mu);
  n = p->num_subchannels;
  subchannels = gpr_malloc(n * sizeof(*subchannels));
  for (i = 0; i < n; i++) {
    subchannels[i] = p->subchannels[i];
    GRPC_SUBCHANNEL_REF(subchannels[i], "rr_broadcast");
  }
  gpr_mu_unlock(&p->mu);

  for (i = 0; i < n; i++) {
    grpc_subchannel_process_transport_op(exec_ctx, subchannels[i], op);
    GRPC_SUBCHANNEL_UNREF(exec_ctx, subchannels[i], "rr_broadcast");
  }
  gpr_free(subchannels);
}

static grpc_connectivity_state rr_check_connectivity(grpc_exec_ctx *exec_ctx,
                                                     grpc_lb_policy *pol) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  grpc_connectivity_state st;
  gpr_mu_lock(&p->mu);
  st = grpc_connectivity_state_check(&p->state_tracker);
  gpr_mu_unlock(&p->mu);
  return st;
}

static void rr_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                      grpc_lb_policy *pol,
                                      grpc_connectivity_state *current,
                                      grpc_closure *notify) {
  round_robin_lb_policy *p = (round_robin_lb_policy *)pol;
  gpr_mu_lock(&p->mu);
  grpc_connectivity_state_notify_on_state_change(exec_ctx, &p->state_tracker,
                                                 current, notify);
  gpr_mu_unlock(&p->mu);
}

static const grpc_lb_policy_vtable round_robin_lb_policy_vtable = {
    rr_destroy, rr_shutdown, rr_pick, rr_cancel_pick, rr_exit_idle,
    rr_broadcast, rr_check_connectivity, rr_notify_on_state_change};

static void round_robin_factory_ref(grpc_lb_policy_factory *factory) {}

static void round_robin_factory_unref(grpc_lb_policy_factory *factory) {}

static grpc_lb_policy *create_round_robin(grpc_lb_policy_factory *factory,
                                          grpc_lb_policy_args *args) {
  size_t i;
  round_robin_lb_policy *p = gpr_malloc(sizeof(*p));
  GPR_ASSERT(args->num_subchannels > 0);
  memset(p, 0, sizeof(*p));
  grpc_lb_policy_init(&p->base, &round_robin_lb_policy_vtable);
  p->subchannels =
      gpr_malloc(sizeof(grpc_subchannel *) * args->num_subchannels);
  p->num_subchannels = args->num_subchannels;
  grpc_connectivity_state_init(&p->state_tracker, GRPC_CHANNEL_IDLE,
                               "round_robin");
  memcpy(p->subchannels, args->subchannels,
         sizeof(grpc_subchannel *) * args->num_subchannels);

  gpr_mu_init(&p->mu);
  p->connectivity_changed_cbs =
      gpr_malloc(sizeof(grpc_closure) * args->num_subchannels);
  p->subchannel_connectivity =
      gpr_malloc(sizeof(grpc_connectivity_state) * args->num_subchannels);

  p->cb_args =
      gpr_malloc(sizeof(connectivity_changed_cb_arg) * args->num_subchannels);
  for (i = 0; i < args->num_subchannels; i++) {
    p->cb_args[i].subchannel_idx = i;
    p->cb_args[i].p = p;
    grpc_closure_init(&p->connectivity_changed_cbs[i], rr_connectivity_changed,
                      &p->cb_args[i]);
  }

  /* The (dummy node) root of the ready list */
  p->ready_list.subchannel = NULL;
  p->ready_list.prev = NULL;
  p->ready_list.next = NULL;
  p->ready_list_last_pick = &p->ready_list;

  p->subchannel_index_to_readylist_node =
      gpr_malloc(sizeof(grpc_subchannel *) * args->num_subchannels);
  memset(p->subchannel_index_to_readylist_node, 0,
         sizeof(grpc_subchannel *) * args->num_subchannels);
  return &p->base;
}

static const grpc_lb_policy_factory_vtable round_robin_factory_vtable = {
    round_robin_factory_ref, round_robin_factory_unref, create_round_robin,
    "round_robin"};

static grpc_lb_policy_factory round_robin_lb_policy_factory = {
    &round_robin_factory_vtable};

grpc_lb_policy_factory *grpc_round_robin_lb_factory_create() {
  return &round_robin_lb_policy_factory;
}
