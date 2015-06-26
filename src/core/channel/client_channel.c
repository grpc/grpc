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

#include "src/core/channel/channel_args.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/pollset_set.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

/* Client channel implementation */

typedef struct call_data call_data;

typedef struct {
  /** metadata context for this channel */
  grpc_mdctx *mdctx;
  /** resolver for this channel */
  grpc_resolver *resolver;

  /** mutex protecting client configuration, resolution state */
  gpr_mu mu_config;
  /** currently active load balancer - guarded by mu_config */
  grpc_lb_policy *lb_policy;
  /** incoming configuration - set by resolver.next
      guarded by mu_config */
  grpc_client_config *incoming_configuration;
  /** a list of closures that are all waiting for config to come in */
  grpc_iomgr_closure *waiting_for_config_closures;
  /** resolver callback */
  grpc_iomgr_closure on_config_changed;
  /** connectivity state being tracked */
  grpc_iomgr_closure *on_connectivity_state_change;
  grpc_connectivity_state *connectivity_state;
} channel_data;

typedef enum {
  CALL_CREATED,
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
  grpc_iomgr_closure async_setup_task;
  grpc_transport_stream_op waiting_op;
  /* our child call stack */
  grpc_subchannel_call *subchannel_call;
  grpc_linked_mdelem status;
  grpc_linked_mdelem details;
};

#if 0
static int prepare_activate(grpc_call_element *elem,
                            grpc_child_channel *on_child) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (calld->state == CALL_CANCELLED) return 0;

  /* no more access to calld->s.waiting allowed */
  GPR_ASSERT(calld->state == CALL_WAITING);

  if (calld->waiting_op.bind_pollset) {
    grpc_transport_setup_del_interested_party(chand->transport_setup,
                                              calld->waiting_op.bind_pollset);
  }

  calld->state = CALL_ACTIVE;

  /* create a child call */
  /* TODO(ctiller): pass the waiting op down here */
  calld->s.active.child_call =
      grpc_child_channel_create_call(on_child, elem, NULL);

  return 1;
}

static void complete_activate(grpc_call_element *elem, grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  grpc_call_element *child_elem =
      grpc_child_call_get_top_element(calld->s.active.child_call);

  GPR_ASSERT(calld->state == CALL_ACTIVE);

  /* continue the start call down the stack, this nees to happen after metadata
     are flushed*/
  child_elem->filter->start_transport_op(child_elem, op);
}

static void remove_waiting_child(channel_data *chand, call_data *calld) {
  size_t new_count;
  size_t i;
  for (i = 0, new_count = 0; i < chand->waiting_child_count; i++) {
    if (chand->waiting_children[i] == calld) {
      grpc_transport_setup_del_interested_party(
          chand->transport_setup, calld->waiting_op.bind_pollset);
      continue;
    }
    chand->waiting_children[new_count++] = chand->waiting_children[i];
  }
  GPR_ASSERT(new_count == chand->waiting_child_count - 1 ||
             new_count == chand->waiting_child_count);
  chand->waiting_child_count = new_count;
}
#endif

static void handle_op_after_cancellation(grpc_call_element *elem,
                                         grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  if (op->send_ops) {
    grpc_stream_ops_unref_owned_objects(op->send_ops->ops, op->send_ops->nops);
    op->on_done_send->cb(op->on_done_send->cb_arg, 0);
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
    mdb.deadline = gpr_inf_future;
    grpc_sopb_add_metadata(op->recv_ops, mdb);
    *op->recv_state = GRPC_STREAM_CLOSED;
    op->on_done_recv->cb(op->on_done_recv->cb_arg, 1);
  }
  if (op->on_consumed) {
    op->on_consumed->cb(op->on_consumed->cb_arg, 0);
  }
}

typedef struct {
  grpc_iomgr_closure closure;
  grpc_call_element *elem;
} waiting_call;

static void perform_transport_stream_op(grpc_call_element *elem, grpc_transport_stream_op *op, int continuation);

static void continue_with_pick(void *arg, int iomgr_success) {
  waiting_call *wc = arg;
  call_data *calld = wc->elem->call_data;
  perform_transport_stream_op(wc->elem, &calld->waiting_op, 1);
  gpr_free(wc);
}

