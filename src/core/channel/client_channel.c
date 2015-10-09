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
#include "src/core/iomgr/iomgr.h"
#include "src/core/profiling/timers.h"
#include "src/core/support/string.h"
#include "src/core/surface/channel.h"
#include "src/core/transport/connectivity_state.h"

/* Client channel implementation */

typedef struct call_data call_data;

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

typedef enum {
  CALL_CREATED,
  CALL_WAITING_FOR_SEND,
  CALL_WAITING_FOR_CONFIG,
  CALL_WAITING_FOR_PICK,
  CALL_WAITING_FOR_CALL,
  CALL_ACTIVE,
  CALL_CANCELLED
} call_state;

struct call_data {
  /* owning element */
  grpc_call_element *elem;

  gpr_mu mu_state;

  call_state state;
  gpr_timespec deadline;
  grpc_subchannel *picked_channel;
  grpc_closure async_setup_task;
  grpc_transport_stream_op waiting_op;
  /* our child call stack */
  grpc_subchannel_call *subchannel_call;
  grpc_linked_mdelem status;
  grpc_linked_mdelem details;
};

static grpc_closure *merge_into_waiting_op(grpc_call_element *elem,
                                           grpc_transport_stream_op *new_op)
    GRPC_MUST_USE_RESULT;

static void handle_op_after_cancellation(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (op->send_ops) {
    grpc_stream_ops_unref_owned_objects(op->send_ops->ops, op->send_ops->nops);
    op->on_done_send->cb(exec_ctx, op->on_done_send->cb_arg, 0);
  }
  if (op->recv_ops) {
    char status[GPR_LTOA_MIN_BUFSIZE];
    grpc_metadata_batch mdb;
    gpr_ltoa(GRPC_STATUS_CANCELLED, status);
    calld->status.md =
        grpc_mdelem_from_strings(chand->mdctx, "grpc-status", status);
    calld->details.md =
        grpc_mdelem_from_strings(chand->mdctx, "grpc-message", "Cancelled");
    calld->status.prev = calld->details.next = NULL;
    calld->status.next = &calld->details;
    calld->details.prev = &calld->status;
    mdb.list.head = &calld->status;
    mdb.list.tail = &calld->details;
    mdb.garbage.head = mdb.garbage.tail = NULL;
    mdb.deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    grpc_sopb_add_metadata(op->recv_ops, mdb);
    *op->recv_state = GRPC_STREAM_CLOSED;
    op->on_done_recv->cb(exec_ctx, op->on_done_recv->cb_arg, 1);
  }
  if (op->on_consumed) {
    op->on_consumed->cb(exec_ctx, op->on_consumed->cb_arg, 0);
  }
}

typedef struct {
  grpc_closure closure;
  grpc_call_element *elem;
} waiting_call;

static void perform_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                        grpc_call_element *elem,
                                        grpc_transport_stream_op *op,
                                        int continuation);

static void continue_with_pick(grpc_exec_ctx *exec_ctx, void *arg,
                               int iomgr_success) {
  waiting_call *wc = arg;
  call_data *calld = wc->elem->call_data;
  perform_transport_stream_op(exec_ctx, wc->elem, &calld->waiting_op, 1);
  gpr_free(wc);
}

static void add_to_lb_policy_wait_queue_locked_state_config(
    grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  waiting_call *wc = gpr_malloc(sizeof(*wc));
  grpc_closure_init(&wc->closure, continue_with_pick, wc);
  wc->elem = elem;
  grpc_closure_list_add(&chand->waiting_for_config_closures, &wc->closure, 1);
}

static int is_empty(void *p, int len) {
  char *ptr = p;
  int i;
  for (i = 0; i < len; i++) {
    if (ptr[i] != 0) return 0;
  }
  return 1;
}

