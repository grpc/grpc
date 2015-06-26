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

#include "src/core/client_config/subchannel.h"

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/channel/channel_args.h"

typedef struct {
  gpr_refcount refs;
  grpc_subchannel *subchannel;
} connection;

typedef struct waiting_for_connect {
  struct waiting_for_connect *next;
  grpc_iomgr_closure *notify;
  grpc_transport_stream_op *initial_op;
  grpc_subchannel_call **target;
} waiting_for_connect;

typedef struct connectivity_state_watcher {
  struct connectivity_state_watcher *next;
  grpc_iomgr_closure *notify;
  grpc_connectivity_state *current;
} connectivity_state_watcher;

struct grpc_subchannel {
  gpr_refcount refs;
  grpc_connector *connector;

  /** non-transport related channel filters */
  const grpc_channel_filter **filters;
  size_t filter_count;
  /** channel arguments */
  grpc_channel_args *args;
  /** address to connect to */
  struct sockaddr *addr;
  size_t addr_len;
  /** metadata context */
  grpc_mdctx *mdctx;

  /** set during connection */
  grpc_transport *connecting_transport;

  /** callback for connection finishing */
  grpc_iomgr_closure connected;

  /** pollset_set tracking who's interested in a connection
      being setup */
  grpc_pollset_set pollset_set;

  /** mutex protecting remaining elements */
  gpr_mu mu;

  /** active connection */
  connection *active;
  /** are we connecting */
  int connecting;
  /** things waiting for a connection */
  waiting_for_connect *waiting;
  /** things watching the connectivity state */
  connectivity_state_watcher *watchers;
};

struct grpc_subchannel_call {
  connection *connection;
  gpr_refcount refs;
};

#define SUBCHANNEL_CALL_TO_CALL_STACK(call) (((grpc_call_stack *)(call)) + 1)

static grpc_subchannel_call *create_call(connection *con, grpc_transport_stream_op *initial_op);
static void connectivity_state_changed_locked(grpc_subchannel *c);
static grpc_connectivity_state compute_connectivity_locked(grpc_subchannel *c);
static gpr_timespec compute_connect_deadline(grpc_subchannel *c);

/*
 * grpc_subchannel implementation
 */

void grpc_subchannel_ref(grpc_subchannel *c) { gpr_ref(&c->refs); }

void grpc_subchannel_unref(grpc_subchannel *c) {
  if (gpr_unref(&c->refs)) {
    gpr_free(c->filters);
    grpc_channel_args_destroy(c->args);
    gpr_free(c->addr);
    grpc_mdctx_unref(c->mdctx);
    gpr_free(c);
  }
}

void grpc_subchannel_add_interested_party(grpc_subchannel *c,
                                          grpc_pollset *pollset) {
  grpc_pollset_set_add_pollset(&c->pollset_set, pollset);
}

void grpc_subchannel_del_interested_party(grpc_subchannel *c,
                                          grpc_pollset *pollset) {
  grpc_pollset_set_del_pollset(&c->pollset_set, pollset);
}

grpc_subchannel *grpc_subchannel_create(grpc_connector *connector,
                                        grpc_subchannel_args *args) {
  grpc_subchannel *c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  gpr_ref_init(&c->refs, 1);
  c->connector = connector;
  grpc_connector_ref(c->connector);
  c->filters = gpr_malloc(sizeof(grpc_channel_filter *) * args->filter_count);
  memcpy(c->filters, args->filters,
         sizeof(grpc_channel_filter *) * args->filter_count);
  c->filter_count = args->filter_count;
  c->addr = gpr_malloc(args->addr_len);
  memcpy(c->addr, args->addr, args->addr_len);
  c->addr_len = args->addr_len;
  c->args = grpc_channel_args_copy(args->args);
  c->mdctx = args->mdctx;
  grpc_mdctx_ref(c->mdctx);
  gpr_mu_init(&c->mu);
  return c;
}