static void add_to_lb_policy_wait_queue_locked_state_config(grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  waiting_call *wc = gpr_malloc(sizeof(*wc));
  grpc_iomgr_closure_init(&wc->closure, continue_with_pick, wc);
  wc->elem = elem;
  wc->closure.next = chand->waiting_for_config_closures;
  chand->waiting_for_config_closures = &wc->closure;
}

static int is_empty(void *p, int len) {
  char *ptr = p;
  int i;
  for (i = 0; i < len; i++) {
    if (ptr[i] != 0) return 0;
  }
  return 1;
}

static void started_call(void *arg, int iomgr_success) {
  call_data *calld = arg;
  grpc_transport_stream_op op;
  int have_waiting;

  gpr_mu_lock(&calld->mu_state);
  if (calld->state == CALL_CANCELLED && calld->subchannel_call != NULL) {
    memset(&op, 0, sizeof(op));
    op.cancel_with_status = GRPC_STATUS_CANCELLED;
    gpr_mu_unlock(&calld->mu_state);
    grpc_subchannel_call_process_op(calld->subchannel_call, &op);
  } else if (calld->state == CALL_WAITING_FOR_CALL) {
    have_waiting = !is_empty(&calld->waiting_op, sizeof(calld->waiting_op));
    if (calld->subchannel_call != NULL) {
      calld->state = CALL_ACTIVE;
      gpr_mu_unlock(&calld->mu_state);
      if (have_waiting) {
        grpc_subchannel_call_process_op(calld->subchannel_call, &calld->waiting_op);
      }
    } else {
      calld->state = CALL_CANCELLED;
      gpr_mu_unlock(&calld->mu_state);
      if (have_waiting) {
        handle_op_after_cancellation(calld->elem, &calld->waiting_op);
      }
    }
  } else {
    GPR_ASSERT(calld->state == CALL_CANCELLED);
  }
}

static void picked_target(void *arg, int iomgr_success) {
  call_data *calld = arg;
  grpc_transport_stream_op op;

  if (calld->picked_channel == NULL) {
    /* treat this like a cancellation */
    calld->waiting_op.cancel_with_status = GRPC_STATUS_UNAVAILABLE;
    perform_transport_stream_op(calld->elem, &calld->waiting_op, 1);
  } else {
    gpr_mu_lock(&calld->mu_state);
    if (calld->state == CALL_CANCELLED) {
      gpr_mu_unlock(&calld->mu_state);
      handle_op_after_cancellation(calld->elem, &calld->waiting_op);
    } else {
      GPR_ASSERT(calld->state == CALL_WAITING_FOR_PICK);
      calld->state = CALL_WAITING_FOR_CALL;
      op = calld->waiting_op;
      memset(&calld->waiting_op, 0, sizeof(calld->waiting_op));
      gpr_mu_unlock(&calld->mu_state);
      grpc_iomgr_closure_init(&calld->async_setup_task, started_call, calld);
      grpc_subchannel_create_call(calld->picked_channel, &op,
                                  &calld->subchannel_call,
                                  &calld->async_setup_task);
    }
  }
}

static void pick_target(grpc_lb_policy *lb_policy, call_data *calld) {
  grpc_metadata_batch *initial_metadata;
  grpc_transport_stream_op *op = &calld->waiting_op;

  GPR_ASSERT(op->bind_pollset);
  GPR_ASSERT(op->send_ops);
  GPR_ASSERT(op->send_ops->nops >= 1);
  GPR_ASSERT(op->send_ops->ops[0].type == GRPC_OP_METADATA);
  initial_metadata = &op->send_ops->ops[0].data.metadata;

  grpc_iomgr_closure_init(&calld->async_setup_task, picked_target, calld);
  grpc_lb_policy_pick(lb_policy, op->bind_pollset, 
    initial_metadata, &calld->picked_channel, &calld->async_setup_task);
}

