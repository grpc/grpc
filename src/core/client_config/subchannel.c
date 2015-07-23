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
#include "src/core/channel/client_channel.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/iomgr/alarm.h"
#include "src/core/transport/connectivity_state.h"
#include "src/core/surface/channel.h"

typedef struct {
  /* all fields protected by subchannel->mu */
  /** refcount */
  int refs;
  /** parent subchannel */
  grpc_subchannel *subchannel;
} connection;

typedef struct {
  grpc_iomgr_closure closure;
  size_t version;
  grpc_subchannel *subchannel;
  grpc_connectivity_state connectivity_state;
} state_watcher;

typedef struct waiting_for_connect {
  struct waiting_for_connect *next;
  grpc_iomgr_closure *notify;
  grpc_pollset *pollset;
  grpc_subchannel_call **target;
  grpc_subchannel *subchannel;
  grpc_iomgr_closure continuation;
} waiting_for_connect;

struct grpc_subchannel {
  grpc_connector *connector;

  /** non-transport related channel filters */
  const grpc_channel_filter **filters;
  size_t num_filters;
  /** channel arguments */
  grpc_channel_args *args;
  /** address to connect to */
  struct sockaddr *addr;
  size_t addr_len;
  /** metadata context */
  grpc_mdctx *mdctx;
  /** master channel - the grpc_channel instance that ultimately owns
      this channel_data via its channel stack.
      We occasionally use this to bump the refcount on the master channel
      to keep ourselves alive through an asynchronous operation. */
  grpc_channel *master;
  /** have we seen a disconnection? */
  int disconnected;

  /** set during connection */
  grpc_connect_out_args connecting_result;

  /** callback for connection finishing */
  grpc_iomgr_closure connected;

  /** pollset_set tracking who's interested in a connection
      being setup - owned by the master channel (in particular the
     client_channel
      filter there-in) */
  grpc_pollset_set *pollset_set;

  /** mutex protecting remaining elements */
  gpr_mu mu;

  /** active connection */
  connection *active;
  /** version number for the active connection */
  size_t active_version;
  /** refcount */
  int refs;
  /** are we connecting */
  int connecting;
  /** things waiting for a connection */
  waiting_for_connect *waiting;
  /** connectivity state tracking */
  grpc_connectivity_state_tracker state_tracker;

  /** next connect attempt time */
  gpr_timespec next_attempt;
  /** amount to backoff each failure */
  gpr_timespec backoff_delta;
  /** do we have an active alarm? */
  int have_alarm;
  /** our alarm */
  grpc_alarm alarm;
};

struct grpc_subchannel_call {
  connection *connection;
  gpr_refcount refs;
};

#define SUBCHANNEL_CALL_TO_CALL_STACK(call) ((grpc_call_stack *)((call) + 1))
#define CHANNEL_STACK_FROM_CONNECTION(con) ((grpc_channel_stack *)((con) + 1))

static grpc_subchannel_call *create_call(connection *con);
static void connectivity_state_changed_locked(grpc_subchannel *c,
                                              const char *reason);
static grpc_connectivity_state compute_connectivity_locked(grpc_subchannel *c);
static gpr_timespec compute_connect_deadline(grpc_subchannel *c);
static void subchannel_connected(void *subchannel, int iomgr_success);

static void subchannel_ref_locked(
    grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
static int subchannel_unref_locked(
    grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) GRPC_MUST_USE_RESULT;
static void connection_ref_locked(connection *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS);
static grpc_subchannel *connection_unref_locked(
    connection *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) GRPC_MUST_USE_RESULT;
static void subchannel_destroy(grpc_subchannel *c);

#ifdef GRPC_SUBCHANNEL_REFCOUNT_DEBUG
#define SUBCHANNEL_REF_LOCKED(p, r) \
  subchannel_ref_locked((p), __FILE__, __LINE__, (r))
#define SUBCHANNEL_UNREF_LOCKED(p, r) \
  subchannel_unref_locked((p), __FILE__, __LINE__, (r))
#define CONNECTION_REF_LOCKED(p, r) \
  connection_ref_locked((p), __FILE__, __LINE__, (r))
#define CONNECTION_UNREF_LOCKED(p, r) \
  connection_unref_locked((p), __FILE__, __LINE__, (r))
#define REF_PASS_ARGS , file, line, reason
#define REF_LOG(name, p)                                                  \
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "%s: %p   ref %d -> %d %s", \
          (name), (p), (p)->refs, (p)->refs + 1, reason)