void grpc_subchannel_create_call(grpc_subchannel *c,
                                 grpc_transport_stream_op *initial_op,
                                 grpc_subchannel_call **target,
                                 grpc_iomgr_closure *notify) {
  connection *con;
  gpr_mu_lock(&c->mu);
  if (c->active != NULL) {
    con = c->active;
    gpr_ref(&con->refs);
    gpr_mu_unlock(&c->mu);

    *target = create_call(con, initial_op);
    notify->cb(notify->cb_arg, 1);
  } else {
    waiting_for_connect *w4c = gpr_malloc(sizeof(*w4c));
    w4c->next = c->waiting;
    w4c->notify = notify;
    w4c->initial_op = initial_op;
    w4c->target = target;
    c->waiting = w4c;
    grpc_subchannel_add_interested_party(c, initial_op->bind_pollset);
    if (!c->connecting) {
      c->connecting = 1;
      connectivity_state_changed_locked(c);
      gpr_mu_unlock(&c->mu);

      grpc_connector_connect(c->connector, &c->pollset_set, c->addr,
                             c->addr_len, compute_connect_deadline(c), c->args,
                             c->mdctx, &c->connecting_transport, &c->connected);
    } else {
      gpr_mu_unlock(&c->mu);
    }
  }
}

grpc_connectivity_state grpc_subchannel_check_connectivity(grpc_subchannel *c) {
  grpc_connectivity_state state;
  gpr_mu_lock(&c->mu);
  state = compute_connectivity_locked(c);
  gpr_mu_unlock(&c->mu);
  return state;
}

void grpc_subchannel_notify_on_state_change(grpc_subchannel *c,
                                            grpc_connectivity_state *state,
                                            grpc_iomgr_closure *notify) {
  grpc_connectivity_state current;
  int do_connect = 0;
  connectivity_state_watcher *w = gpr_malloc(sizeof(*w));
  w->current = state;
  w->notify = notify;
  gpr_mu_lock(&c->mu);
  current = compute_connectivity_locked(c);
  if (current == GRPC_CHANNEL_IDLE) {
    current = GRPC_CHANNEL_CONNECTING;
    c->connecting = 1;
    do_connect = 1;
    connectivity_state_changed_locked(c);
  }
  if (current != *state) {
    gpr_mu_unlock(&c->mu);
    *state = current;
    grpc_iomgr_add_callback(notify);
    gpr_free(w);
  } else {
    w->next = c->watchers;
    c->watchers = w;
    gpr_mu_unlock(&c->mu);
  }
  if (do_connect) {
    grpc_connector_connect(c->connector, &c->pollset_set, c->addr, c->addr_len,
                           compute_connect_deadline(c), c->args, c->mdctx,
                           &c->connecting_transport, &c->connected);
  }
}

static gpr_timespec compute_connect_deadline(grpc_subchannel *c) {
  return gpr_time_add(gpr_now(), gpr_time_from_seconds(60));
}

static grpc_connectivity_state compute_connectivity_locked(grpc_subchannel *c) {
  if (c->connecting) {
    return GRPC_CHANNEL_CONNECTING;
  }
  if (c->active) {
    return GRPC_CHANNEL_READY;
  }
  return GRPC_CHANNEL_IDLE;
}

static void connectivity_state_changed_locked(grpc_subchannel *c) {
  grpc_connectivity_state current = compute_connectivity_locked(c);
  connectivity_state_watcher *new = NULL;
  connectivity_state_watcher *w;
  while ((w = c->watchers)) {
    c->watchers = w->next;

    if (current != *w->current) {
      *w->current = current;
      grpc_iomgr_add_callback(w->notify);
      gpr_free(w);
    } else {
      w->next = new;
      new = w;
    }
  }
  c->watchers = new;
}

/*
 * grpc_subchannel_call implementation
 */

void grpc_subchannel_call_ref(grpc_subchannel_call *call) {
  gpr_ref(&call->refs);
}

void grpc_subchannel_call_unref(grpc_subchannel_call *call) {
  if (gpr_unref(&call->refs)) {
    grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(call));
    if (gpr_unref(&call->connection->refs)) {
      gpr_free(call->connection);
    }
    gpr_free(call);
  }
}

void grpc_subchannel_call_process_op(grpc_subchannel_call *call,
                                     grpc_transport_stream_op *op) {
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  top_elem->filter->start_transport_stream_op(top_elem, op);
}

grpc_subchannel_call *create_call(connection *con, grpc_transport_stream_op *initial_op) {
  abort();
  return NULL;
}
