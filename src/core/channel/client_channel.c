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

#include "src/core/channel/channel_args.h"
#include "src/core/channel/child_channel.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/channel/metadata_buffer.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

/* Client channel implementation */

typedef struct call_data call_data;

typedef struct {
  /* protects children, child_count, child_capacity, active_child,
     transport_setup_initiated
     does not protect channel stacks held by children
     transport_setup is assumed to be set once during construction */
  gpr_mu mu;

  /* the sending child (may be null) */
  grpc_child_channel *active_child;

  /* calls waiting for a channel to be ready */
  call_data **waiting_children;
  size_t waiting_child_count;
  size_t waiting_child_capacity;

  /* transport setup for this channel */
  grpc_transport_setup *transport_setup;
  int transport_setup_initiated;

  grpc_channel_args *args;

  /* metadata cache */
  grpc_mdelem *cancel_status;
} channel_data;

typedef enum {
  CALL_CREATED,
  CALL_WAITING,
  CALL_ACTIVE,
  CALL_CANCELLED
} call_state;

struct call_data {
  /* owning element */
  grpc_call_element *elem;

  call_state state;
  grpc_metadata_buffer pending_metadata;
  gpr_timespec deadline;
  union {
    struct {
      /* our child call stack */
      grpc_child_call *child_call;
    } active;
    struct {
      void (*on_complete)(void *user_data, grpc_op_error error);
      void *on_complete_user_data;
      gpr_uint32 start_flags;
      grpc_pollset *pollset;
    } waiting;
  } s;
};

static int prepare_activate(grpc_call_element *elem,
                            grpc_child_channel *on_child) {
  call_data *calld = elem->call_data;
  if (calld->state == CALL_CANCELLED) return 0;

  /* no more access to calld->s.waiting allowed */
  GPR_ASSERT(calld->state == CALL_WAITING);
  calld->state = CALL_ACTIVE;

  /* create a child call */
  calld->s.active.child_call = grpc_child_channel_create_call(on_child, elem);

  return 1;
}

static void do_nothing(void *ignored, grpc_op_error error) {}

static void complete_activate(grpc_call_element *elem, grpc_call_op *op) {
  call_data *calld = elem->call_data;
  grpc_call_element *child_elem =
      grpc_child_call_get_top_element(calld->s.active.child_call);

  GPR_ASSERT(calld->state == CALL_ACTIVE);

  /* sending buffered metadata down the stack before the start call */
  grpc_metadata_buffer_flush(&calld->pending_metadata, child_elem);

  if (gpr_time_cmp(calld->deadline, gpr_inf_future) != 0) {
    grpc_call_op dop;
    dop.type = GRPC_SEND_DEADLINE;
    dop.dir = GRPC_CALL_DOWN;
    dop.flags = 0;
    dop.data.deadline = calld->deadline;
    dop.done_cb = do_nothing;
    dop.user_data = NULL;
    child_elem->filter->call_op(child_elem, elem, &dop);
  }

  /* continue the start call down the stack, this nees to happen after metadata
     are flushed*/
  child_elem->filter->call_op(child_elem, elem, op);
}

static void start_rpc(grpc_call_element *elem, grpc_call_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  gpr_mu_lock(&chand->mu);
  if (calld->state == CALL_CANCELLED) {
    gpr_mu_unlock(&chand->mu);
    op->done_cb(op->user_data, GRPC_OP_ERROR);
    return;
  }
  GPR_ASSERT(calld->state == CALL_CREATED);
  calld->state = CALL_WAITING;
  if (chand->active_child) {
    /* channel is connected - use the connected stack */
    if (prepare_activate(elem, chand->active_child)) {
      gpr_mu_unlock(&chand->mu);
      /* activate the request (pass it down) outside the lock */
      complete_activate(elem, op);
    } else {
      gpr_mu_unlock(&chand->mu);
    }
  } else {
    /* check to see if we should initiate a connection (if we're not already),
       but don't do so until outside the lock to avoid re-entrancy problems if
       the callback is immediate */
    int initiate_transport_setup = 0;
    if (!chand->transport_setup_initiated) {
      chand->transport_setup_initiated = 1;
      initiate_transport_setup = 1;
    }
    /* add this call to the waiting set to be resumed once we have a child
       channel stack, growing the waiting set if needed */
    if (chand->waiting_child_count == chand->waiting_child_capacity) {
      chand->waiting_child_capacity =
          GPR_MAX(chand->waiting_child_capacity * 2, 8);
      chand->waiting_children =
          gpr_realloc(chand->waiting_children,
                      chand->waiting_child_capacity * sizeof(call_data *));
    }
    calld->s.waiting.on_complete = op->done_cb;
    calld->s.waiting.on_complete_user_data = op->user_data;
    calld->s.waiting.start_flags = op->flags;
    calld->s.waiting.pollset = op->data.start.pollset;
    chand->waiting_children[chand->waiting_child_count++] = calld;
    gpr_mu_unlock(&chand->mu);

    /* finally initiate transport setup if needed */
    if (initiate_transport_setup) {
      grpc_transport_setup_initiate(chand->transport_setup);
    }
  }
}