#define UNREF_LOG(name, p)                                                \
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "%s: %p unref %d -> %d %s", \
          (name), (p), (p)->refs, (p)->refs - 1, reason)
#else
#define SUBCHANNEL_REF_LOCKED(p, r) subchannel_ref_locked((p))
#define SUBCHANNEL_UNREF_LOCKED(p, r) subchannel_unref_locked((p))
#define CONNECTION_REF_LOCKED(p, r) connection_ref_locked((p))
#define CONNECTION_UNREF_LOCKED(p, r) connection_unref_locked((p))
#define REF_PASS_ARGS
#define REF_LOG(name, p) \
  do {                   \
  } while (0)
#define UNREF_LOG(name, p) \
  do {                     \
  } while (0)
#endif

/*
 * connection implementation
 */

static void connection_destroy(connection *c) {
  GPR_ASSERT(c->refs == 0);
  grpc_channel_stack_destroy(CHANNEL_STACK_FROM_CONNECTION(c));
  gpr_free(c);
}

static void connection_ref_locked(
    connection *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  REF_LOG("CONNECTION", c);
  subchannel_ref_locked(c->subchannel REF_PASS_ARGS);
  ++c->refs;
}

static grpc_subchannel *connection_unref_locked(
    connection *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  grpc_subchannel *destroy = NULL;
  UNREF_LOG("CONNECTION", c);
  if (subchannel_unref_locked(c->subchannel REF_PASS_ARGS)) {
    destroy = c->subchannel;
  }
  if (--c->refs == 0 && c->subchannel->active != c) {
    connection_destroy(c);
  }
  return destroy;
}

/*
 * grpc_subchannel implementation
 */

static void subchannel_ref_locked(
    grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  REF_LOG("SUBCHANNEL", c);
  ++c->refs;
}

static int subchannel_unref_locked(
    grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  UNREF_LOG("SUBCHANNEL", c);
  return --c->refs == 0;
}

void grpc_subchannel_ref(grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_mu_lock(&c->mu);
  subchannel_ref_locked(c REF_PASS_ARGS);
  gpr_mu_unlock(&c->mu);
}

void grpc_subchannel_unref(grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  int destroy;
  gpr_mu_lock(&c->mu);
  destroy = subchannel_unref_locked(c REF_PASS_ARGS);
  gpr_mu_unlock(&c->mu);
  if (destroy) subchannel_destroy(c);
}

static void subchannel_destroy(grpc_subchannel *c) {
  if (c->active != NULL) {
    connection_destroy(c->active);
  }
  gpr_free(c->filters);
  grpc_channel_args_destroy(c->args);
  gpr_free(c->addr);
  grpc_mdctx_unref(c->mdctx);
  grpc_connectivity_state_destroy(&c->state_tracker);
  grpc_connector_unref(c->connector);
  gpr_free(c);
}

void grpc_subchannel_add_interested_party(grpc_subchannel *c,
                                          grpc_pollset *pollset) {
  grpc_pollset_set_add_pollset(c->pollset_set, pollset);
}

void grpc_subchannel_del_interested_party(grpc_subchannel *c,
                                          grpc_pollset *pollset) {
  grpc_pollset_set_del_pollset(c->pollset_set, pollset);
}

