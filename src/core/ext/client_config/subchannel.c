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

#include "src/core/ext/client_config/subchannel.h"

#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/avl.h>

#include "src/core/ext/client_config/client_channel.h"
#include "src/core/ext/client_config/initial_connect_string.h"
#include "src/core/ext/client_config/subchannel_index.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/backoff.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/connectivity_state.h"

#define INTERNAL_REF_BITS 16
#define STRONG_REF_MASK (~(gpr_atm)((1 << INTERNAL_REF_BITS) - 1))

#define GRPC_SUBCHANNEL_MIN_CONNECT_TIMEOUT_SECONDS 20
#define GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_SUBCHANNEL_RECONNECT_JITTER 0.2

#define GET_CONNECTED_SUBCHANNEL(subchannel, barrier)      \
  ((grpc_connected_subchannel *)(gpr_atm_##barrier##_load( \
      &(subchannel)->connected_subchannel)))

typedef struct {
  grpc_closure closure;
  grpc_subchannel *subchannel;
  grpc_connectivity_state connectivity_state;
} state_watcher;

typedef struct external_state_watcher {
  grpc_subchannel *subchannel;
  grpc_pollset_set *pollset_set;
  grpc_closure *notify;
  grpc_closure closure;
  struct external_state_watcher *next;
  struct external_state_watcher *prev;
} external_state_watcher;

struct grpc_subchannel {
  grpc_connector *connector;

  /** refcount
      - lower INTERNAL_REF_BITS bits are for internal references:
        these do not keep the subchannel open.
      - upper remaining bits are for public references: these do
        keep the subchannel open */
  gpr_atm ref_pair;

  /** non-transport related channel filters */
  const grpc_channel_filter **filters;
  size_t num_filters;
  /** channel arguments */
  grpc_channel_args *args;
  /** address to connect to */
  struct sockaddr *addr;
  size_t addr_len;

  grpc_subchannel_key *key;

  /** initial string to send to peer */
  gpr_slice initial_connect_string;

  /** set during connection */
  grpc_connect_out_args connecting_result;

  /** callback for connection finishing */
  grpc_closure connected;

  /** pollset_set tracking who's interested in a connection
      being setup */
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

  external_state_watcher root_external_state_watcher;

  /** next connect attempt time */
  gpr_timespec next_attempt;
  /** backoff state */
  gpr_backoff backoff_state;
  /** do we have an active alarm? */
  int have_alarm;
  /** our alarm */
  grpc_timer alarm;
};

struct grpc_subchannel_call {
  grpc_connected_subchannel *connection;
};

#define SUBCHANNEL_CALL_TO_CALL_STACK(call) ((grpc_call_stack *)((call) + 1))
#define CHANNEL_STACK_FROM_CONNECTION(con) ((grpc_channel_stack *)(con))
#define CALLSTACK_TO_SUBCHANNEL_CALL(callstack) \
  (((grpc_subchannel_call *)(callstack)) - 1)

static void subchannel_connected(grpc_exec_ctx *exec_ctx, void *subchannel,
                                 grpc_error *error);

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define REF_REASON reason
#define REF_LOG(name, p)                                                  \
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "%s: %p   ref %d -> %d %s", \
          (name), (p), (p)->refs.count, (p)->refs.count + 1, reason)
#define UNREF_LOG(name, p)                                                \
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG, "%s: %p unref %d -> %d %s", \
          (name), (p), (p)->refs.count, (p)->refs.count - 1, reason)
#define REF_MUTATE_EXTRA_ARGS \
  GRPC_SUBCHANNEL_REF_EXTRA_ARGS, const char *purpose
#define REF_MUTATE_PURPOSE(x) , file, line, reason, x
#else
#define REF_REASON ""
#define REF_LOG(name, p) \
  do {                   \
  } while (0)
#define UNREF_LOG(name, p) \
  do {                     \
  } while (0)
#define REF_MUTATE_EXTRA_ARGS
#define REF_MUTATE_PURPOSE(x)
#endif

/*
 * connection implementation
 */

static void connection_destroy(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  grpc_connected_subchannel *c = arg;
  grpc_channel_stack_destroy(exec_ctx, CHANNEL_STACK_FROM_CONNECTION(c));
  gpr_free(c);
}

void grpc_connected_subchannel_ref(
    grpc_connected_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CHANNEL_STACK_REF(CHANNEL_STACK_FROM_CONNECTION(c), REF_REASON);
}

