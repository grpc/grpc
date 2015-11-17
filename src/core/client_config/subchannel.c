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
#include "src/core/iomgr/timer.h"
#include "src/core/profiling/timers.h"
#include "src/core/surface/channel.h"
#include "src/core/transport/connectivity_state.h"
#include "src/core/transport/connectivity_state.h"

#define GRPC_SUBCHANNEL_MIN_CONNECT_TIMEOUT_SECONDS 20
#define GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_SUBCHANNEL_RECONNECT_JITTER 0.2

#define GET_CONNECTED_SUBCHANNEL(subchannel, barrier) \
  ((grpc_connected_subchannel *)(gpr_atm_##barrier##_load(&(subchannel)->connected_subchannel)))

struct grpc_connected_subchannel {
  /** refcount */
  gpr_refcount refs;
};

typedef struct {
  grpc_closure closure;
  union {
    grpc_subchannel *subchannel;
    grpc_connected_subchannel *connected_subchannel;
  } whom;
  grpc_connectivity_state connectivity_state;
} state_watcher;

struct grpc_subchannel {
  grpc_connector *connector;

  /** refcount */
  gpr_refcount refs;

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

  /** set during connection */
  grpc_connect_out_args connecting_result;

  /** callback for connection finishing */
  grpc_closure connected;

  /** pollset_set tracking who's interested in a connection
      being setup - owned by the master channel (in particular the
     client_channel
      filter there-in) */
  grpc_pollset_set *pollset_set;

  /** active connection, or null; of type grpc_connected_subchannel */
  gpr_atm connected_subchannel;

  /** mutex protecting remaining elements */
  gpr_mu mu;

  /** have we seen a disconnection? */
  int disconnected;
  /** are we connecting */
  int connecting;
  /** connectivity state tracking */
  grpc_connectivity_state_tracker state_tracker;

  /** next connect attempt time */
  gpr_timespec next_attempt;
  /** amount to backoff each failure */
  gpr_timespec backoff_delta;
  /** do we have an active alarm? */
  int have_alarm;
  /** our alarm */
  grpc_timer alarm;
  /** current random value */
  gpr_uint32 random;
};

struct grpc_subchannel_call {
  grpc_connected_subchannel *connection;
};

#define SUBCHANNEL_CALL_TO_CALL_STACK(call) ((grpc_call_stack *)((call) + 1))
#define CHANNEL_STACK_FROM_CONNECTION(con) ((grpc_channel_stack *)((con) + 1))
#define CALLSTACK_TO_SUBCHANNEL_CALL(callstack) \
  (((grpc_subchannel_call *)(callstack)) - 1)

static gpr_timespec compute_connect_deadline(grpc_subchannel *c);
static void subchannel_connected(grpc_exec_ctx *exec_ctx, void *subchannel,
                                 int iomgr_success);

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define SUBCHANNEL_REF_LOCKED(p, r) \
  subchannel_ref_locked((p), __FILE__, __LINE__, (r))
#define SUBCHANNEL_UNREF_LOCKED(p, r) \
  subchannel_unref_locked((p), __FILE__, __LINE__, (r))
#define CONNECTION_REF_LOCKED(p, r) \
  connection_ref_locked((p), __FILE__, __LINE__, (r))
#define CONNECTION_UNREF_LOCKED(cl, p, r) \
  connection_unref_locked((cl), (p), __FILE__, __LINE__, (r))
#define REF_PASS_ARGS , file, line, reason
#define REF_PASS_REASON , reason
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
#define CONNECTION_UNREF_LOCKED(cl, p, r) connection_unref_locked((cl), (p))
#define REF_PASS_ARGS
#define REF_PASS_REASON
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

static void connection_destroy(grpc_exec_ctx *exec_ctx, void *arg, int success) {
  grpc_connected_subchannel *c = arg;
  grpc_channel_stack_destroy(exec_ctx, CHANNEL_STACK_FROM_CONNECTION(c));
  gpr_free(c);
}

void grpc_connected_subchannel_ref(grpc_connected_subchannel *c
                                      GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  REF_LOG("CONNECTION", c);
  gpr_ref(&c->refs);
}

void grpc_connected_subchannel_unref(
    grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  UNREF_LOG("CONNECTION", c);
  if (gpr_unref(&c->refs)) {
    grpc_exec_ctx_enqueue(exec_ctx, grpc_closure_create(connection_destroy, c), 1);
  }
}

/*
 * grpc_subchannel implementation
 */

static void subchannel_destroy(grpc_exec_ctx *exec_ctx, void *arg, int success) {
  grpc_subchannel *c = arg;
  grpc_connected_subchannel *con = GET_CONNECTED_SUBCHANNEL(c, no_barrier);
  if (con != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, con, "connection");
  }
  gpr_free((void *)c->filters);
  grpc_channel_args_destroy(c->args);
  gpr_free(c->addr);
  grpc_mdctx_unref(c->mdctx);
  grpc_connectivity_state_destroy(exec_ctx, &c->state_tracker);
  grpc_connector_unref(exec_ctx, c->connector);
  gpr_free(c);
}

void grpc_subchannel_ref(grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_ref(&c->refs);
}

void grpc_subchannel_unref(grpc_exec_ctx *exec_ctx,
                           grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  if (gpr_unref(&c->refs)) {
    grpc_exec_ctx_enqueue(exec_ctx, grpc_closure_create(subchannel_destroy, c), 1);
  }
}

void grpc_subchannel_add_interested_party(grpc_exec_ctx *exec_ctx,
                                          grpc_subchannel *c,
                                          grpc_pollset *pollset) {
  grpc_pollset_set_add_pollset(exec_ctx, c->pollset_set, pollset);
}

void grpc_subchannel_del_interested_party(grpc_exec_ctx *exec_ctx,
                                          grpc_subchannel *c,
                                          grpc_pollset *pollset) {
  grpc_pollset_set_del_pollset(exec_ctx, c->pollset_set, pollset);
}

static gpr_uint32 random_seed() {
  return (gpr_uint32)(gpr_time_to_millis(gpr_now(GPR_CLOCK_MONOTONIC)));
}

grpc_subchannel *grpc_subchannel_create(grpc_connector *connector,
                                        grpc_subchannel_args *args) {
  grpc_subchannel *c = gpr_malloc(sizeof(*c));
  grpc_channel_element *parent_elem = grpc_channel_stack_last_element(
      grpc_channel_get_channel_stack(args->master));
  memset(c, 0, sizeof(*c));
  gpr_ref_init(&c->refs, 1);
  c->connector = connector;
  grpc_connector_ref(c->connector);
  c->num_filters = args->filter_count;
  c->filters = gpr_malloc(sizeof(grpc_channel_filter *) * c->num_filters);
  memcpy((void *)c->filters, args->filters,
         sizeof(grpc_channel_filter *) * c->num_filters);
  c->addr = gpr_malloc(args->addr_len);
  memcpy(c->addr, args->addr, args->addr_len);
  c->addr_len = args->addr_len;
  c->args = grpc_channel_args_copy(args->args);
  c->mdctx = args->mdctx;
  c->master = args->master;
  c->pollset_set = grpc_client_channel_get_connecting_pollset_set(parent_elem);
  c->random = random_seed();
  grpc_mdctx_ref(c->mdctx);
  grpc_closure_init(&c->connected, subchannel_connected, c);
  grpc_connectivity_state_init(&c->state_tracker, GRPC_CHANNEL_IDLE,
                               "subchannel");
  gpr_mu_init(&c->mu);
  return c;
}

static void continue_connect(grpc_exec_ctx *exec_ctx, grpc_subchannel *c) {
  grpc_connect_in_args args;

  args.interested_parties = c->pollset_set;
  args.addr = c->addr;
  args.addr_len = c->addr_len;
  args.deadline = compute_connect_deadline(c);
  args.channel_args = c->args;

  grpc_connectivity_state_set(exec_ctx, &c->state_tracker, GRPC_CHANNEL_CONNECTING, "state_change");
  grpc_connector_connect(exec_ctx, c->connector, &args, &c->connecting_result,
                         &c->connected);
}

static void start_connect(grpc_exec_ctx *exec_ctx, grpc_subchannel *c) {
  c->backoff_delta = gpr_time_from_seconds(
      GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS, GPR_TIMESPAN);
  c->next_attempt =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), c->backoff_delta);
  continue_connect(exec_ctx, c);
}

