/*
 *
 * Copyright 2014, Google Inc.
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
#include "src/core/channel/connected_channel.h"
#include "src/core/channel/metadata_buffer.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

/* Link back filter: passes up calls to the client channel, pushes down calls
   down */

typedef struct { grpc_channel_element *back; } lb_channel_data;

typedef struct { grpc_call_element *back; } lb_call_data;

static void lb_call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                       grpc_call_op *op) {
  lb_call_data *calld = elem->call_data;

  switch (op->dir) {
    case GRPC_CALL_UP:
      calld->back->filter->call_op(calld->back, elem, op);
      break;
    case GRPC_CALL_DOWN:
      grpc_call_next_op(elem, op);
      break;
  }
}

/* Currently we assume all channel operations should just be pushed up. */
static void lb_channel_op(grpc_channel_element *elem,
                          grpc_channel_element *from_elem,
                          grpc_channel_op *op) {
  lb_channel_data *chand = elem->channel_data;

  switch (op->dir) {
    case GRPC_CALL_UP:
      chand->back->filter->channel_op(chand->back, elem, op);
      break;
    case GRPC_CALL_DOWN:
      grpc_channel_next_op(elem, op);
      break;
  }
}

/* Constructor for call_data */
static void lb_init_call_elem(grpc_call_element *elem,
                              const void *server_transport_data) {}

/* Destructor for call_data */
static void lb_destroy_call_elem(grpc_call_element *elem) {}

/* Constructor for channel_data */
static void lb_init_channel_elem(grpc_channel_element *elem,
                                 const grpc_channel_args *args,
                                 grpc_mdctx *metadata_context, int is_first,
                                 int is_last) {
  GPR_ASSERT(is_first);
  GPR_ASSERT(!is_last);
}

/* Destructor for channel_data */
static void lb_destroy_channel_elem(grpc_channel_element *elem) {}

static const grpc_channel_filter link_back_filter = {
    lb_call_op,               lb_channel_op,

    sizeof(lb_call_data),     lb_init_call_elem,    lb_destroy_call_elem,

    sizeof(lb_channel_data),  lb_init_channel_elem, lb_destroy_channel_elem,

    "clientchannel.linkback",
};

/* Client channel implementation */

typedef struct {
  size_t inflight_requests;
  grpc_channel_stack *channel_stack;
} child_entry;

typedef struct call_data call_data;

typedef struct {
  /* protects children, child_count, child_capacity, active_child,
     transport_setup_initiated
     does not protect channel stacks held by children
     transport_setup is assumed to be set once during construction */
  gpr_mu mu;

  /* the sending child (points somewhere in children, or NULL) */
  child_entry *active_child;
  /* vector of child channels  */
  child_entry *children;
  size_t child_count;
  size_t child_capacity;

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
      grpc_call_stack *child_stack;
      /* ... and the channel stack associated with it */
      grpc_channel_stack *using_stack;
    } active;
    struct {
      void (*on_complete)(void *user_data, grpc_op_error error);
      void *on_complete_user_data;
      gpr_uint32 start_flags;
    } waiting;
  } s;
};

static int prepare_activate(call_data *calld, child_entry *on_child) {
  grpc_call_element *child_elem;
  grpc_channel_stack *use_stack = on_child->channel_stack;

  if (calld->state == CALL_CANCELLED) return 0;

  on_child->inflight_requests++;

  /* no more access to calld->s.waiting allowed */
  GPR_ASSERT(calld->state == CALL_WAITING);
  calld->state = CALL_ACTIVE;

  /* create a child stack, and record that we're using a particular channel
     stack */
  calld->s.active.child_stack = gpr_malloc(use_stack->call_stack_size);
  calld->s.active.using_stack = use_stack;
  grpc_call_stack_init(use_stack, NULL, calld->s.active.child_stack);
  /* initialize the top level link back element */
  child_elem = grpc_call_stack_element(calld->s.active.child_stack, 0);
  GPR_ASSERT(child_elem->filter == &link_back_filter);
  ((lb_call_data *)child_elem->call_data)->back = calld->elem;

  return 1;
}

static void do_nothing(void *ignored, grpc_op_error error) {}