static void remove_waiting_child(channel_data *chand, call_data *calld) {
  size_t new_count;
  size_t i;
  for (i = 0, new_count = 0; i < chand->waiting_child_count; i++) {
    if (chand->waiting_children[i] == calld) continue;
    chand->waiting_children[new_count++] = chand->waiting_children[i];
  }
  GPR_ASSERT(new_count == chand->waiting_child_count - 1 ||
             new_count == chand->waiting_child_count);
  chand->waiting_child_count = new_count;
}

static void send_up_cancelled_ops(grpc_call_element *elem) {
  grpc_call_op finish_op;
  channel_data *chand = elem->channel_data;
  /* send up a synthesized status */
  finish_op.type = GRPC_RECV_METADATA;
  finish_op.dir = GRPC_CALL_UP;
  finish_op.flags = 0;
  finish_op.data.metadata = grpc_mdelem_ref(chand->cancel_status);
  finish_op.done_cb = do_nothing;
  finish_op.user_data = NULL;
  grpc_call_next_op(elem, &finish_op);
  /* send up a finish */
  finish_op.type = GRPC_RECV_FINISH;
  finish_op.dir = GRPC_CALL_UP;
  finish_op.flags = 0;
  finish_op.done_cb = do_nothing;
  finish_op.user_data = NULL;
  grpc_call_next_op(elem, &finish_op);
}

static void cancel_rpc(grpc_call_element *elem, grpc_call_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_call_element *child_elem;

  gpr_mu_lock(&chand->mu);
  switch (calld->state) {
    case CALL_ACTIVE:
      child_elem = grpc_child_call_get_top_element(calld->s.active.child_call);
      gpr_mu_unlock(&chand->mu);
      child_elem->filter->call_op(child_elem, elem, op);
      return; /* early out */
    case CALL_WAITING:
      remove_waiting_child(chand, calld);
      calld->state = CALL_CANCELLED;
      gpr_mu_unlock(&chand->mu);
      send_up_cancelled_ops(elem);
      calld->s.waiting.on_complete(calld->s.waiting.on_complete_user_data,
                                   GRPC_OP_ERROR);
      return; /* early out */
    case CALL_CREATED:
      calld->state = CALL_CANCELLED;
      gpr_mu_unlock(&chand->mu);
      send_up_cancelled_ops(elem);
      return; /* early out */
    case CALL_CANCELLED:
      gpr_mu_unlock(&chand->mu);
      return; /* early out */
  }
  gpr_log(GPR_ERROR, "should never reach here");
  abort();
}

static void call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                    grpc_call_op *op) {
  call_data *calld = elem->call_data;
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  switch (op->type) {
    case GRPC_SEND_METADATA:
      grpc_metadata_buffer_queue(&calld->pending_metadata, op);
      break;
    case GRPC_SEND_DEADLINE:
      calld->deadline = op->data.deadline;
      op->done_cb(op->user_data, GRPC_OP_OK);
      break;
    case GRPC_SEND_START:
      /* filter out the start event to find which child to send on */
      start_rpc(elem, op);
      break;
    case GRPC_CANCEL_OP:
      cancel_rpc(elem, op);
      break;
    case GRPC_SEND_MESSAGE:
    case GRPC_SEND_FINISH:
    case GRPC_REQUEST_DATA:
      if (calld->state == CALL_ACTIVE) {
        grpc_call_element *child_elem =
            grpc_child_call_get_top_element(calld->s.active.child_call);
        child_elem->filter->call_op(child_elem, elem, op);
      } else {
        op->done_cb(op->user_data, GRPC_OP_ERROR);
      }
      break;
    default:
      GPR_ASSERT(op->dir == GRPC_CALL_UP);
      grpc_call_next_op(elem, op);
      break;
  }
}

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