void grpc_connected_subchannel_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_connected_subchannel *c
                                         GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CHANNEL_STACK_UNREF(exec_ctx, CHANNEL_STACK_FROM_CONNECTION(c),
                           REF_REASON);
}

/*
 * grpc_subchannel implementation
 */

static void subchannel_destroy(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  grpc_subchannel *c = arg;
  gpr_free((void *)c->filters);
  grpc_channel_args_destroy(c->args);
  gpr_free(c->addr);
  gpr_slice_unref(c->initial_connect_string);
  grpc_connectivity_state_destroy(exec_ctx, &c->state_tracker);
  grpc_connector_unref(exec_ctx, c->connector);
  grpc_pollset_set_destroy(c->pollset_set);
  grpc_subchannel_key_destroy(exec_ctx, c->key);
  gpr_free(c);
}

static gpr_atm ref_mutate(grpc_subchannel *c, gpr_atm delta,
                          int barrier REF_MUTATE_EXTRA_ARGS) {
  gpr_atm old_val = barrier ? gpr_atm_full_fetch_add(&c->ref_pair, delta)
                            : gpr_atm_no_barrier_fetch_add(&c->ref_pair, delta);
#ifdef GRPC_STREAM_REFCOUNT_DEBUG
  gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
          "SUBCHANNEL: %p % 12s 0x%08x -> 0x%08x [%s]", c, purpose, old_val,
          old_val + delta, reason);
#endif
  return old_val;
}

grpc_subchannel *grpc_subchannel_ref(
    grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, (1 << INTERNAL_REF_BITS),
                        0 REF_MUTATE_PURPOSE("STRONG_REF"));
  GPR_ASSERT((old_refs & STRONG_REF_MASK) != 0);
  return c;
}

grpc_subchannel *grpc_subchannel_weak_ref(
    grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, 1, 0 REF_MUTATE_PURPOSE("WEAK_REF"));
  GPR_ASSERT(old_refs != 0);
  return c;
}

grpc_subchannel *grpc_subchannel_ref_from_weak_ref(
    grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  if (!c) return NULL;
  for (;;) {
    gpr_atm old_refs = gpr_atm_acq_load(&c->ref_pair);
    if (old_refs >= (1 << INTERNAL_REF_BITS)) {
      gpr_atm new_refs = old_refs + (1 << INTERNAL_REF_BITS);
      if (gpr_atm_rel_cas(&c->ref_pair, old_refs, new_refs)) {
        return c;
      }
    } else {
      return NULL;
    }
  }
}

static void disconnect(grpc_exec_ctx *exec_ctx, grpc_subchannel *c) {
  grpc_connected_subchannel *con;
  grpc_subchannel_index_unregister(exec_ctx, c->key, c);
  gpr_mu_lock(&c->mu);
  GPR_ASSERT(!c->disconnected);
  c->disconnected = 1;
  grpc_connector_shutdown(exec_ctx, c->connector);
  con = GET_CONNECTED_SUBCHANNEL(c, no_barrier);
  if (con != NULL) {
    GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, con, "connection");
    gpr_atm_no_barrier_store(&c->connected_subchannel, (gpr_atm)0xdeadbeef);
  }
  gpr_mu_unlock(&c->mu);
}

void grpc_subchannel_unref(grpc_exec_ctx *exec_ctx,
                           grpc_subchannel *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, (gpr_atm)1 - (gpr_atm)(1 << INTERNAL_REF_BITS),
                        1 REF_MUTATE_PURPOSE("STRONG_UNREF"));
  if ((old_refs & STRONG_REF_MASK) == (1 << INTERNAL_REF_BITS)) {
    disconnect(exec_ctx, c);
  }
  GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, c, "strong-unref");
}

void grpc_subchannel_weak_unref(grpc_exec_ctx *exec_ctx,
                                grpc_subchannel *c
                                    GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = ref_mutate(c, -(gpr_atm)1, 1 REF_MUTATE_PURPOSE("WEAK_UNREF"));
  if (old_refs == 1) {
    grpc_exec_ctx_sched(exec_ctx, grpc_closure_create(subchannel_destroy, c),
                        GRPC_ERROR_NONE, NULL);
  }
}