static void perform_transport_stream_op(grpc_call_element *elem, grpc_transport_stream_op *op, int continuation) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_subchannel_call *subchannel_call;
  grpc_lb_policy *lb_policy;
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  gpr_mu_lock(&calld->mu_state);
  switch (calld->state) {
    case CALL_ACTIVE:
      GPR_ASSERT(!continuation);
      subchannel_call = calld->subchannel_call;
      gpr_mu_unlock(&calld->mu_state);
      grpc_subchannel_call_process_op(subchannel_call, op);
      break;
    case CALL_CANCELLED:
      gpr_mu_unlock(&calld->mu_state);
      handle_op_after_cancellation(elem, op);
      break;
    case CALL_WAITING_FOR_CONFIG:
    case CALL_WAITING_FOR_PICK:
    case CALL_WAITING_FOR_CALL:
      if (!continuation) {
        if (op->cancel_with_status != GRPC_STATUS_OK) {
          calld->state = CALL_CANCELLED;
          gpr_mu_unlock(&calld->mu_state);
          handle_op_after_cancellation(elem, op);
        } else {
          GPR_ASSERT((calld->waiting_op.send_ops == NULL) !=
                     (op->send_ops == NULL));
          GPR_ASSERT((calld->waiting_op.recv_ops == NULL) !=
                     (op->recv_ops == NULL));
          if (op->send_ops != NULL) {
            calld->waiting_op.send_ops = op->send_ops;
            calld->waiting_op.is_last_send = op->is_last_send;
            calld->waiting_op.on_done_send = op->on_done_send;
          }
          if (op->recv_ops != NULL) {
            calld->waiting_op.recv_ops = op->recv_ops;
            calld->waiting_op.recv_state = op->recv_state;
            calld->waiting_op.on_done_recv = op->on_done_recv;
          }
          gpr_mu_unlock(&calld->mu_state);
          if (op->on_consumed != NULL) {
            op->on_consumed->cb(op->on_consumed->cb_arg, 0);
          }
        }
        break;
      }
      /* fall through */
    case CALL_CREATED:
      if (op->cancel_with_status != GRPC_STATUS_OK) {
        calld->state = CALL_CANCELLED;
        gpr_mu_unlock(&calld->mu_state);
        handle_op_after_cancellation(elem, op);
      } else {
        calld->waiting_op = *op;

        gpr_mu_lock(&chand->mu_config);
        lb_policy = chand->lb_policy;
        if (lb_policy) {
          grpc_lb_policy_ref(lb_policy);
          gpr_mu_unlock(&chand->mu_config);
          calld->state = CALL_WAITING_FOR_PICK;
          gpr_mu_unlock(&calld->mu_state);

          pick_target(lb_policy, calld);

          grpc_lb_policy_unref(lb_policy);
        } else {
          calld->state = CALL_WAITING_FOR_CONFIG;
          add_to_lb_policy_wait_queue_locked_state_config(elem);
          gpr_mu_unlock(&chand->mu_config);
          gpr_mu_unlock(&calld->mu_state);
        }
      }
      break;
  }
}

static void cc_start_transport_stream_op(grpc_call_element *elem,
                                  grpc_transport_stream_op *op) {
  perform_transport_stream_op(elem, op, 0);
}

static void update_state_locked(channel_data *chand) {
  gpr_log(GPR_ERROR, "update_state_locked not implemented");
}

static void cc_on_config_changed(void *arg, int iomgr_success) {
  channel_data *chand = arg;
  grpc_lb_policy *lb_policy = NULL;
  grpc_lb_policy *old_lb_policy;
  grpc_resolver *old_resolver;
  grpc_iomgr_closure *wakeup_closures = NULL;

  if (chand->incoming_configuration) {
    lb_policy = grpc_client_config_get_lb_policy(chand->incoming_configuration);
    grpc_lb_policy_ref(lb_policy);

    grpc_client_config_unref(chand->incoming_configuration);
  }

  chand->incoming_configuration = NULL;

  gpr_mu_lock(&chand->mu_config);
  old_lb_policy = chand->lb_policy;
  chand->lb_policy = lb_policy;
  if (lb_policy != NULL) {
    wakeup_closures = chand->waiting_for_config_closures;
    chand->waiting_for_config_closures = NULL;
  }
  gpr_mu_unlock(&chand->mu_config);

  while (wakeup_closures) {
    grpc_iomgr_closure *next = wakeup_closures->next;
    grpc_iomgr_add_callback(wakeup_closures);
    wakeup_closures = next;
  }

  if (old_lb_policy) {
    grpc_lb_policy_unref(old_lb_policy);
  }

  if (iomgr_success) {
    grpc_resolver_next(chand->resolver, &chand->incoming_configuration, &chand->on_config_changed);
  } else {
    gpr_mu_lock(&chand->mu_config);
    old_resolver = chand->resolver;
    chand->resolver = NULL;
    update_state_locked(chand);
    gpr_mu_unlock(&chand->mu_config);
    grpc_resolver_unref(old_resolver);
  }
}