static void started_call(grpc_exec_ctx *exec_ctx, void *arg,
                         int iomgr_success) {
  call_data *calld = arg;
  grpc_transport_stream_op op;
  int have_waiting;

  gpr_mu_lock(&calld->mu_state);
  if (calld->state == CALL_CANCELLED && calld->subchannel_call != NULL) {
    memset(&op, 0, sizeof(op));
    op.cancel_with_status = GRPC_STATUS_CANCELLED;
    gpr_mu_unlock(&calld->mu_state);
    grpc_subchannel_call_process_op(exec_ctx, calld->subchannel_call, &op);
  } else if (calld->state == CALL_WAITING_FOR_CALL) {
    have_waiting = !is_empty(&calld->waiting_op, sizeof(calld->waiting_op));
    if (calld->subchannel_call != NULL) {
      calld->state = CALL_ACTIVE;
      gpr_mu_unlock(&calld->mu_state);
      if (have_waiting) {
        grpc_subchannel_call_process_op(exec_ctx, calld->subchannel_call,
                                        &calld->waiting_op);
      }
    } else {
      calld->state = CALL_CANCELLED;
      gpr_mu_unlock(&calld->mu_state);
      if (have_waiting) {
        handle_op_after_cancellation(exec_ctx, calld->elem, &calld->waiting_op);
      }
    }
  } else {
    GPR_ASSERT(calld->state == CALL_CANCELLED);
    gpr_mu_unlock(&calld->mu_state);
  }
}

static void picked_target(grpc_exec_ctx *exec_ctx, void *arg,
                          int iomgr_success) {
  call_data *calld = arg;
  grpc_pollset *pollset;

  GRPC_TIMER_BEGIN("picked_target", 0);

  if (calld->picked_channel == NULL) {
    /* treat this like a cancellation */
    calld->waiting_op.cancel_with_status = GRPC_STATUS_UNAVAILABLE;
    perform_transport_stream_op(exec_ctx, calld->elem, &calld->waiting_op, 1);
  } else {
    gpr_mu_lock(&calld->mu_state);
    if (calld->state == CALL_CANCELLED) {
      gpr_mu_unlock(&calld->mu_state);
      handle_op_after_cancellation(exec_ctx, calld->elem, &calld->waiting_op);
    } else {
      GPR_ASSERT(calld->state == CALL_WAITING_FOR_PICK);
      calld->state = CALL_WAITING_FOR_CALL;
      pollset = calld->waiting_op.bind_pollset;
      gpr_mu_unlock(&calld->mu_state);
      grpc_closure_init(&calld->async_setup_task, started_call, calld);
      grpc_subchannel_create_call(exec_ctx, calld->picked_channel, pollset,
                                  &calld->subchannel_call,
                                  &calld->async_setup_task);
    }
  }

  GRPC_TIMER_END("picked_target", 0);
}

static grpc_closure *merge_into_waiting_op(grpc_call_element *elem,
                                           grpc_transport_stream_op *new_op) {
  call_data *calld = elem->call_data;
  grpc_closure *consumed_op = NULL;
  grpc_transport_stream_op *waiting_op = &calld->waiting_op;
  GPR_ASSERT((waiting_op->send_ops != NULL) + (new_op->send_ops != NULL) <= 1);
  GPR_ASSERT((waiting_op->recv_ops != NULL) + (new_op->recv_ops != NULL) <= 1);
  if (new_op->send_ops != NULL) {
    waiting_op->send_ops = new_op->send_ops;
    waiting_op->is_last_send = new_op->is_last_send;
    waiting_op->on_done_send = new_op->on_done_send;
  }
  if (new_op->recv_ops != NULL) {
    waiting_op->recv_ops = new_op->recv_ops;
    waiting_op->recv_state = new_op->recv_state;
    waiting_op->on_done_recv = new_op->on_done_recv;
  }
  if (new_op->on_consumed != NULL) {
    if (waiting_op->on_consumed != NULL) {
      consumed_op = waiting_op->on_consumed;
    }
    waiting_op->on_consumed = new_op->on_consumed;
  }
  if (new_op->cancel_with_status != GRPC_STATUS_OK) {
    waiting_op->cancel_with_status = new_op->cancel_with_status;
  }
  return consumed_op;
}

