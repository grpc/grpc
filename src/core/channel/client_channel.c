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

#include "src/core/channel/client_channel.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

#include "src/core/channel/channel_args.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/channel/subchannel_call_holder.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/profiling/timers.h"
#include "src/core/support/string.h"
#include "src/core/surface/channel.h"
#include "src/core/transport/connectivity_state.h"

/* Client channel implementation */

typedef grpc_subchannel_call_holder call_data;

typedef struct client_channel_channel_data {
  /** metadata context for this channel */
  grpc_mdctx *mdctx;
  /** resolver for this channel */
  grpc_resolver *resolver;
  /** have we started resolving this channel */
  int started_resolving;
  /** master channel - the grpc_channel instance that ultimately owns
      this channel_data via its channel stack.
      We occasionally use this to bump the refcount on the master channel
      to keep ourselves alive through an asynchronous operation. */
  grpc_channel *master;

  /** mutex protecting client configuration, including all
      variables below in this data structure */
  gpr_mu mu_config;
  /** currently active load balancer - guarded by mu_config */
  grpc_lb_policy *lb_policy;
  /** incoming configuration - set by resolver.next
      guarded by mu_config */
  grpc_client_config *incoming_configuration;
  /** a list of closures that are all waiting for config to come in */
  grpc_closure_list waiting_for_config_closures;
  /** resolver callback */
  grpc_closure on_config_changed;
  /** connectivity state being tracked */
  grpc_connectivity_state_tracker state_tracker;
  /** when an lb_policy arrives, should we try to exit idle */
  int exit_idle_when_lb_policy_arrives;
  /** pollset_set of interested parties in a new connection */
  grpc_pollset_set pollset_set;
} channel_data;

/** We create one watcher for each new lb_policy that is returned from a
   resolver,
    to watch for state changes from the lb_policy. When a state change is seen,
   we
    update the channel, and create a new watcher */
typedef struct {
  channel_data *chand;
  grpc_closure on_changed;
  grpc_connectivity_state state;
  grpc_lb_policy *lb_policy;
} lb_policy_connectivity_watcher;

typedef struct {
  grpc_closure closure;
  grpc_call_element *elem;
} waiting_call;

static char *cc_get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  return grpc_subchannel_call_holder_get_peer(exec_ctx, elem->call_data,
                                              chand->master);
}

static void cc_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         grpc_transport_stream_op *op) {
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);
  grpc_subchannel_call_holder_perform_op(exec_ctx, elem->call_data, op);
}

static void watch_lb_policy(grpc_exec_ctx *exec_ctx, channel_data *chand,
                            grpc_lb_policy *lb_policy,
                            grpc_connectivity_state current_state);

static void on_lb_policy_state_changed_locked(
    grpc_exec_ctx *exec_ctx, lb_policy_connectivity_watcher *w) {
  /* check if the notification is for a stale policy */
  if (w->lb_policy != w->chand->lb_policy) return;

  grpc_connectivity_state_set(exec_ctx, &w->chand->state_tracker, w->state,
                              "lb_changed");
  if (w->state != GRPC_CHANNEL_FATAL_FAILURE) {
    watch_lb_policy(exec_ctx, w->chand, w->lb_policy, w->state);
  }
}

static void on_lb_policy_state_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                       int iomgr_success) {
  lb_policy_connectivity_watcher *w = arg;

  gpr_mu_lock(&w->chand->mu_config);
  on_lb_policy_state_changed_locked(exec_ctx, w);
  gpr_mu_unlock(&w->chand->mu_config);

  GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, w->chand->master, "watch_lb_policy");
  gpr_free(w);
}

static void watch_lb_policy(grpc_exec_ctx *exec_ctx, channel_data *chand,
                            grpc_lb_policy *lb_policy,
                            grpc_connectivity_state current_state) {
  lb_policy_connectivity_watcher *w = gpr_malloc(sizeof(*w));
  GRPC_CHANNEL_INTERNAL_REF(chand->master, "watch_lb_policy");

  w->chand = chand;
  grpc_closure_init(&w->on_changed, on_lb_policy_state_changed, w);
  w->state = current_state;
  w->lb_policy = lb_policy;
  grpc_lb_policy_notify_on_state_change(exec_ctx, lb_policy, &w->state,
                                        &w->on_changed);
}