grpc_subchannel *grpc_subchannel_create(grpc_exec_ctx *exec_ctx,
                                        grpc_connector *connector,
                                        grpc_subchannel_args *args) {
  grpc_subchannel_key *key = grpc_subchannel_key_create(connector, args);
  grpc_subchannel *c = grpc_subchannel_index_find(exec_ctx, key);
  if (c) {
    grpc_subchannel_key_destroy(exec_ctx, key);
    return c;
  }

  c = gpr_malloc(sizeof(*c));
  memset(c, 0, sizeof(*c));
  c->key = key;
  gpr_atm_no_barrier_store(&c->ref_pair, 1 << INTERNAL_REF_BITS);
  c->connector = connector;
  grpc_connector_ref(c->connector);
  c->num_filters = args->filter_count;
  if (c->num_filters > 0) {
    c->filters = gpr_malloc(sizeof(grpc_channel_filter *) * c->num_filters);
    memcpy((void *)c->filters, args->filters,
           sizeof(grpc_channel_filter *) * c->num_filters);
  } else {
    c->filters = NULL;
  }
  c->addr = gpr_malloc(args->addr_len);
  if (args->addr_len) memcpy(c->addr, args->addr, args->addr_len);
  c->pollset_set = grpc_pollset_set_create();
  c->addr_len = args->addr_len;
  grpc_set_initial_connect_string(&c->addr, &c->addr_len,
                                  &c->initial_connect_string);
  c->args = grpc_channel_args_copy(args->args);
  c->root_external_state_watcher.next = c->root_external_state_watcher.prev =
      &c->root_external_state_watcher;
  grpc_closure_init(&c->connected, subchannel_connected, c);
  grpc_connectivity_state_init(&c->state_tracker, GRPC_CHANNEL_IDLE,
                               "subchannel");
  int initial_backoff_ms =
      GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS * 1000;
  int max_backoff_ms = GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS * 1000;
  bool fixed_reconnect_backoff = false;
  if (c->args) {
    for (size_t i = 0; i < c->args->num_args; i++) {
      if (0 == strcmp(c->args->args[i].key,
                      "grpc.testing.fixed_reconnect_backoff")) {
        GPR_ASSERT(c->args->args[i].type == GRPC_ARG_INTEGER);
        fixed_reconnect_backoff = true;
        initial_backoff_ms = max_backoff_ms = grpc_channel_arg_get_integer(
            &c->args->args[i],
            (grpc_integer_options){initial_backoff_ms, 100, INT_MAX});
      } else if (0 == strcmp(c->args->args[i].key,
                             GRPC_ARG_MAX_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        max_backoff_ms = grpc_channel_arg_get_integer(
            &c->args->args[i],
            (grpc_integer_options){max_backoff_ms, 100, INT_MAX});
      } else if (0 == strcmp(c->args->args[i].key,
                             GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        initial_backoff_ms = grpc_channel_arg_get_integer(
            &c->args->args[i],
            (grpc_integer_options){initial_backoff_ms, 100, INT_MAX});
      }
    }
  }
  gpr_backoff_init(
      &c->backoff_state,
      fixed_reconnect_backoff ? 1.0
                              : GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER,
      fixed_reconnect_backoff ? 0.0 : GRPC_SUBCHANNEL_RECONNECT_JITTER,
      initial_backoff_ms, max_backoff_ms);
  gpr_mu_init(&c->mu);

  return grpc_subchannel_index_register(exec_ctx, key, c);
}

static void continue_connect(grpc_exec_ctx *exec_ctx, grpc_subchannel *c) {
  grpc_connect_in_args args;

  args.interested_parties = c->pollset_set;
  args.addr = c->addr;
  args.addr_len = c->addr_len;
  args.deadline = c->next_attempt;
  args.channel_args = c->args;
  args.initial_connect_string = c->initial_connect_string;

  grpc_connectivity_state_set(exec_ctx, &c->state_tracker,
                              GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE,
                              "state_change");
  grpc_connector_connect(exec_ctx, c->connector, &args, &c->connecting_result,
                         &c->connected);
}

static void start_connect(grpc_exec_ctx *exec_ctx, grpc_subchannel *c) {
  c->next_attempt =
      gpr_backoff_begin(&c->backoff_state, gpr_now(GPR_CLOCK_MONOTONIC));
  continue_connect(exec_ctx, c);
}

grpc_connectivity_state grpc_subchannel_check_connectivity(grpc_subchannel *c,
                                                           grpc_error **error) {
  grpc_connectivity_state state;
  gpr_mu_lock(&c->mu);
  state = grpc_connectivity_state_check(&c->state_tracker, error);
  gpr_mu_unlock(&c->mu);
  return state;
}

static void on_external_state_watcher_done(grpc_exec_ctx *exec_ctx, void *arg,
                                           grpc_error *error) {
  external_state_watcher *w = arg;
  grpc_closure *follow_up = w->notify;
  if (w->pollset_set != NULL) {
    grpc_pollset_set_del_pollset_set(exec_ctx, w->subchannel->pollset_set,
                                     w->pollset_set);
  }
  gpr_mu_lock(&w->subchannel->mu);
  w->next->prev = w->prev;
  w->prev->next = w->next;
  gpr_mu_unlock(&w->subchannel->mu);
  GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, w->subchannel, "external_state_watcher");
  gpr_free(w);
  follow_up->cb(exec_ctx, follow_up->cb_arg, error);
}

void grpc_subchannel_notify_on_state_change(
    grpc_exec_ctx *exec_ctx, grpc_subchannel *c,
    grpc_pollset_set *interested_parties, grpc_connectivity_state *state,
    grpc_closure *notify) {
  external_state_watcher *w;

  if (state == NULL) {
    gpr_mu_lock(&c->mu);
    for (w = c->root_external_state_watcher.next;
         w != &c->root_external_state_watcher; w = w->next) {
      if (w->notify == notify) {
        grpc_connectivity_state_notify_on_state_change(
            exec_ctx, &c->state_tracker, NULL, &w->closure);
      }
    }
    gpr_mu_unlock(&c->mu);
  } else {
    w = gpr_malloc(sizeof(*w));
    w->subchannel = c;
    w->pollset_set = interested_parties;
    w->notify = notify;
    grpc_closure_init(&w->closure, on_external_state_watcher_done, w);
    if (interested_parties != NULL) {
      grpc_pollset_set_add_pollset_set(exec_ctx, c->pollset_set,
                                       interested_parties);
    }
    GRPC_SUBCHANNEL_WEAK_REF(c, "external_state_watcher");
    gpr_mu_lock(&c->mu);
    w->next = &c->root_external_state_watcher;
    w->prev = w->next->prev;
    w->next->prev = w->prev->next = w;
    if (grpc_connectivity_state_notify_on_state_change(
            exec_ctx, &c->state_tracker, state, &w->closure)) {
      c->connecting = 1;
      /* released by connection */
      GRPC_SUBCHANNEL_WEAK_REF(c, "connecting");
      start_connect(exec_ctx, c);
    }
    gpr_mu_unlock(&c->mu);
  }
}

void grpc_connected_subchannel_process_transport_op(
    grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *con,
    grpc_transport_op *op) {
  grpc_channel_stack *channel_stack = CHANNEL_STACK_FROM_CONNECTION(con);
  grpc_channel_element *top_elem = grpc_channel_stack_element(channel_stack, 0);
  top_elem->filter->start_transport_op(exec_ctx, top_elem, op);
}

static void subchannel_on_child_state_changed(grpc_exec_ctx *exec_ctx, void *p,
                                              grpc_error *error) {
  state_watcher *sw = p;
  grpc_subchannel *c = sw->subchannel;
  gpr_mu *mu = &c->mu;

  gpr_mu_lock(mu);

  /* if we failed just leave this closure */
  if (sw->connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    /* any errors on a subchannel ==> we're done, create a new one */
    sw->connectivity_state = GRPC_CHANNEL_SHUTDOWN;
  }
  grpc_connectivity_state_set(exec_ctx, &c->state_tracker,
                              sw->connectivity_state, GRPC_ERROR_REF(error),
                              "reflect_child");
  if (sw->connectivity_state != GRPC_CHANNEL_SHUTDOWN) {
    grpc_connected_subchannel_notify_on_state_change(
        exec_ctx, GET_CONNECTED_SUBCHANNEL(c, no_barrier), NULL,
        &sw->connectivity_state, &sw->closure);
    GRPC_SUBCHANNEL_WEAK_REF(c, "state_watcher");
    sw = NULL;
  }

  gpr_mu_unlock(mu);
  GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, c, "state_watcher");
  gpr_free(sw);
}

