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

#include "src/core/ext/client_config/client_channel.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

#include "src/core/ext/client_config/subchannel_call_holder.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"

/* Client channel implementation */

typedef grpc_subchannel_call_holder call_data;

typedef struct client_channel_channel_data {
  /** resolver for this channel */
  grpc_resolver *resolver;
  /** have we started resolving this channel */
  int started_resolving;

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
  /** owning stack */
  grpc_channel_stack *owning_stack;
  /** interested parties (owned) */
  grpc_pollset_set *interested_parties;
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
  return grpc_subchannel_call_holder_get_peer(exec_ctx, elem->call_data);
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

static void set_channel_connectivity_state_locked(grpc_exec_ctx *exec_ctx,
                                                  channel_data *chand,
                                                  grpc_connectivity_state state,
                                                  const char *reason) {
  if ((state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
       state == GRPC_CHANNEL_SHUTDOWN) &&
      chand->lb_policy != NULL) {
    /* cancel fail-fast picks */
    grpc_lb_policy_cancel_picks(
        exec_ctx, chand->lb_policy,
        /* mask= */ GRPC_INITIAL_METADATA_IGNORE_CONNECTIVITY,
        /* check= */ 0);
  }
  grpc_connectivity_state_set(exec_ctx, &chand->state_tracker, state, reason);
}

static void on_lb_policy_state_changed_locked(
    grpc_exec_ctx *exec_ctx, lb_policy_connectivity_watcher *w) {
  grpc_connectivity_state publish_state = w->state;
  /* check if the notification is for a stale policy */
  if (w->lb_policy != w->chand->lb_policy) return;

  if (publish_state == GRPC_CHANNEL_SHUTDOWN && w->chand->resolver != NULL) {
    publish_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
    grpc_resolver_channel_saw_error(exec_ctx, w->chand->resolver);
    GRPC_LB_POLICY_UNREF(exec_ctx, w->chand->lb_policy, "channel");
    w->chand->lb_policy = NULL;
  }
  set_channel_connectivity_state_locked(exec_ctx, w->chand, publish_state,
                                        "lb_changed");
  if (w->state != GRPC_CHANNEL_SHUTDOWN) {
    watch_lb_policy(exec_ctx, w->chand, w->lb_policy, w->state);
  }
}

static void on_lb_policy_state_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                       bool iomgr_success) {
  lb_policy_connectivity_watcher *w = arg;

  gpr_mu_lock(&w->chand->mu_config);
  on_lb_policy_state_changed_locked(exec_ctx, w);
  gpr_mu_unlock(&w->chand->mu_config);

  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack, "watch_lb_policy");
  gpr_free(w);
}

static void watch_lb_policy(grpc_exec_ctx *exec_ctx, channel_data *chand,
                            grpc_lb_policy *lb_policy,
                            grpc_connectivity_state current_state) {
  lb_policy_connectivity_watcher *w = gpr_malloc(sizeof(*w));
  GRPC_CHANNEL_STACK_REF(chand->owning_stack, "watch_lb_policy");

  w->chand = chand;
  grpc_closure_init(&w->on_changed, on_lb_policy_state_changed, w);
  w->state = current_state;
  w->lb_policy = lb_policy;
  grpc_lb_policy_notify_on_state_change(exec_ctx, lb_policy, &w->state,
                                        &w->on_changed);
}

static void cc_on_config_changed(grpc_exec_ctx *exec_ctx, void *arg,
                                 bool iomgr_success) {
  channel_data *chand = arg;
  grpc_lb_policy *lb_policy = NULL;
  grpc_lb_policy *old_lb_policy;
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

  if (lb_policy != NULL) {
    grpc_pollset_set_add_pollset_set(exec_ctx, lb_policy->interested_parties,
                                     chand->interested_parties);
  }

  gpr_mu_lock(&chand->mu_config);
  old_lb_policy = chand->lb_policy;
  chand->lb_policy = lb_policy;
  if (lb_policy != NULL) {
    grpc_exec_ctx_enqueue_list(exec_ctx, &chand->waiting_for_config_closures,
                               NULL);
  } else if (chand->resolver == NULL /* disconnected */) {
    grpc_closure_list_fail_all(&chand->waiting_for_config_closures);
    grpc_exec_ctx_enqueue_list(exec_ctx, &chand->waiting_for_config_closures,
                               NULL);
  }
  if (lb_policy != NULL && chand->exit_idle_when_lb_policy_arrives) {
    GRPC_LB_POLICY_REF(lb_policy, "exit_idle");
    exit_idle = 1;
    chand->exit_idle_when_lb_policy_arrives = 0;
  }

  if (iomgr_success && chand->resolver) {
    set_channel_connectivity_state_locked(exec_ctx, chand, state,
                                          "new_lb+resolver");
    if (lb_policy != NULL) {
      watch_lb_policy(exec_ctx, chand, lb_policy, state);
    }
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
    grpc_resolver_next(exec_ctx, chand->resolver,
                       &chand->incoming_configuration,
                       &chand->on_config_changed);
    gpr_mu_unlock(&chand->mu_config);
  } else {
    if (chand->resolver != NULL) {
      grpc_resolver_shutdown(exec_ctx, chand->resolver);
      GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
      chand->resolver = NULL;
    }
    set_channel_connectivity_state_locked(
        exec_ctx, chand, GRPC_CHANNEL_SHUTDOWN, "resolver_gone");
    gpr_mu_unlock(&chand->mu_config);
  }

  if (exit_idle) {
    grpc_lb_policy_exit_idle(exec_ctx, lb_policy);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "exit_idle");
  }

  if (old_lb_policy != NULL) {
    grpc_pollset_set_del_pollset_set(
        exec_ctx, old_lb_policy->interested_parties, chand->interested_parties);
    GRPC_LB_POLICY_UNREF(exec_ctx, old_lb_policy, "channel");
  }

  if (lb_policy != NULL) {
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "config_change");
  }

  GRPC_CHANNEL_STACK_UNREF(exec_ctx, chand->owning_stack, "resolver");
}