grpc_connectivity_state grpc_subchannel_check_connectivity(grpc_subchannel *c) {
  grpc_connectivity_state state;
  gpr_mu_lock(&c->mu);
  state = grpc_connectivity_state_check(&c->state_tracker);
  gpr_mu_unlock(&c->mu);
  return state;
}

void grpc_subchannel_notify_on_state_change(grpc_exec_ctx *exec_ctx,
                                            grpc_subchannel *c,
                                            grpc_connectivity_state *state,
                                            grpc_closure *notify) {
  int do_connect = 0;
  gpr_mu_lock(&c->mu);
  if (grpc_connectivity_state_notify_on_state_change(
          exec_ctx, &c->state_tracker, state, notify)) {
    do_connect = 1;
    c->connecting = 1;
    /* released by connection */
    GRPC_SUBCHANNEL_REF(c, "connecting");
    GRPC_CHANNEL_INTERNAL_REF(c->master, "connecting");
  }
  gpr_mu_unlock(&c->mu);

  if (do_connect) {
    start_connect(exec_ctx, c);
  }
}

void grpc_subchannel_state_change_unsubscribe(grpc_exec_ctx *exec_ctx,
                                             grpc_subchannel *c,
                                             grpc_closure *subscribed_notify) {
  gpr_mu_lock(&c->mu);
  grpc_connectivity_state_change_unsubscribe(
      exec_ctx, &c->state_tracker, subscribed_notify);
  gpr_mu_unlock(&c->mu);
}