static void connected_subchannel_state_op(grpc_exec_ctx *exec_ctx,
                                          grpc_connected_subchannel *con,
                                          grpc_pollset_set *interested_parties,
                                          grpc_connectivity_state *state,
                                          grpc_closure *closure) {
  grpc_transport_op op;
  grpc_channel_element *elem;
  memset(&op, 0, sizeof(op));
  op.connectivity_state = state;
  op.on_connectivity_state_change = closure;
  op.bind_pollset_set = interested_parties;
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CONNECTION(con), 0);
  elem->filter->start_transport_op(exec_ctx, elem, &op);
}

void grpc_connected_subchannel_notify_on_state_change(
    grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *con,
    grpc_pollset_set *interested_parties, grpc_connectivity_state *state,
    grpc_closure *closure) {
  connected_subchannel_state_op(exec_ctx, con, interested_parties, state,
                                closure);
}

void grpc_connected_subchannel_ping(grpc_exec_ctx *exec_ctx,
                                    grpc_connected_subchannel *con,
                                    grpc_closure *closure) {
  grpc_transport_op op;
  grpc_channel_element *elem;
  memset(&op, 0, sizeof(op));
  op.send_ping = closure;
  elem = grpc_channel_stack_element(CHANNEL_STACK_FROM_CONNECTION(con), 0);
  elem->filter->start_transport_op(exec_ctx, elem, &op);
}