static char *cc_get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_subchannel_call *subchannel_call;
  char *result;

  gpr_mu_lock(&calld->mu_state);
  if (calld->state == CALL_ACTIVE) {
    subchannel_call = calld->subchannel_call;
    GRPC_SUBCHANNEL_CALL_REF(subchannel_call, "get_peer");
    gpr_mu_unlock(&calld->mu_state);
    result = grpc_subchannel_call_get_peer(exec_ctx, subchannel_call);
    GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, subchannel_call, "get_peer");
    return result;
  } else {
    gpr_mu_unlock(&calld->mu_state);
    return grpc_channel_get_target(chand->master);
  }
}

static void perform_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                        grpc_call_element *elem,
                                        grpc_transport_stream_op *op,
                                        int continuation) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_subchannel_call *subchannel_call;
  grpc_lb_policy *lb_policy;
  grpc_transport_stream_op op2;
  GRPC_TIMER_BEGIN("perform_transport_stream_op", 0);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  gpr_mu_lock(&calld->mu_state);
  switch (calld->state) {
    case CALL_ACTIVE:
      GPR_ASSERT(!continuation);
      subchannel_call = calld->subchannel_call;
      gpr_mu_unlock(&calld->mu_state);
      grpc_subchannel_call_process_op(exec_ctx, subchannel_call, op);
      break;
    case CALL_CANCELLED:
      gpr_mu_unlock(&calld->mu_state);
      handle_op_after_cancellation(exec_ctx, elem, op);
      break;
    case CALL_WAITING_FOR_SEND:
      GPR_ASSERT(!continuation);
      grpc_exec_ctx_enqueue(exec_ctx, merge_into_waiting_op(elem, op), 1);
      if (!calld->waiting_op.send_ops &&
          calld->waiting_op.cancel_with_status == GRPC_STATUS_OK) {
        gpr_mu_unlock(&calld->mu_state);
        break;
      }
      *op = calld->waiting_op;
      memset(&calld->waiting_op, 0, sizeof(calld->waiting_op));
      continuation = 1;
    /* fall through */
    case CALL_WAITING_FOR_CONFIG:
    case CALL_WAITING_FOR_PICK:
    case CALL_WAITING_FOR_CALL:
      if (!continuation) {
        if (op->cancel_with_status != GRPC_STATUS_OK) {
          calld->state = CALL_CANCELLED;
          op2 = calld->waiting_op;
          memset(&calld->waiting_op, 0, sizeof(calld->waiting_op));
          if (op->on_consumed) {
            calld->waiting_op.on_consumed = op->on_consumed;
            op->on_consumed = NULL;
          } else if (op2.on_consumed) {
            calld->waiting_op.on_consumed = op2.on_consumed;
            op2.on_consumed = NULL;
          }
          gpr_mu_unlock(&calld->mu_state);
          handle_op_after_cancellation(exec_ctx, elem, op);
          handle_op_after_cancellation(exec_ctx, elem, &op2);
        } else {
          grpc_exec_ctx_enqueue(exec_ctx, merge_into_waiting_op(elem, op), 1);
          gpr_mu_unlock(&calld->mu_state);
        }
        break;
      }
    /* fall through */
    case CALL_CREATED:
      if (op->cancel_with_status != GRPC_STATUS_OK) {
        calld->state = CALL_CANCELLED;
        gpr_mu_unlock(&calld->mu_state);
        handle_op_after_cancellation(exec_ctx, elem, op);
      } else {
        calld->waiting_op = *op;

        if (op->send_ops == NULL) {
          /* need to have some send ops before we can select the
             lb target */
          calld->state = CALL_WAITING_FOR_SEND;
          gpr_mu_unlock(&calld->mu_state);
        } else {
          gpr_mu_lock(&chand->mu_config);
          lb_policy = chand->lb_policy;
          if (lb_policy) {
            grpc_transport_stream_op *waiting_op = &calld->waiting_op;
            grpc_pollset *bind_pollset = waiting_op->bind_pollset;
            grpc_metadata_batch *initial_metadata =
                &waiting_op->send_ops->ops[0].data.metadata;
            GRPC_LB_POLICY_REF(lb_policy, "pick");
            gpr_mu_unlock(&chand->mu_config);
            calld->state = CALL_WAITING_FOR_PICK;

            GPR_ASSERT(waiting_op->bind_pollset);
            GPR_ASSERT(waiting_op->send_ops);
            GPR_ASSERT(waiting_op->send_ops->nops >= 1);
            GPR_ASSERT(waiting_op->send_ops->ops[0].type == GRPC_OP_METADATA);
            gpr_mu_unlock(&calld->mu_state);

            grpc_closure_init(&calld->async_setup_task, picked_target, calld);
            grpc_lb_policy_pick(exec_ctx, lb_policy, bind_pollset,
                                initial_metadata, &calld->picked_channel,
                                &calld->async_setup_task);

            GRPC_LB_POLICY_UNREF(exec_ctx, lb_policy, "pick");
          } else if (chand->resolver != NULL) {
            calld->state = CALL_WAITING_FOR_CONFIG;
            add_to_lb_policy_wait_queue_locked_state_config(elem);
            if (!chand->started_resolving && chand->resolver != NULL) {
              GRPC_CHANNEL_INTERNAL_REF(chand->master, "resolver");
              chand->started_resolving = 1;
              grpc_resolver_next(exec_ctx, chand->resolver,
                                 &chand->incoming_configuration,
                                 &chand->on_config_changed);
            }
            gpr_mu_unlock(&chand->mu_config);
            gpr_mu_unlock(&calld->mu_state);
          } else {
            calld->state = CALL_CANCELLED;
            gpr_mu_unlock(&chand->mu_config);
            gpr_mu_unlock(&calld->mu_state);
            handle_op_after_cancellation(exec_ctx, elem, op);
          }
        }
      }
      break;
  }

  GRPC_TIMER_END("perform_transport_stream_op", 0);
}