void grpc_subchannel_process_transport_op(grpc_exec_ctx *exec_ctx,
                                          grpc_subchannel *c,
                                          grpc_transport_op *op) {
  grpc_connected_subchannel *con;
  int cancel_alarm = 0;
  gpr_mu_lock(&c->mu);
  con = GET_CONNECTED_SUBCHANNEL(c, no_barrier);
  if (con != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_REF(con, "transport-op");
  }
  if (op->disconnect) {
    c->disconnected = 1;
    grpc_connectivity_state_set(exec_ctx, &c->state_tracker, GRPC_CHANNEL_FATAL_FAILURE, "disconnect");
    if (c->have_alarm) {
      cancel_alarm = 1;
    }
  }
  gpr_mu_unlock(&c->mu);

  if (con != NULL) {
    grpc_connected_subchannel_process_transport_op(exec_ctx, con, op);
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, con, "transport-op");
  }

  if (cancel_alarm) {
    grpc_timer_cancel(exec_ctx, &c->alarm);
  }

  if (op->disconnect) {
    grpc_connector_shutdown(exec_ctx, c->connector);
  }
}

void grpc_connected_subchannel_process_transport_op(grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *con, grpc_transport_op *op) {
  grpc_channel_stack *channel_stack = CHANNEL_STACK_FROM_CONNECTION(con);
  grpc_channel_element *top_elem =
      grpc_channel_stack_element(channel_stack, 0);
  top_elem->filter->start_transport_op(exec_ctx, top_elem, op);
}