#if 0
static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  channel_data *chand = elem->channel_data;
  grpc_child_channel *child_channel;
  grpc_channel_op rop;
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  switch (op->type) {
    case GRPC_CHANNEL_GOAWAY:
      /* sending goaway: clear out the active child on the way through */
      gpr_mu_lock(&chand->mu);
      child_channel = chand->active_child;
      chand->active_child = NULL;
      gpr_mu_unlock(&chand->mu);
      if (child_channel) {
        grpc_child_channel_handle_op(child_channel, op);
        grpc_child_channel_destroy(child_channel, 1);
      } else {
        gpr_slice_unref(op->data.goaway.message);
      }
      break;
    case GRPC_CHANNEL_DISCONNECT:
      /* sending disconnect: clear out the active child on the way through */
      gpr_mu_lock(&chand->mu);
      child_channel = chand->active_child;
      chand->active_child = NULL;
      gpr_mu_unlock(&chand->mu);
      if (child_channel) {
        grpc_child_channel_destroy(child_channel, 1);
      }
      /* fake a transport closed to satisfy the refcounting in client */
      rop.type = GRPC_TRANSPORT_CLOSED;
      rop.dir = GRPC_CALL_UP;
      grpc_channel_next_op(elem, &rop);
      break;
    case GRPC_TRANSPORT_GOAWAY:
      /* receiving goaway: if it's from our active child, drop the active child;
         in all cases consume the event here */
      gpr_mu_lock(&chand->mu);
      child_channel = grpc_channel_stack_from_top_element(from_elem);
      if (child_channel == chand->active_child) {
        chand->active_child = NULL;
      } else {
        child_channel = NULL;
      }
      gpr_mu_unlock(&chand->mu);
      if (child_channel) {
        grpc_child_channel_destroy(child_channel, 0);
      }
      gpr_slice_unref(op->data.goaway.message);
      break;
    case GRPC_TRANSPORT_CLOSED:
      /* receiving disconnect: if it's from our active child, drop the active
         child; in all cases consume the event here */
      gpr_mu_lock(&chand->mu);
      child_channel = grpc_channel_stack_from_top_element(from_elem);
      if (child_channel == chand->active_child) {
        chand->active_child = NULL;
      } else {
        child_channel = NULL;
      }
      gpr_mu_unlock(&chand->mu);
      if (child_channel) {
        grpc_child_channel_destroy(child_channel, 0);
      }
      break;
    default:
      switch (op->dir) {
        case GRPC_CALL_UP:
          grpc_channel_next_op(elem, op);
          break;
        case GRPC_CALL_DOWN:
          gpr_log(GPR_ERROR, "unhandled channel op: %d", op->type);
          abort();
          break;
      }
      break;
  }
}
#endif

static void cc_start_transport_op(grpc_channel_element *elem, grpc_transport_op *op) {}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
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
  calld->deadline = gpr_inf_future;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
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
      grpc_subchannel_call_unref(subchannel_call);
      break;
    case CALL_CREATED:
    case CALL_CANCELLED:
      gpr_mu_unlock(&calld->mu_state);
      break;
    case CALL_WAITING_FOR_PICK:
    case CALL_WAITING_FOR_CONFIG:
    case CALL_WAITING_FOR_CALL:
      gpr_log(GPR_ERROR, "should never reach here");
      abort();
      break;
  }
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  channel_data *chand = elem->channel_data;

  memset(chand, 0, sizeof(*chand));

  GPR_ASSERT(is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  gpr_mu_init(&chand->mu_config);
  chand->mdctx = metadata_context;
  grpc_iomgr_closure_init(&chand->on_config_changed, cc_on_config_changed, chand);
}