static void cc_on_config_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                 int iomgr_success) {
  channel_data *chand = arg;
  grpc_lb_policy *lb_policy = NULL;
  grpc_lb_policy *old_lb_policy;
  grpc_resolver *old_resolver;
  grpc_connectivity_state state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  int exit_idle = 0;

  if (chand->incoming_configuration != NULL) {
    lb_policy = grpc_client_config_get_lb_policy(chand->incoming_configuration);
    if (lb_policy != NULL) {
      GRPC_LB_POLICY_REF(lb_policy, "channel");
      GRPC_LB_POLICY_REF(lb_policy, "config_change");
      state = grpc_lb_policy_check_connectivity(exec_ctx, lb_policy);
    }

    grpc_client_config_unref(exec_ctx, chand->incoming_configuration);
  }

  chand->incoming_configuration = NULL;

  gpr_mu_lock(&chand->mu_config);
  old_lb_policy = chand->lb_policy;
  chand->lb_policy = lb_policy;
  if (lb_policy != NULL || chand->resolver == NULL /* disconnected */) {
    grpc_exec_ctx_enqueue_list(exec_ctx, &chand->waiting_for_config_closures);
  }
  if (lb_policy != NULL && chand->exit_idle_when_lb_policy_arrives) {
    GRPC_LB_POLICY_REF(lb_policy, "exit_idle");
    exit_idle = 1;
    chand->exit_idle_when_lb_policy_arrives = 0;
  }

  if (iomgr_success && chand->resolver) {
    grpc_resolver *resolver = chand->resolver;
    GRPC_RESOLVER_REF(resolver, "channel-next");
    grpc_connectivity_state_set(exec_ctx, &chand->state_tracker, state,
                                "new_lb+resolver");
    if (lb_policy != NULL) {
      watch_lb_policy(exec_ctx, chand, lb_policy, state);
    }
    gpr_mu_unlock(&chand->mu_config);
    GRPC_CHANNEL_INTERNAL_REF(chand->master, "resolver");
    grpc_resolver_next(exec_ctx, resolver, &chand->incoming_configuration,
                       &chand->on_config_changed);
    GRPC_RESOLVER_UNREF(exec_ctx, resolver, "channel-next");
  } else {
    old_resolver = chand->resolver;
    chand->resolver = NULL;
    grpc_connectivity_state_set(exec_ctx, &chand->state_tracker,
                                GRPC_CHANNEL_FATAL_FAILURE, "resolver_gone");
    gpr_mu_unlock(&chand->mu_config);
    if (old_resolver != NULL) {
      grpc_resolver_shutdown(exec_ctx, old_resolver);
      GRPC_RESOLVER_UNREF(exec_ctx, old_resolver, "channel");
    }
  }

  if (exit_idle) {
    grpc_lb_policy_exit_idle(exec_ctx, lb_policy);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "exit_idle");
  }

  if (old_lb_policy != NULL) {
    grpc_lb_policy_shutdown(exec_ctx, old_lb_policy);
    GRPC_LB_POLICY_UNREF(exec_ctx, old_lb_policy, "channel");
  }

  if (lb_policy != NULL) {
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "config_change");
  }

  GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, chand->master, "resolver");
}

static void cc_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_channel_element *elem,
                                  grpc_transport_op *op) {
  grpc_lb_policy *lb_policy = NULL;
  channel_data *chand = elem->channel_data;
  grpc_resolver *destroy_resolver = NULL;

  grpc_exec_ctx_enqueue(exec_ctx, op->on_consumed, 1);

  GPR_ASSERT(op->set_accept_stream == NULL);
  GPR_ASSERT(op->bind_pollset == NULL);

  gpr_mu_lock(&chand->mu_config);
  if (op->on_connectivity_state_change != NULL) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &chand->state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
    op->on_connectivity_state_change = NULL;
    op->connectivity_state = NULL;
  }

  lb_policy = chand->lb_policy;
  if (lb_policy) {
    GRPC_LB_POLICY_REF(lb_policy, "broadcast");
  }

  if (op->disconnect && chand->resolver != NULL) {
    grpc_connectivity_state_set(exec_ctx, &chand->state_tracker,
                                GRPC_CHANNEL_FATAL_FAILURE, "disconnect");
    destroy_resolver = chand->resolver;
    chand->resolver = NULL;
    if (chand->lb_policy != NULL) {
      grpc_lb_policy_shutdown(exec_ctx, chand->lb_policy);
      GRPC_LB_POLICY_UNREF(exec_ctx, chand->lb_policy, "channel");
      chand->lb_policy = NULL;
    }
  }
  gpr_mu_unlock(&chand->mu_config);

  if (destroy_resolver) {
    grpc_resolver_shutdown(exec_ctx, destroy_resolver);
    GRPC_RESOLVER_UNREF(exec_ctx, destroy_resolver, "channel");
  }

  if (lb_policy) {
    grpc_lb_policy_broadcast(exec_ctx, lb_policy, op);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "broadcast");
  }
}