static void publish_transport_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_subchannel *c) {
  grpc_connected_subchannel *con;
  grpc_channel_stack *stk;
  state_watcher *sw_subchannel;

  /* construct channel stack */
  grpc_channel_stack_builder *builder = grpc_channel_stack_builder_create();
  grpc_channel_stack_builder_set_channel_arguments(
      builder, c->connecting_result.channel_args);
  grpc_channel_stack_builder_set_transport(builder,
                                           c->connecting_result.transport);

  if (grpc_channel_init_create_stack(exec_ctx, builder,
                                     GRPC_CLIENT_SUBCHANNEL)) {
    con = grpc_channel_stack_builder_finish(exec_ctx, builder, 0, 1,
                                            connection_destroy, NULL);
  } else {
    grpc_channel_stack_builder_destroy(builder);
    abort(); /* TODO(ctiller): what to do here (previously we just crashed) */
  }
  stk = CHANNEL_STACK_FROM_CONNECTION(con);
  memset(&c->connecting_result, 0, sizeof(c->connecting_result));

  /* initialize state watcher */
  sw_subchannel = gpr_malloc(sizeof(*sw_subchannel));
  sw_subchannel->subchannel = c;
  sw_subchannel->connectivity_state = GRPC_CHANNEL_READY;
  grpc_closure_init(&sw_subchannel->closure, subchannel_on_child_state_changed,
                    sw_subchannel);

  if (c->disconnected) {
    gpr_free(sw_subchannel);
    grpc_channel_stack_destroy(exec_ctx, stk);
    gpr_free(con);
    GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, c, "connecting");
    return;
  }

  /* publish */
  /* TODO(ctiller): this full barrier seems to clear up a TSAN failure.
                    I'd have expected the rel_cas below to be enough, but
                    seemingly it's not.
                    Re-evaluate if we really need this. */
  gpr_atm_full_barrier();
  GPR_ASSERT(gpr_atm_rel_cas(&c->connected_subchannel, 0, (gpr_atm)con));
  c->connecting = 0;

  /* setup subchannel watching connected subchannel for changes; subchannel
     ref
     for connecting is donated
     to the state watcher */
  GRPC_SUBCHANNEL_WEAK_REF(c, "state_watcher");
  GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, c, "connecting");
  grpc_connected_subchannel_notify_on_state_change(
      exec_ctx, con, c->pollset_set, &sw_subchannel->connectivity_state,
      &sw_subchannel->closure);

  /* signal completion */
  grpc_connectivity_state_set(exec_ctx, &c->state_tracker, GRPC_CHANNEL_READY,
                              GRPC_ERROR_NONE, "connected");
}

static void on_alarm(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_subchannel *c = arg;
  gpr_mu_lock(&c->mu);
  c->have_alarm = 0;
  if (c->disconnected) {
    error = GRPC_ERROR_CREATE_REFERENCING("Disconnected", &error, 1);
  } else {
    GRPC_ERROR_REF(error);
  }
  if (error == GRPC_ERROR_NONE) {
    gpr_log(GPR_INFO, "Failed to connect to channel, retrying");
    c->next_attempt =
        gpr_backoff_step(&c->backoff_state, gpr_now(GPR_CLOCK_MONOTONIC));
    continue_connect(exec_ctx, c);
    gpr_mu_unlock(&c->mu);
  } else {
    gpr_mu_unlock(&c->mu);
    GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, c, "connecting");
  }
  GRPC_ERROR_UNREF(error);
}