static void cc_start_transport_op(grpc_exec_ctx *exec_ctx,
                                  grpc_channel_element *elem,
                                  grpc_transport_op *op) {
  channel_data *chand = elem->channel_data;

  grpc_exec_ctx_enqueue(exec_ctx, op->on_consumed, true, NULL);

  GPR_ASSERT(op->set_accept_stream == false);
  if (op->bind_pollset != NULL) {
    grpc_pollset_set_add_pollset(exec_ctx, chand->interested_parties,
                                 op->bind_pollset);
  }

  gpr_mu_lock(&chand->mu_config);
  if (op->on_connectivity_state_change != NULL) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &chand->state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
    op->on_connectivity_state_change = NULL;
    op->connectivity_state = NULL;
  }

  if (op->send_ping != NULL) {
    if (chand->lb_policy == NULL) {
      grpc_exec_ctx_enqueue(exec_ctx, op->send_ping, false, NULL);
    } else {
      grpc_lb_policy_ping_one(exec_ctx, chand->lb_policy, op->send_ping);
      op->bind_pollset = NULL;
    }
    op->send_ping = NULL;
  }

  if (op->disconnect && chand->resolver != NULL) {
    set_channel_connectivity_state_locked(exec_ctx, chand,
                                          GRPC_CHANNEL_SHUTDOWN, "disconnect");
    grpc_resolver_shutdown(exec_ctx, chand->resolver);
    GRPC_RESOLVER_UNREF(exec_ctx, chand->resolver, "channel");
    chand->resolver = NULL;
    if (!chand->started_resolving) {
      grpc_closure_list_fail_all(&chand->waiting_for_config_closures);
      grpc_exec_ctx_enqueue_list(exec_ctx, &chand->waiting_for_config_closures,
                                 NULL);
    }
    if (chand->lb_policy != NULL) {
      grpc_pollset_set_del_pollset_set(exec_ctx,
                                       chand->lb_policy->interested_parties,
                                       chand->interested_parties);
      GRPC_LB_POLICY_UNREF(exec_ctx, chand->lb_policy, "channel");
      chand->lb_policy = NULL;
    }
  }
  gpr_mu_unlock(&chand->mu_config);
}

typedef struct {
  grpc_metadata_batch *initial_metadata;
  uint32_t initial_metadata_flags;
  grpc_connected_subchannel **connected_subchannel;
  grpc_closure *on_ready;
  grpc_call_element *elem;
  grpc_closure closure;
} continue_picking_args;

static int cc_pick_subchannel(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_metadata_batch *initial_metadata,
                              uint32_t initial_metadata_flags,
                              grpc_connected_subchannel **connected_subchannel,
                              grpc_closure *on_ready);

static void continue_picking(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  continue_picking_args *cpa = arg;
  if (cpa->connected_subchannel == NULL) {
    /* cancelled, do nothing */
  } else if (!success) {
    grpc_exec_ctx_enqueue(exec_ctx, cpa->on_ready, false, NULL);
  } else if (cc_pick_subchannel(exec_ctx, cpa->elem, cpa->initial_metadata,
                                cpa->initial_metadata_flags,
                                cpa->connected_subchannel, cpa->on_ready)) {
    grpc_exec_ctx_enqueue(exec_ctx, cpa->on_ready, true, NULL);
  }
  gpr_free(cpa);
}