typedef struct {
  grpc_metadata_batch *initial_metadata;
  grpc_connected_subchannel **connected_subchannel;
  grpc_closure *on_ready;
  grpc_call_element *elem;
  grpc_closure closure;
} continue_picking_args;

static int cc_pick_subchannel(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_metadata_batch *initial_metadata,
                              grpc_connected_subchannel **connected_subchannel,
                              grpc_closure *on_ready);

static void continue_picking(grpc_exec_ctx *exec_ctx, void *arg, int success) {
  continue_picking_args *cpa = arg;
  if (!success) {
    grpc_exec_ctx_enqueue(exec_ctx, cpa->on_ready, 0);
  } else if (cpa->connected_subchannel == NULL) {
    /* cancelled, do nothing */
  } else if (cc_pick_subchannel(exec_ctx, cpa->elem, cpa->initial_metadata,
                                cpa->connected_subchannel, cpa->on_ready)) {
    grpc_exec_ctx_enqueue(exec_ctx, cpa->on_ready, 1);
  }
  gpr_free(cpa);
}

static int cc_pick_subchannel(grpc_exec_ctx *exec_ctx, void *elemp,
                              grpc_metadata_batch *initial_metadata,
                              grpc_connected_subchannel **connected_subchannel,
                              grpc_closure *on_ready) {
  grpc_call_element *elem = elemp;
  channel_data *chand = elem->channel_data;
  call_data *calld = elem->call_data;
  continue_picking_args *cpa;
  grpc_closure *closure;

  GPR_ASSERT(connected_subchannel);

  gpr_mu_lock(&chand->mu_config);
  if (initial_metadata == NULL) {
    if (chand->lb_policy != NULL) {
      grpc_lb_policy_cancel_pick(exec_ctx, chand->lb_policy, connected_subchannel);
    }
    for (closure = chand->waiting_for_config_closures.head; closure != NULL;
         closure = grpc_closure_next(closure)) {
      cpa = closure->cb_arg;
      if (cpa->connected_subchannel == connected_subchannel) {
        cpa->connected_subchannel = NULL;
        grpc_exec_ctx_enqueue(exec_ctx, cpa->on_ready, 0);
      }
    }
    gpr_mu_unlock(&chand->mu_config);
    return 1;
  }
  if (chand->lb_policy != NULL) {
    int r = grpc_lb_policy_pick(exec_ctx, chand->lb_policy, calld->pollset,
                                initial_metadata, connected_subchannel, on_ready);
    gpr_mu_unlock(&chand->mu_config);
    return r;
  }
  if (chand->resolver != NULL && !chand->started_resolving) {
    chand->started_resolving = 1;
    GRPC_CHANNEL_INTERNAL_REF(chand->master, "resolver");
    grpc_resolver_next(exec_ctx, chand->resolver,
                       &chand->incoming_configuration,
                       &chand->on_config_changed);
  }
  cpa = gpr_malloc(sizeof(*cpa));
  cpa->initial_metadata = initial_metadata;
  cpa->connected_subchannel = connected_subchannel;
  cpa->on_ready = on_ready;
  cpa->elem = elem;
  grpc_closure_init(&cpa->closure, continue_picking, cpa);
  grpc_closure_list_add(&chand->waiting_for_config_closures, &cpa->closure, 1);
  gpr_mu_unlock(&chand->mu_config);
  return 0;
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_call_element_args *args) {
  grpc_subchannel_call_holder_init(elem->call_data, cc_pick_subchannel, elem);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx,
                              grpc_call_element *elem) {
  grpc_subchannel_call_holder_destroy(exec_ctx, elem->call_data);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem,
                              grpc_channel_element_args *args) {
  channel_data *chand = elem->channel_data;

  memset(chand, 0, sizeof(*chand));

  GPR_ASSERT(args->is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  gpr_mu_init(&chand->mu_config);
  chand->mdctx = args->metadata_context;
  chand->master = args->master;
  grpc_pollset_set_init(&chand->pollset_set);
  grpc_closure_init(&chand->on_config_changed, cc_on_config_changed, chand);

  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_channel");
}

/* Destructor for channel_data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;

  if (chand->resolver != NULL) {
    grpc_resolver_shutdown(exec_ctx, chand->resolver);
    GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
  }
  if (chand->lb_policy != NULL) {
    GRPC_LB_POLICY_UNREF(exec_ctx, chand->lb_policy, "channel");
  }
  grpc_connectivity_state_destroy(exec_ctx, &chand->state_tracker);
  grpc_pollset_set_destroy(&chand->pollset_set);
  gpr_mu_destroy(&chand->mu_config);
}

static void cc_set_pollset(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_pollset *pollset) {
  call_data *calld = elem->call_data;
  calld->pollset = pollset;
}

const grpc_channel_filter grpc_client_channel_filter = {
    cc_start_transport_stream_op, cc_start_transport_op, sizeof(call_data),
    init_call_elem, cc_set_pollset, destroy_call_elem, sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, cc_get_peer, "client-channel",
};

void grpc_client_channel_set_resolver(grpc_exec_ctx *exec_ctx,
                                      grpc_channel_stack *channel_stack,
                                      grpc_resolver *resolver) {
  /* post construction initialization: set the transport setup pointer */
  grpc_channel_element *elem = grpc_channel_stack_last_element(channel_stack);
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->mu_config);
  GPR_ASSERT(!chand->resolver);
  chand->resolver = resolver;
  GRPC_RESOLVER_REF(resolver, "channel");
  if (!grpc_closure_list_empty(chand->waiting_for_config_closures) ||
      chand->exit_idle_when_lb_policy_arrives) {
    chand->started_resolving = 1;
    GRPC_CHANNEL_INTERNAL_REF(chand->master, "resolver");
    grpc_resolver_next(exec_ctx, resolver, &chand->incoming_configuration,
                       &chand->on_config_changed);
  }
  gpr_mu_unlock(&chand->mu_config);
}