static void subchannel_on_child_state_changed(grpc_exec_ctx *exec_ctx, void *p,
                             int iomgr_success) {
  state_watcher *sw = p;
  grpc_subchannel *c = sw->whom.subchannel;
  gpr_mu *mu = &c->mu;

  gpr_mu_lock(mu);

  /* if we failed just leave this closure */
  if (iomgr_success) {
    grpc_connectivity_state_set(exec_ctx, &c->state_tracker, sw->connectivity_state, "reflect_child");
    if (sw->connectivity_state != GRPC_CHANNEL_FATAL_FAILURE) {
      grpc_connected_subchannel_notify_on_state_change(exec_ctx, GET_CONNECTED_SUBCHANNEL(c, no_barrier), &sw->connectivity_state, &sw->closure);
      GRPC_SUBCHANNEL_REF(c, "state_watcher");
      sw = NULL;
    }
  }

  gpr_mu_unlock(mu);
  GRPC_SUBCHANNEL_UNREF(exec_ctx, c, "state_watcher");
  gpr_free(sw);
}

static void connected_subchannel_state_op(grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *con, grpc_connectivity_state *state, grpc_closure *closure) {
  grpc_transport_op op;
  grpc_channel_element *elem;
  memset(&op, 0, sizeof(op));
  op.connectivity_state = state;
  op.on_connectivity_state_change = closure;
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CONNECTION(con), 0);
  elem->filter->start_transport_op(exec_ctx, elem, &op);
}

void grpc_connected_subchannel_notify_on_state_change(grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *con, grpc_connectivity_state *state, grpc_closure *closure) {
  GPR_ASSERT(state != NULL);
  connected_subchannel_state_op(exec_ctx, con, state, closure);
}

void grpc_connected_subchannel_state_change_unsubscribe(grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *con, grpc_closure *closure) {
  connected_subchannel_state_op(exec_ctx, con, NULL, closure);
}

static void publish_transport(grpc_exec_ctx *exec_ctx, grpc_subchannel *c) {
  size_t channel_stack_size;
  grpc_connected_subchannel *con;
  grpc_channel_stack *stk;
  size_t num_filters;
  const grpc_channel_filter **filters;
  state_watcher *sw_subchannel;

  /* build final filter list */
  num_filters = c->num_filters + c->connecting_result.num_filters + 1;
  filters = gpr_malloc(sizeof(*filters) * num_filters);
  memcpy((void *)filters, c->filters, sizeof(*filters) * c->num_filters);
  memcpy((void *)(filters + c->num_filters), c->connecting_result.filters,
         sizeof(*filters) * c->connecting_result.num_filters);
  filters[num_filters - 1] = &grpc_connected_channel_filter;

  /* construct channel stack */
  channel_stack_size = grpc_channel_stack_size(filters, num_filters);
  con = gpr_malloc(sizeof(grpc_connected_subchannel) + channel_stack_size);
  stk = (grpc_channel_stack *)(con + 1);
  gpr_ref_init(&c->refs, 1);
  grpc_channel_stack_init(exec_ctx, filters, num_filters, c->master, c->args,
                          c->mdctx, stk);
  grpc_connected_channel_bind_transport(stk, c->connecting_result.transport);
  gpr_free((void *)c->connecting_result.filters);
  memset(&c->connecting_result, 0, sizeof(c->connecting_result));

  /* initialize state watcher */
  sw_subchannel = gpr_malloc(sizeof(*sw_subchannel));
  sw_subchannel->whom.subchannel = c;
  sw_subchannel->connectivity_state = GRPC_CHANNEL_READY;
  grpc_closure_init(&sw_subchannel->closure, subchannel_on_child_state_changed, sw_subchannel);

  gpr_mu_lock(&c->mu);

  if (c->disconnected) {
    gpr_mu_unlock(&c->mu);
    gpr_free(sw_subchannel);
    gpr_free((void *)filters);
    grpc_channel_stack_destroy(exec_ctx, stk);
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, c->master, "connecting");
    GRPC_SUBCHANNEL_UNREF(exec_ctx, c, "connecting");
    return;
  }

  /* publish */
  GPR_ASSERT(gpr_atm_no_barrier_cas(&c->connected_subchannel, 0, (gpr_atm)con));
  c->connecting = 0;

  /* setup subchannel watching connected subchannel for changes; subchannel ref for connecting is donated
     to the state watcher */
  GRPC_SUBCHANNEL_REF(c, "state_watcher");
  GRPC_SUBCHANNEL_UNREF(exec_ctx, c, "connecting");
  grpc_connected_subchannel_notify_on_state_change(exec_ctx, con, &sw_subchannel->connectivity_state, &sw_subchannel->closure);