static void subchannel_connected(grpc_exec_ctx *exec_ctx, void *arg,
                                 grpc_error *error) {
  grpc_subchannel *c = arg;
  grpc_channel_args *delete_channel_args = c->connecting_result.channel_args;

  GRPC_SUBCHANNEL_WEAK_REF(c, "connected");
  gpr_mu_lock(&c->mu);
  if (c->connecting_result.transport != NULL) {
    publish_transport_locked(exec_ctx, c);
  } else if (c->disconnected) {
    GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, c, "connecting");
  } else {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    GPR_ASSERT(!c->have_alarm);
    c->have_alarm = 1;
    grpc_connectivity_state_set(
        exec_ctx, &c->state_tracker, GRPC_CHANNEL_TRANSIENT_FAILURE,
        GRPC_ERROR_CREATE_REFERENCING("Connect Failed", &error, 1),
        "connect_failed");
    gpr_timespec time_til_next = gpr_time_sub(c->next_attempt, now);
    const char *errmsg = grpc_error_string(error);
    gpr_log(GPR_INFO, "Connect failed: %s", errmsg);
    if (gpr_time_cmp(time_til_next, gpr_time_0(time_til_next.clock_type)) <=
        0) {
      gpr_log(GPR_INFO, "Retry immediately");
    } else {
      gpr_log(GPR_INFO, "Retry in %" PRId64 ".%09d seconds",
              time_til_next.tv_sec, time_til_next.tv_nsec);
    }
    grpc_timer_init(exec_ctx, &c->alarm, c->next_attempt, on_alarm, c, now);
    grpc_error_free_string(errmsg);
  }
  gpr_mu_unlock(&c->mu);
  GRPC_SUBCHANNEL_WEAK_UNREF(exec_ctx, c, "connecting");
  grpc_channel_args_destroy(delete_channel_args);
}

/*
 * grpc_subchannel_call implementation
 */

static void subchannel_call_destroy(grpc_exec_ctx *exec_ctx, void *call,
                                    grpc_error *error) {
  grpc_subchannel_call *c = call;
  GPR_TIMER_BEGIN("grpc_subchannel_call_unref.destroy", 0);
  grpc_connected_subchannel *connection = c->connection;
  grpc_call_stack_destroy(exec_ctx, SUBCHANNEL_CALL_TO_CALL_STACK(c), NULL, c);
  GRPC_CONNECTED_SUBCHANNEL_UNREF(exec_ctx, connection, "subchannel_call");
  GPR_TIMER_END("grpc_subchannel_call_unref.destroy", 0);
}

void grpc_subchannel_call_ref(
    grpc_subchannel_call *c GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(c), REF_REASON);
}

void grpc_subchannel_call_unref(grpc_exec_ctx *exec_ctx,
                                grpc_subchannel_call *c
                                    GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  GRPC_CALL_STACK_UNREF(exec_ctx, SUBCHANNEL_CALL_TO_CALL_STACK(c), REF_REASON);
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
  GPR_TIMER_BEGIN("grpc_subchannel_call_process_op", 0);
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  top_elem->filter->start_transport_stream_op(exec_ctx, top_elem, op);
  GPR_TIMER_END("grpc_subchannel_call_process_op", 0);
}

grpc_connected_subchannel *grpc_subchannel_get_connected_subchannel(
    grpc_subchannel *c) {
  return GET_CONNECTED_SUBCHANNEL(c, acq);
}

grpc_subchannel_call *grpc_connected_subchannel_create_call(
    grpc_exec_ctx *exec_ctx, grpc_connected_subchannel *con,
    grpc_polling_entity *pollent) {
  grpc_channel_stack *chanstk = CHANNEL_STACK_FROM_CONNECTION(con);
  grpc_subchannel_call *call =
      gpr_malloc(sizeof(grpc_subchannel_call) + chanstk->call_stack_size);
  grpc_call_stack *callstk = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  call->connection = con;
  GRPC_CONNECTED_SUBCHANNEL_REF(con, "subchannel_call");
  grpc_call_stack_init(exec_ctx, chanstk, 1, subchannel_call_destroy, call,
                       NULL, NULL, callstk);
  grpc_call_stack_set_pollset_or_pollset_set(exec_ctx, callstk, pollent);
  return call;
}

grpc_call_stack *grpc_subchannel_call_get_call_stack(
    grpc_subchannel_call *subchannel_call) {
  return SUBCHANNEL_CALL_TO_CALL_STACK(subchannel_call);
}