static void complete_activate(grpc_call_element *elem, child_entry *on_child,
                              grpc_call_op *op) {
  call_data *calld = elem->call_data;
  grpc_call_element *child_elem =
      grpc_call_stack_element(calld->s.active.child_stack, 0);

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
    if (prepare_activate(calld, chand->active_child)) {
      gpr_mu_unlock(&chand->mu);
      /* activate the request (pass it down) outside the lock */
      complete_activate(elem, chand->active_child, op);
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

static void cancel_rpc(grpc_call_element *elem, grpc_call_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_call_element *child_elem;
  grpc_call_op finish_op;

  gpr_mu_lock(&chand->mu);
  switch (calld->state) {
    case CALL_ACTIVE:
      child_elem = grpc_call_stack_element(calld->s.active.child_stack, 0);
      gpr_mu_unlock(&chand->mu);
      child_elem->filter->call_op(child_elem, elem, op);
      return; /* early out */
    case CALL_WAITING:
      remove_waiting_child(chand, calld);
      calld->s.waiting.on_complete(calld->s.waiting.on_complete_user_data,
                                   GRPC_OP_ERROR);
    /* fallthrough intended */
    case CALL_CREATED:
      calld->state = CALL_CANCELLED;
      gpr_mu_unlock(&chand->mu);
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
  grpc_call_element *child_elem;
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
    default:
      switch (op->dir) {
        case GRPC_CALL_UP:
          grpc_call_next_op(elem, op);
          break;
        case GRPC_CALL_DOWN:
          child_elem = grpc_call_stack_element(calld->s.active.child_stack, 0);
          GPR_ASSERT(calld->state == CALL_ACTIVE);
          child_elem->filter->call_op(child_elem, elem, op);
          break;
      }
      break;
  }
}

static void broadcast_channel_op_down(grpc_channel_element *elem,
                                      grpc_channel_op *op) {
  channel_data *chand = elem->channel_data;
  grpc_channel_element *child_elem;
  grpc_channel_stack **children;
  size_t child_count;
  size_t i;

  /* copy the current set of children, and mark them all as having an inflight
     request */
  gpr_mu_lock(&chand->mu);
  child_count = chand->child_count;
  children = gpr_malloc(sizeof(grpc_channel_stack *) * child_count);
  for (i = 0; i < child_count; i++) {
    children[i] = chand->children[i].channel_stack;
    chand->children[i].inflight_requests++;
  }
  gpr_mu_unlock(&chand->mu);

  /* send the message down */
  for (i = 0; i < child_count; i++) {
    child_elem = grpc_channel_stack_element(children[i], 0);
    if (op->type == GRPC_CHANNEL_GOAWAY) {
      gpr_slice_ref(op->data.goaway.message);
    }
    child_elem->filter->channel_op(child_elem, elem, op);
  }
  if (op->type == GRPC_CHANNEL_GOAWAY) {
    gpr_slice_unref(op->data.goaway.message);
  }

  /* unmark the inflight requests */
  gpr_mu_lock(&chand->mu);
  for (i = 0; i < child_count; i++) {
    chand->children[i].inflight_requests--;
  }
  gpr_mu_unlock(&chand->mu);

  gpr_free(children);
}

static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  switch (op->type) {
    default:
      switch (op->dir) {
        case GRPC_CALL_UP:
          grpc_channel_next_op(elem, op);
          break;
        case GRPC_CALL_DOWN:
          broadcast_channel_op_down(elem, op);
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
  channel_data *chand = elem->channel_data;
  size_t i;

  /* if the metadata buffer is not flushed, destroy it here. */
  grpc_metadata_buffer_destroy(&calld->pending_metadata, GRPC_OP_OK);
  /* if the call got activated, we need to destroy the child stack also, and
     remove it from the in-flight requests tracked by the child_entry we
     picked */
  if (calld->state == CALL_ACTIVE) {
    grpc_call_stack_destroy(calld->s.active.child_stack);
    gpr_free(calld->s.active.child_stack);

    gpr_mu_lock(&chand->mu);
    for (i = 0; i < chand->child_count; i++) {
      if (chand->children[i].channel_stack == calld->s.active.using_stack) {
        chand->children[i].inflight_requests--;
        /* TODO(ctiller): garbage collect channels that are not active
           and have no inflight requests */
      }
    }
    gpr_mu_unlock(&chand->mu);
  }
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args,
                              grpc_mdctx *metadata_context, int is_first,
                              int is_last) {
  channel_data *chand = elem->channel_data;
  char temp[16];

  GPR_ASSERT(!is_first);
  GPR_ASSERT(is_last);
  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  gpr_mu_init(&chand->mu);
  chand->active_child = NULL;
  chand->children = NULL;
  chand->child_count = 0;
  chand->child_capacity = 0;
  chand->waiting_children = NULL;
  chand->waiting_child_count = 0;
  chand->waiting_child_capacity = 0;
  chand->transport_setup = NULL;
  chand->transport_setup_initiated = 0;
  chand->args = grpc_channel_args_copy(args);

  sprintf(temp, "%d", GRPC_STATUS_CANCELLED);
  chand->cancel_status =
      grpc_mdelem_from_strings(metadata_context, "grpc-status", temp);
}

/* Destructor for channel_data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  size_t i;

  grpc_transport_setup_cancel(chand->transport_setup);

  for (i = 0; i < chand->child_count; i++) {
    GPR_ASSERT(chand->children[i].inflight_requests == 0);
    grpc_channel_stack_destroy(chand->children[i].channel_stack);
    gpr_free(chand->children[i].channel_stack);
  }

  grpc_channel_args_destroy(chand->args);
  grpc_mdelem_unref(chand->cancel_status);

  gpr_mu_destroy(&chand->mu);
  GPR_ASSERT(chand->waiting_child_count == 0);
  gpr_free(chand->waiting_children);
  gpr_free(chand->children);
}

const grpc_channel_filter grpc_client_channel_filter = {
    call_op,              channel_op,

    sizeof(call_data),    init_call_elem,    destroy_call_elem,

    sizeof(channel_data), init_channel_elem, destroy_channel_elem,

    "clientchannel",
};

grpc_transport_setup_result grpc_client_channel_transport_setup_complete(
    grpc_channel_stack *channel_stack, grpc_transport *transport,
    grpc_channel_filter const **channel_filters, size_t num_channel_filters,
    grpc_mdctx *mdctx) {
  /* we just got a new transport: lets create a child channel stack for it */
  grpc_channel_element *elem = grpc_channel_stack_last_element(channel_stack);
  channel_data *chand = elem->channel_data;
  grpc_channel_element *lb_elem;
  grpc_channel_stack *child_stack;
  size_t num_child_filters = 2 + num_channel_filters;
  grpc_channel_filter const **child_filters;
  grpc_transport_setup_result result;
  child_entry *child_ent;
  call_data **waiting_children;
  size_t waiting_child_count;
  size_t i;
  grpc_call_op *call_ops;

  /* build the child filter stack */
  child_filters = gpr_malloc(sizeof(grpc_channel_filter *) * num_child_filters);
  /* we always need a link back filter to get back to the connected channel */
  child_filters[0] = &link_back_filter;
  for (i = 0; i < num_channel_filters; i++) {
    child_filters[i + 1] = channel_filters[i];
  }
  /* and we always need a connected channel to talk to the transport */
  child_filters[num_child_filters - 1] = &grpc_connected_channel_filter;

  GPR_ASSERT(elem->filter == &grpc_client_channel_filter);

  /* BEGIN LOCKING CHANNEL */
  gpr_mu_lock(&chand->mu);
  chand->transport_setup_initiated = 0;

  if (chand->child_count == chand->child_capacity) {
    /* realloc will invalidate chand->active_child, but it's reset in the next
       stanza anyway */
    chand->child_capacity =
        GPR_MAX(2 * chand->child_capacity, chand->child_capacity + 2);
    chand->children = gpr_realloc(chand->children,
                                  sizeof(child_entry) * chand->child_capacity);
  }

  /* build up the child stack */
  child_stack =
      gpr_malloc(grpc_channel_stack_size(child_filters, num_child_filters));
  grpc_channel_stack_init(child_filters, num_child_filters, chand->args, mdctx,
                          child_stack);
  lb_elem = grpc_channel_stack_element(child_stack, 0);
  GPR_ASSERT(lb_elem->filter == &link_back_filter);
  ((lb_channel_data *)lb_elem->channel_data)->back = elem;
  result = grpc_connected_channel_bind_transport(child_stack, transport);
  child_ent = &chand->children[chand->child_count++];
  child_ent->channel_stack = child_stack;
  child_ent->inflight_requests = 0;
  chand->active_child = child_ent;

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
    if (!prepare_activate(waiting_children[i], child_ent)) {
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
      complete_activate(waiting_children[i]->elem, child_ent, &call_ops[i]);
    }
  }
  gpr_free(waiting_children);
  gpr_free(call_ops);
  gpr_free(child_filters);

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