/* Destructor for channel_data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;

  if (chand->resolver) {
    grpc_resolver_unref(chand->resolver);
  }
  if (chand->lb_policy) {
    grpc_lb_policy_unref(chand->lb_policy);
  }
  gpr_mu_destroy(&chand->mu_config);
}

const grpc_channel_filter grpc_client_channel_filter = {
    cc_start_transport_stream_op, cc_start_transport_op,           sizeof(call_data),
    init_call_elem,        destroy_call_elem,    sizeof(channel_data),
    init_channel_elem,     destroy_channel_elem, "client-channel",
};

#if 0
grpc_transport_setup_result grpc_client_channel_transport_setup_complete(
    grpc_channel_stack *channel_stack, grpc_transport *transport,
    grpc_channel_filter const **channel_filters, size_t num_channel_filters,
    grpc_mdctx *mdctx) {
  /* we just got a new transport: lets create a child channel stack for it */
  grpc_channel_element *elem = grpc_channel_stack_last_element(channel_stack);
  channel_data *chand = elem->channel_data;
  size_t num_child_filters = 2 + num_channel_filters;
  grpc_channel_filter const **child_filters;
  grpc_transport_setup_result result;
  grpc_child_channel *old_active = NULL;
  call_data **waiting_children;
  size_t waiting_child_count;
  size_t i;
  grpc_transport_stream_op *call_ops;

  /* build the child filter stack */
  child_filters = gpr_malloc(sizeof(grpc_channel_filter *) * num_child_filters);
  /* we always need a link back filter to get back to the connected channel */
  child_filters[0] = &grpc_child_channel_top_filter;
  for (i = 0; i < num_channel_filters; i++) {
    child_filters[i + 1] = channel_filters[i];
  }
  /* and we always need a connected channel to talk to the transport */
  child_filters[num_child_filters - 1] = &grpc_connected_channel_filter;

  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  /* BEGIN LOCKING CHANNEL */
  gpr_mu_lock(&chand->mu);
  chand->transport_setup_initiated = 0;

  if (chand->active_child) {
    old_active = chand->active_child;
  }
  chand->active_child = grpc_child_channel_create(
      elem, child_filters, num_child_filters, chand->args, mdctx);
  result =
      grpc_connected_channel_bind_transport(chand->active_child, transport);

  /* capture the waiting children - we'll activate them outside the lock
     to avoid re-entrancy problems */
  waiting_children = chand->waiting_children;
  waiting_child_count = chand->waiting_child_count;
  /* bumping up inflight_requests here avoids taking a lock per rpc below */

  chand->waiting_children = NULL;
  chand->waiting_child_count = 0;
  chand->waiting_child_capacity = 0;

  call_ops = gpr_malloc(sizeof(*call_ops) * waiting_child_count);

  for (i = 0; i < waiting_child_count; i++) {
    call_ops[i] = waiting_children[i]->waiting_op;
    if (!prepare_activate(waiting_children[i]->elem, chand->active_child)) {
      waiting_children[i] = NULL;
      grpc_transport_stream_op_finish_with_failure(&call_ops[i]);
    }
  }

  /* END LOCKING CHANNEL */
  gpr_mu_unlock(&chand->mu);

  /* activate any pending operations - this is safe to do as we guarantee one
     and only one write operation per request at the surface api - if we lose
     that guarantee we need to do some curly locking here */
  for (i = 0; i < waiting_child_count; i++) {
    if (waiting_children[i]) {
      complete_activate(waiting_children[i]->elem, &call_ops[i]);
    }
  }
  gpr_free(waiting_children);
  gpr_free(call_ops);
  gpr_free(child_filters);

  if (old_active) {
    grpc_child_channel_destroy(old_active, 1);
  }

  return result;
}
#endif

void grpc_client_channel_set_resolver(grpc_channel_stack *channel_stack,
                                      grpc_resolver *resolver) {
  /* post construction initialization: set the transport setup pointer */
  grpc_channel_element *elem = grpc_channel_stack_last_element(channel_stack);
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(!chand->resolver);
  chand->resolver = resolver;
  grpc_resolver_ref(resolver);
  grpc_resolver_next(resolver, &chand->incoming_configuration, &chand->on_config_changed);
}