#if 0
  grpc_transport_op op;
  grpc_channel_element *elem;

  /* setup connected subchannel watching transport for changes */
  memset(&op, 0, sizeof(op));
  op.connectivity_state = &sw_connected_subchannel->connectivity_state;
  op.on_connectivity_state_change = &sw_connected_subchannel->closure;
  op.bind_pollset_set = c->pollset_set;
  elem =
      grpc_channel_stack_element(CHANNEL_STACK_FROM_CONNECTION(con), 0);
  elem->filter->start_transport_op(exec_ctx, elem, &op);
#endif

  /* signal completion */
  grpc_connectivity_state_set(exec_ctx, &c->state_tracker, GRPC_CHANNEL_READY, "connected");

  gpr_mu_unlock(&c->mu);
  gpr_free((void *)filters);
  GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, c->master, "connecting");
}

/* Generate a random number between 0 and 1. */
static double generate_uniform_random_number(grpc_subchannel *c) {
  c->random = (1103515245 * c->random + 12345) % ((gpr_uint32)1 << 31);
  return c->random / (double)((gpr_uint32)1 << 31);
}

/* Update backoff_delta and next_attempt in subchannel */
static void update_reconnect_parameters(grpc_subchannel *c) {
  gpr_int32 backoff_delta_millis, jitter;
  gpr_int32 max_backoff_millis =
      GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS * 1000;
  double jitter_range;
  backoff_delta_millis =
      (gpr_int32)(gpr_time_to_millis(c->backoff_delta) *
                  GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER);
  if (backoff_delta_millis > max_backoff_millis) {
    backoff_delta_millis = max_backoff_millis;
  }
  c->backoff_delta = gpr_time_from_millis(backoff_delta_millis, GPR_TIMESPAN);
  c->next_attempt =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), c->backoff_delta);

  jitter_range = GRPC_SUBCHANNEL_RECONNECT_JITTER * backoff_delta_millis;
  jitter =
      (gpr_int32)((2 * generate_uniform_random_number(c) - 1) * jitter_range);
  c->next_attempt =
      gpr_time_add(c->next_attempt, gpr_time_from_millis(jitter, GPR_TIMESPAN));
}

static void on_alarm(grpc_exec_ctx *exec_ctx, void *arg, int iomgr_success) {
  grpc_subchannel *c = arg;
  gpr_mu_lock(&c->mu);
  c->have_alarm = 0;
  if (c->disconnected) {
    iomgr_success = 0;
  }
  gpr_mu_unlock(&c->mu);
  if (iomgr_success) {
    update_reconnect_parameters(c);
    continue_connect(exec_ctx, c);
  } else {
    GRPC_CHANNEL_INTERNAL_UNREF(exec_ctx, c->master, "connecting");
    GRPC_SUBCHANNEL_UNREF(exec_ctx, c, "connecting");
  }
}

static void subchannel_connected(grpc_exec_ctx *exec_ctx, void *arg,
                                 int iomgr_success) {
  grpc_subchannel *c = arg;
  if (c->connecting_result.transport != NULL) {
    publish_transport(exec_ctx, c);
  } else if (c->disconnected) {
    /* do nothing */
  } else {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_mu_lock(&c->mu);
    GPR_ASSERT(!c->have_alarm);
    c->have_alarm = 1;
    grpc_connectivity_state_set(exec_ctx, &c->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE, "connect_failed");
    grpc_timer_init(exec_ctx, &c->alarm, c->next_attempt, on_alarm, c, now);
    gpr_mu_unlock(&c->mu);
  }
}