static int cc_pick_subchannel(grpc_exec_ctx *exec_ctx, void *elemp,
                              grpc_metadata_batch *initial_metadata,
                              uint32_t initial_metadata_flags,
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
      grpc_lb_policy_cancel_pick(exec_ctx, chand->lb_policy,
                                 connected_subchannel);
    }
    for (closure = chand->waiting_for_config_closures.head; closure != NULL;
         closure = grpc_closure_next(closure)) {
      cpa = closure->cb_arg;
      if (cpa->connected_subchannel == connected_subchannel) {
        cpa->connected_subchannel = NULL;
        grpc_exec_ctx_enqueue(exec_ctx, cpa->on_ready, false, NULL);
      }
    }
    gpr_mu_unlock(&chand->mu_config);
    return 1;
  }
  if (chand->lb_policy != NULL) {
    grpc_lb_policy *lb_policy = chand->lb_policy;
    int r;
    GRPC_LB_POLICY_REF(lb_policy, "cc_pick_subchannel");
    gpr_mu_unlock(&chand->mu_config);
    r = grpc_lb_policy_pick(exec_ctx, lb_policy, calld->pollent,
                            initial_metadata, initial_metadata_flags,
                            connected_subchannel, on_ready);
    GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "cc_pick_subchannel");
    return r;
  }
  if (chand->resolver != NULL && !chand->started_resolving) {
    chand->started_resolving = 1;
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
    grpc_resolver_next(exec_ctx, chand->resolver,
                       &chand->incoming_configuration,
                       &chand->on_config_changed);
  }
  if (chand->resolver != NULL) {
    cpa = gpr_malloc(sizeof(*cpa));
    cpa->initial_metadata = initial_metadata;
    cpa->initial_metadata_flags = initial_metadata_flags;
    cpa->connected_subchannel = connected_subchannel;
    cpa->on_ready = on_ready;
    cpa->elem = elem;
    grpc_closure_init(&cpa->closure, continue_picking, cpa);
    grpc_closure_list_add(&chand->waiting_for_config_closures, &cpa->closure,
                          1);
  } else {
    grpc_exec_ctx_enqueue(exec_ctx, on_ready, false, NULL);
  }
  gpr_mu_unlock(&chand->mu_config);
  return 0;
}

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           grpc_call_element_args *args) {
  grpc_subchannel_call_holder_init(elem->call_data, cc_pick_subchannel, elem,
                                   args->call_stack);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_stats *stats,
                              void *and_free_memory) {
  grpc_subchannel_call_holder_destroy(exec_ctx, elem->call_data);
  gpr_free(and_free_memory);
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
  grpc_closure_init(&chand->on_config_changed, cc_on_config_changed, chand);
  chand->owning_stack = args->channel_stack;

  grpc_connectivity_state_init(&chand->state_tracker, GRPC_CHANNEL_IDLE,
                               "client_channel");
  chand->interested_parties = grpc_pollset_set_create();
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
    grpc_pollset_set_del_pollset_set(exec_ctx,
                                     chand->lb_policy->interested_parties,
                                     chand->interested_parties);
    GRPC_LB_POLICY_UNREF(exec_ctx, chand->lb_policy, "channel");
  }
  grpc_connectivity_state_destroy(exec_ctx, &chand->state_tracker);
  grpc_pollset_set_destroy(chand->interested_parties);
  gpr_mu_destroy(&chand->mu_config);
}

static void cc_set_pollset_or_pollset_set(grpc_exec_ctx *exec_ctx,
                                          grpc_call_element *elem,
                                          grpc_polling_entity *pollent) {
  call_data *calld = elem->call_data;
  calld->pollent = pollent;
}

const grpc_channel_filter grpc_client_channel_filter = {
    cc_start_transport_stream_op,
    cc_start_transport_op,
    sizeof(call_data),
    init_call_elem,
    cc_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    cc_get_peer,
    "client-channel",
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
    GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
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
        GRPC_CHANNEL_STACK_REF(chand->owning_stack, "resolver");
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

typedef struct {
  channel_data *chand;
  grpc_pollset *pollset;
  grpc_closure *on_complete;
  grpc_closure my_closure;
} external_connectivity_watcher;

static void on_external_watch_complete(grpc_exec_ctx *exec_ctx, void *arg,
                                       bool iomgr_success) {
  external_connectivity_watcher *w = arg;
  grpc_closure *follow_up = w->on_complete;
  grpc_pollset_set_del_pollset(exec_ctx, w->chand->interested_parties,
                               w->pollset);
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, w->chand->owning_stack,
                           "external_connectivity_watcher");
  gpr_free(w);
  follow_up->cb(exec_ctx, follow_up->cb_arg, iomgr_success);
}

void grpc_client_channel_watch_connectivity_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_element *elem, grpc_pollset *pollset,
    grpc_connectivity_state *state, grpc_closure *on_complete) {
  channel_data *chand = elem->channel_data;
  external_connectivity_watcher *w = gpr_malloc(sizeof(*w));
  w->chand = chand;
  w->pollset = pollset;
  w->on_complete = on_complete;
  grpc_pollset_set_add_pollset(exec_ctx, chand->interested_parties, pollset);
  grpc_closure_init(&w->my_closure, on_external_watch_complete, w);
  GRPC_CHANNEL_STACK_REF(w->chand->owning_stack,
                         "external_connectivity_watcher");
  gpr_mu_lock(&chand->mu_config);
  grpc_connectivity_state_notify_on_state_change(
      exec_ctx, &chand->state_tracker, state, &w->my_closure);
  gpr_mu_unlock(&chand->mu_config);
}