grpc_subchannel *grpc_subchannel_create(grpc_connector *connector,
                                        grpc_subchannel_args *args) {
  grpc_subchannel *c = gpr_malloc(sizeof(*c));
  grpc_channel_element *parent_elem = grpc_channel_stack_last_element(
      grpc_channel_get_channel_stack(args->master));
  memset(c, 0, sizeof(*c));
  c->refs = 1;
  c->connector = connector;
  grpc_connector_ref(c->connector);
  c->num_filters = args->filter_count;
  c->filters = gpr_malloc(sizeof(grpc_channel_filter *) * c->num_filters);
  memcpy(c->filters, args->filters,
         sizeof(grpc_channel_filter *) * c->num_filters);
  c->addr = gpr_malloc(args->addr_len);
  memcpy(c->addr, args->addr, args->addr_len);
  c->addr_len = args->addr_len;
  c->args = grpc_channel_args_copy(args->args);
  c->mdctx = args->mdctx;
  c->master = args->master;
  c->pollset_set = grpc_client_channel_get_connecting_pollset_set(parent_elem);
  grpc_mdctx_ref(c->mdctx);
  grpc_iomgr_closure_init(&c->connected, subchannel_connected, c);
  grpc_connectivity_state_init(&c->state_tracker, GRPC_CHANNEL_IDLE,
                               "subchannel");
  gpr_mu_init(&c->mu);
  return c;
}

static void continue_connect(grpc_subchannel *c) {
  grpc_connect_in_args args;

  args.interested_parties = c->pollset_set;
  args.addr = c->addr;
  args.addr_len = c->addr_len;
  args.deadline = compute_connect_deadline(c);
  args.channel_args = c->args;
  args.metadata_context = c->mdctx;

  grpc_connector_connect(c->connector, &args, &c->connecting_result,
                         &c->connected);
}

static void start_connect(grpc_subchannel *c) {
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  c->next_attempt = now;
  c->backoff_delta = gpr_time_from_seconds(1, GPR_TIMESPAN);

  continue_connect(c);
}

static void continue_creating_call(void *arg, int iomgr_success) {
  waiting_for_connect *w4c = arg;
  grpc_subchannel_del_interested_party(w4c->subchannel, w4c->pollset);
  grpc_subchannel_create_call(w4c->subchannel, w4c->pollset, w4c->target,
                              w4c->notify);
  GRPC_SUBCHANNEL_UNREF(w4c->subchannel, "waiting_for_connect");
  gpr_free(w4c);
}

void grpc_subchannel_create_call(grpc_subchannel *c, grpc_pollset *pollset,
                                 grpc_subchannel_call **target,
                                 grpc_iomgr_closure *notify) {
  connection *con;
  gpr_mu_lock(&c->mu);
  if (c->active != NULL) {
    con = c->active;
    CONNECTION_REF_LOCKED(con, "call");
    gpr_mu_unlock(&c->mu);

    *target = create_call(con);
    notify->cb(notify->cb_arg, 1);
  } else {
    waiting_for_connect *w4c = gpr_malloc(sizeof(*w4c));
    w4c->next = c->waiting;
    w4c->notify = notify;
    w4c->pollset = pollset;
    w4c->target = target;
    w4c->subchannel = c;
    /* released when clearing w4c */
    SUBCHANNEL_REF_LOCKED(c, "waiting_for_connect");
    grpc_iomgr_closure_init(&w4c->continuation, continue_creating_call, w4c);
    c->waiting = w4c;
    grpc_subchannel_add_interested_party(c, pollset);
    if (!c->connecting) {
      c->connecting = 1;
      connectivity_state_changed_locked(c, "create_call");
      /* released by connection */
      SUBCHANNEL_REF_LOCKED(c, "connecting");
      GRPC_CHANNEL_INTERNAL_REF(c->master, "connecting");
      gpr_mu_unlock(&c->mu);

      start_connect(c);
    } else {
      gpr_mu_unlock(&c->mu);
    }
  }
}