static void error_bad_on_complete(void *arg, grpc_op_error error) {
  gpr_log(GPR_ERROR,
          "Waiting finished but not started? Bad on_complete callback");
  abort();
}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data) {
  call_data *calld = elem->call_data;

  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);
  GPR_ASSERT(server_transport_data == NULL);
  calld->elem = elem;
  calld->state = CALL_CREATED;
  calld->deadline = gpr_inf_future;
  calld->s.waiting.on_complete = error_bad_on_complete;
  calld->s.waiting.on_complete_user_data = NULL;
  grpc_metadata_buffer_init(&calld->pending_metadata);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  call_data *calld = elem->call_data;

  /* if the metadata buffer is not flushed, destroy it here. */
  grpc_metadata_buffer_destroy(&calld->pending_metadata, GRPC_OP_OK);
  /* if the call got activated, we need to destroy the child stack also, and
     remove it from the in-flight requests tracked by the child_entry we
     picked */
  if (calld->state == CALL_ACTIVE) {
    grpc_child_call_destroy(calld->s.active.child_call);
  }
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  channel_data *chand = elem->channel_data;
  char temp[GPR_LTOA_MIN_BUFSIZE];

  GPR_ASSERT(!is_first);
  GPR_ASSERT(is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  gpr_mu_init(&chand->mu);
  chand->active_child = NULL;
  chand->waiting_children = NULL;
  chand->waiting_child_count = 0;
  chand->waiting_child_capacity = 0;
  chand->transport_setup = NULL;
  chand->transport_setup_initiated = 0;
  chand->args = grpc_channel_args_copy(args);

  gpr_ltoa(GRPC_STATUS_CANCELLED, temp);
  chand->cancel_status =
      grpc_mdelem_from_strings(metadata_context, "grpc-status", temp);
}

/* Destructor for channel_data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;

  grpc_transport_setup_cancel(chand->transport_setup);

  if (chand->active_child) {
    grpc_child_channel_destroy(chand->active_child, 1);
    chand->active_child = NULL;
  }

  grpc_channel_args_destroy(chand->args);
  grpc_mdelem_unref(chand->cancel_status);

  gpr_mu_destroy(&chand->mu);
  GPR_ASSERT(chand->waiting_child_count == 0);
  gpr_free(chand->waiting_children);
}

const grpc_channel_filter grpc_client_channel_filter = {
    call_op,           channel_op,           sizeof(call_data),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "client-channel", };

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
  grpc_call_op *call_ops;

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

  call_ops = gpr_malloc(sizeof(grpc_call_op) * waiting_child_count);

  for (i = 0; i < waiting_child_count; i++) {
    call_ops[i].type = GRPC_SEND_START;
    call_ops[i].dir = GRPC_CALL_DOWN;
    call_ops[i].flags = waiting_children[i]->s.waiting.start_flags;
    call_ops[i].done_cb = waiting_children[i]->s.waiting.on_complete;
    call_ops[i].user_data =
        waiting_children[i]->s.waiting.on_complete_user_data;
    call_ops[i].data.start.pollset = waiting_children[i]->s.waiting.pollset;
    if (!prepare_activate(waiting_children[i]->elem, chand->active_child)) {
      waiting_children[i] = NULL;
      call_ops[i].done_cb(call_ops[i].user_data, GRPC_OP_ERROR);
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

void grpc_client_channel_set_transport_setup(grpc_channel_stack *channel_stack,
                                             grpc_transport_setup *setup) {
  /* post construction initialization: set the transport setup pointer */
  grpc_channel_element *elem = grpc_channel_stack_last_element(channel_stack);
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(!chand->transport_setup);
  chand->transport_setup = setup;
}