static gpr_timespec compute_connect_deadline(grpc_subchannel *c) {
  gpr_timespec current_deadline =
      gpr_time_add(c->next_attempt, c->backoff_delta);
  gpr_timespec min_deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(GRPC_SUBCHANNEL_MIN_CONNECT_TIMEOUT_SECONDS,
                            GPR_TIMESPAN));
  return gpr_time_cmp(current_deadline, min_deadline) > 0 ? current_deadline
                                                          : min_deadline;
}

/*
 * grpc_subchannel_call implementation
 */

static void subchannel_call_destroy(grpc_exec_ctx *exec_ctx, void *call,
                                    int success) {
  grpc_subchannel_call *c = call;
  GPR_TIMER_BEGIN("grpc_subchannel_call_unref.destroy", 0);
  grpc_call_stack_destroy(exec_ctx, SUBCHANNEL_CALL_TO_CALL_STACK(c));
  gpr_free(c);
  GPR_TIMER_END("grpc_subchannel_call_unref.destroy", 0);
}

void grpc_subchannel_call_ref(grpc_subchannel_call *c
                                  GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
#ifdef GRPC_STREAM_REFCOUNT_DEBUG
  grpc_call_stack_ref(SUBCHANNEL_CALL_TO_CALL_STACK(c), reason);
#else
  grpc_call_stack_ref(SUBCHANNEL_CALL_TO_CALL_STACK(c));
#endif
}

void grpc_subchannel_call_unref(grpc_exec_ctx *exec_ctx,
                                grpc_subchannel_call *c
                                    GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
#ifdef GRPC_STREAM_REFCOUNT_DEBUG
  grpc_call_stack_unref(exec_ctx, SUBCHANNEL_CALL_TO_CALL_STACK(c), reason);
#else
  grpc_call_stack_unref(exec_ctx, SUBCHANNEL_CALL_TO_CALL_STACK(c));
#endif
}

char *grpc_subchannel_call_get_peer(grpc_exec_ctx *exec_ctx,
                                    grpc_subchannel_call *call) {
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  return top_elem->filter->get_peer(exec_ctx, top_elem);
}

void grpc_subchannel_call_process_op(grpc_exec_ctx *exec_ctx,
                                     grpc_subchannel_call *call,
                                     grpc_transport_stream_op *op) {
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  top_elem->filter->start_transport_stream_op(exec_ctx, top_elem, op);
}

grpc_connected_subchannel *grpc_subchannel_get_connected_subchannel(grpc_subchannel *c) {
  return GET_CONNECTED_SUBCHANNEL(c, acq);
}

grpc_subchannel_call *grpc_connected_subchannel_create_call(grpc_exec_ctx *exec_ctx,
                                         grpc_connected_subchannel *con,
                                         grpc_pollset *pollset) {
  grpc_channel_stack *chanstk = CHANNEL_STACK_FROM_CONNECTION(con);
  grpc_subchannel_call *call =
      gpr_malloc(sizeof(grpc_subchannel_call) + chanstk->call_stack_size);
  grpc_call_stack *callstk = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  call->connection = con;
  grpc_call_stack_init(exec_ctx, chanstk, 1, subchannel_call_destroy, call,
                       NULL, NULL, callstk);
  grpc_call_stack_set_pollset(exec_ctx, callstk, pollset);
  return call;
}

grpc_mdctx *grpc_subchannel_get_mdctx(grpc_subchannel *subchannel) {
  return subchannel->mdctx;
}

grpc_channel *grpc_subchannel_get_master(grpc_subchannel *subchannel) {
  return subchannel->master;
}

grpc_call_stack *grpc_subchannel_call_get_call_stack(
    grpc_subchannel_call *subchannel_call) {
  return SUBCHANNEL_CALL_TO_CALL_STACK(subchannel_call);
}