grpc_connectivity_state grpc_subchannel_check_connectivity(grpc_subchannel *c) {
  grpc_connectivity_state state;
  gpr_mu_lock(&c->mu);
  state = grpc_connectivity_state_check(&c->state_tracker);
  gpr_mu_unlock(&c->mu);
  return state;
}

void grpc_subchannel_notify_on_state_change(grpc_subchannel *c,
                                            grpc_connectivity_state *state,
                                            grpc_iomgr_closure *notify) {
  int do_connect = 0;
  gpr_mu_lock(&c->mu);
  if (grpc_connectivity_state_notify_on_state_change(&c->state_tracker, state,
                                                     notify)) {
    do_connect = 1;
    c->connecting = 1;
    /* released by connection */
    SUBCHANNEL_REF_LOCKED(c, "connecting");
    GRPC_CHANNEL_INTERNAL_REF(c->master, "connecting");
    connectivity_state_changed_locked(c, "state_change");
  }
  gpr_mu_unlock(&c->mu);
  if (do_connect) {
    start_connect(c);
  }
}

void grpc_subchannel_process_transport_op(grpc_subchannel *c,
                                          grpc_transport_op *op) {
  connection *con = NULL;
  grpc_subchannel *destroy;
  int cancel_alarm = 0;
  gpr_mu_lock(&c->mu);
  if (op->disconnect) {
    c->disconnected = 1;
    connectivity_state_changed_locked(c, "disconnect");
    if (c->have_alarm) {
      cancel_alarm = 1;
    }
  }
  if (c->active != NULL) {
    con = c->active;
    CONNECTION_REF_LOCKED(con, "transport-op");
  }
  gpr_mu_unlock(&c->mu);

  if (con != NULL) {
    grpc_channel_stack *channel_stack = CHANNEL_STACK_FROM_CONNECTION(con);
    grpc_channel_element *top_elem =
        grpc_channel_stack_element(channel_stack, 0);
    top_elem->filter->start_transport_op(top_elem, op);

    gpr_mu_lock(&c->mu);
    destroy = CONNECTION_UNREF_LOCKED(con, "transport-op");
    gpr_mu_unlock(&c->mu);
    if (destroy) {
      subchannel_destroy(destroy);
    }
  }

  if (cancel_alarm) {
    grpc_alarm_cancel(&c->alarm);
  }
}

static void on_state_changed(void *p, int iomgr_success) {
  state_watcher *sw = p;
  grpc_subchannel *c = sw->subchannel;
  gpr_mu *mu = &c->mu;
  int destroy;
  grpc_transport_op op;
  grpc_channel_element *elem;
  connection *destroy_connection = NULL;

  gpr_mu_lock(mu);

  /* if we failed or there is a version number mismatch, just leave
     this closure */
  if (!iomgr_success || sw->subchannel->active_version != sw->version) {
    goto done;
  }

  switch (sw->connectivity_state) {
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_READY:
    case GRPC_CHANNEL_IDLE:
      /* all is still good: keep watching */
      memset(&op, 0, sizeof(op));
      op.connectivity_state = &sw->connectivity_state;
      op.on_connectivity_state_change = &sw->closure;
      elem = grpc_channel_stack_element(
          CHANNEL_STACK_FROM_CONNECTION(c->active), 0);
      elem->filter->start_transport_op(elem, &op);
      /* early out */
      gpr_mu_unlock(mu);
      return;
    case GRPC_CHANNEL_FATAL_FAILURE:
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      /* things have gone wrong, deactivate and enter idle */
      if (sw->subchannel->active->refs == 0) {
        destroy_connection = sw->subchannel->active;
      }
      sw->subchannel->active = NULL;
      grpc_connectivity_state_set(
          &c->state_tracker, c->disconnected ? GRPC_CHANNEL_FATAL_FAILURE
                                             : GRPC_CHANNEL_TRANSIENT_FAILURE,
          "connection_failed");
      break;
  }

done:
  connectivity_state_changed_locked(c, "transport_state_changed");
  destroy = SUBCHANNEL_UNREF_LOCKED(c, "state_watcher");
  gpr_free(sw);
  gpr_mu_unlock(mu);
  if (destroy) {
    subchannel_destroy(c);
  }
  if (destroy_connection != NULL) {
    connection_destroy(destroy_connection);
  }
}