grpc_connectivity_state grpc_client_channel_check_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, int try_to_connect) {
  channel_data *chand = elem->channel_data;
  grpc_connectivity_state out;
  gpr_mu_lock(&chand->mu_config);
  out = grpc_connectivity_state_check(&chand->state_tracker);
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    if (chand->lb_policy != NULL) {
      grpc_lb_policy_exit_idle(exec_ctx, chand->lb_policy);
    } else {
      chand->exit_idle_when_lb_policy_arrives = 1;
      if (!chand->started_resolving && chand->resolver != NULL) {
        GRPC_CHANNEL_INTERNAL_REF(chand->master, "resolver");
        chand->started_resolving = 1;
        grpc_resolver_next(exec_ctx, chand->resolver,
                           &chand->incoming_configuration,
                           &chand->on_config_changed);
      }
    }
  }
  gpr_mu_unlock(&chand->mu_config);
  return out;
}

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
    grpc_connectivity_state *state, grpc_closure *on_complete) {
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->mu_config);
  grpc_connectivity_state_notify_on_state_change(
      exec_ctx, &chand->state_tracker, state, on_complete);
  gpr_mu_unlock(&chand->mu_config);
}

grpc_pollset_set *grpc_client_channel_get_connecting_pollset_set(
    grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  return &chand->pollset_set;
}

void grpc_client_channel_add_interested_party(grpc_exec_ctx *exec_ctx,
                                              grpc_channel_element *elem,
                                              grpc_pollset *pollset) {
  channel_data *chand = elem->channel_data;
  grpc_pollset_set_add_pollset(exec_ctx, &chand->pollset_set, pollset);
}

void grpc_client_channel_del_interested_party(grpc_exec_ctx *exec_ctx,
                                              grpc_channel_element *elem,
                                              grpc_pollset *pollset) {
  channel_data *chand = elem->channel_data;
  grpc_pollset_set_del_pollset(exec_ctx, &chand->pollset_set, pollset);
}