static void cc_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         grpc_transport_stream_op *op) {
  perform_transport_stream_op(exec_ctx, elem, op, 0);
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

  if (!is_empty(op, sizeof(*op))) {
    lb_policy = chand->lb_policy;
    if (lb_policy) {
      GRPC_LB_POLICY_REF(lb_policy, "broadcast");
    }
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

/* Constructor for call_data */
static void init_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                           const void *server_transport_data,
                           grpc_transport_stream_op *initial_op) {
  call_data *calld = elem->call_data;

  /* TODO(ctiller): is there something useful we can do here? */
  GPR_ASSERT(initial_op == NULL);

  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  GPR_ASSERT(server_transport_data == NULL);
  gpr_mu_init(&calld->mu_state);
  calld->elem = elem;
  calld->state = CALL_CREATED;
  calld->deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx,
                              grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  grpc_subchannel_call *subchannel_call;

  /* if the call got activated, we need to destroy the child stack also, and
     remove it from the in-flight requests tracked by the child_entry we
     picked */
  gpr_mu_lock(&calld->mu_state);
  switch (calld->state) {
    case CALL_ACTIVE:
      subchannel_call = calld->subchannel_call;
      gpr_mu_unlock(&calld->mu_state);
      GRPC_SUBCHANNEL_CALL_UNREF(exec_ctx, subchannel_call, "client_channel");
      break;
    case CALL_CREATED:
    case CALL_CANCELLED:
      gpr_mu_unlock(&calld->mu_state);
      break;
    case CALL_WAITING_FOR_PICK:
    case CALL_WAITING_FOR_CONFIG:
    case CALL_WAITING_FOR_CALL:
    case CALL_WAITING_FOR_SEND:
      gpr_log(GPR_ERROR, "should never reach here");
      abort();
      break;
  }
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem, grpc_channel *master,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  channel_data *chand = elem->channel_data;

  memset(chand, 0, sizeof(*chand));

  GPR_ASSERT(is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  gpr_mu_init(&chand->mu_config);
  chand->mdctx = metadata_context;
  chand->master = master;
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

const grpc_channel_filter grpc_client_channel_filter = {
    cc_start_transport_stream_op, cc_start_transport_op, sizeof(call_data),
    init_call_elem, destroy_call_elem, sizeof(channel_data), init_channel_elem,
    destroy_channel_elem, cc_get_peer, "client-channel",
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