static void publish_transport(grpc_subchannel *c) {
  size_t channel_stack_size;
  connection *con;
  grpc_channel_stack *stk;
  size_t num_filters;
  const grpc_channel_filter **filters;
  waiting_for_connect *w4c;
  grpc_transport_op op;
  state_watcher *sw;
  connection *destroy_connection = NULL;
  grpc_channel_element *elem;

  gpr_log(GPR_DEBUG, "publish_transport: %p", c->master);

  /* build final filter list */
  num_filters = c->num_filters + c->connecting_result.num_filters + 1;
  filters = gpr_malloc(sizeof(*filters) * num_filters);
  memcpy(filters, c->filters, sizeof(*filters) * c->num_filters);
  memcpy(filters + c->num_filters, c->connecting_result.filters,
         sizeof(*filters) * c->connecting_result.num_filters);
  filters[num_filters - 1] = &grpc_connected_channel_filter;

  /* construct channel stack */
  channel_stack_size = grpc_channel_stack_size(filters, num_filters);
  con = gpr_malloc(sizeof(connection) + channel_stack_size);
  stk = (grpc_channel_stack *)(con + 1);
  con->refs = 0;
  con->subchannel = c;
  grpc_channel_stack_init(filters, num_filters, c->master, c->args, c->mdctx,
                          stk);
  grpc_connected_channel_bind_transport(stk, c->connecting_result.transport);
  gpr_free(c->connecting_result.filters);
  memset(&c->connecting_result, 0, sizeof(c->connecting_result));

  /* initialize state watcher */
  sw = gpr_malloc(sizeof(*sw));
  grpc_iomgr_closure_init(&sw->closure, on_state_changed, sw);
  sw->subchannel = c;
  sw->connectivity_state = GRPC_CHANNEL_READY;

  gpr_mu_lock(&c->mu);

  if (c->disconnected) {
    gpr_mu_unlock(&c->mu);
    gpr_free(sw);
    gpr_free(filters);
    grpc_channel_stack_destroy(stk);
    GRPC_CHANNEL_INTERNAL_UNREF(c->master, "connecting");
    GRPC_SUBCHANNEL_UNREF(c, "connecting");
    return;
  }

  /* publish */
  if (c->active != NULL && c->active->refs == 0) {
    destroy_connection = c->active;
  }
  c->active = con;
  c->active_version++;
  sw->version = c->active_version;
  c->connecting = 0;

  /* watch for changes; subchannel ref for connecting is donated
     to the state watcher */
  memset(&op, 0, sizeof(op));
  op.connectivity_state = &sw->connectivity_state;
  op.on_connectivity_state_change = &sw->closure;
  op.bind_pollset_set = c->pollset_set;
  SUBCHANNEL_REF_LOCKED(c, "state_watcher");
  GRPC_CHANNEL_INTERNAL_UNREF(c->master, "connecting");
  GPR_ASSERT(!SUBCHANNEL_UNREF_LOCKED(c, "connecting"));
  elem =
      grpc_channel_stack_element(CHANNEL_STACK_FROM_CONNECTION(c->active), 0);
  elem->filter->start_transport_op(elem, &op);

  /* signal completion */
  connectivity_state_changed_locked(c, "connected");
  while ((w4c = c->waiting)) {
    c->waiting = w4c->next;
    grpc_iomgr_add_callback(&w4c->continuation);
  }

  gpr_mu_unlock(&c->mu);

  gpr_free(filters);

  if (destroy_connection != NULL) {
    connection_destroy(destroy_connection);
  }
}

static void on_alarm(void *arg, int iomgr_success) {
  grpc_subchannel *c = arg;
  gpr_mu_lock(&c->mu);
  c->have_alarm = 0;
  if (c->disconnected) {
    iomgr_success = 0;
  }
  connectivity_state_changed_locked(c, "alarm");
  gpr_mu_unlock(&c->mu);
  if (iomgr_success) {
    continue_connect(c);
  } else {
    GRPC_CHANNEL_INTERNAL_UNREF(c->master, "connecting");
    GRPC_SUBCHANNEL_UNREF(c, "connecting");
  }
}

static void subchannel_connected(void *arg, int iomgr_success) {
  grpc_subchannel *c = arg;
  if (c->connecting_result.transport != NULL) {
    publish_transport(c);
  } else {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_mu_lock(&c->mu);
    GPR_ASSERT(!c->have_alarm);
    c->have_alarm = 1;
    connectivity_state_changed_locked(c, "connect_failed");
    c->next_attempt = gpr_time_add(c->next_attempt, c->backoff_delta);
    if (gpr_time_cmp(c->backoff_delta,
                     gpr_time_from_seconds(60, GPR_TIMESPAN)) < 0) {
      c->backoff_delta = gpr_time_add(c->backoff_delta, c->backoff_delta);
    }
    grpc_alarm_init(&c->alarm, c->next_attempt, on_alarm, c, now);
    gpr_mu_unlock(&c->mu);
  }
}

static gpr_timespec compute_connect_deadline(grpc_subchannel *c) {
  return gpr_time_add(c->next_attempt, c->backoff_delta);
}

static grpc_connectivity_state compute_connectivity_locked(grpc_subchannel *c) {
  if (c->disconnected) {
    return GRPC_CHANNEL_FATAL_FAILURE;
  }
  if (c->connecting) {
    if (c->have_alarm) {
      return GRPC_CHANNEL_TRANSIENT_FAILURE;
    }
    return GRPC_CHANNEL_CONNECTING;
  }
  if (c->active) {
    return GRPC_CHANNEL_READY;
  }
  return GRPC_CHANNEL_IDLE;
}

static void connectivity_state_changed_locked(grpc_subchannel *c,
                                              const char *reason) {
  grpc_connectivity_state current = compute_connectivity_locked(c);
  grpc_connectivity_state_set(&c->state_tracker, current, reason);
}

/*
 * grpc_subchannel_call implementation
 */

void grpc_subchannel_call_ref(
    grpc_subchannel_call *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_ref(&c->refs);
}

void grpc_subchannel_call_unref(
    grpc_subchannel_call *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  if (gpr_unref(&c->refs)) {
    gpr_mu *mu = &c->connection->subchannel->mu;
    grpc_subchannel *destroy;
    grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(c));
    gpr_mu_lock(mu);
    destroy = CONNECTION_UNREF_LOCKED(c->connection, "call");
    gpr_mu_unlock(mu);
    gpr_free(c);
    if (destroy != NULL) {
      subchannel_destroy(destroy);
    }
  }
}

char *grpc_subchannel_call_get_peer(grpc_subchannel_call *call) {
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  return top_elem->filter->get_peer(top_elem);
}

void grpc_subchannel_call_process_op(grpc_subchannel_call *call,
                                     grpc_transport_stream_op *op) {
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  top_elem->filter->start_transport_stream_op(top_elem, op);
}

grpc_subchannel_call *create_call(connection *con) {
  grpc_channel_stack *chanstk = CHANNEL_STACK_FROM_CONNECTION(con);
  grpc_subchannel_call *call =
      gpr_malloc(sizeof(grpc_subchannel_call) + chanstk->call_stack_size);
  grpc_call_stack *callstk = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  call->connection = con;
  gpr_ref_init(&call->refs, 1);
  grpc_call_stack_init(chanstk, NULL, NULL, callstk);
  return call;
}
