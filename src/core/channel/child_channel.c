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

#include "src/core/channel/child_channel.h"
#include "src/core/iomgr/iomgr.h"
#include <grpc/support/alloc.h>

/* Link back filter: passes up calls to the client channel, pushes down calls
   down */

static void maybe_destroy_channel(grpc_child_channel *channel);

typedef struct {
  gpr_mu mu;
  gpr_cv cv;
  grpc_channel_element *back;
  /* # of active calls on the channel */
  gpr_uint32 active_calls;
  /* has grpc_child_channel_destroy been called? */
  gpr_uint8 destroyed;
  /* has the transport reported itself disconnected? */
  gpr_uint8 disconnected;
  /* are we calling 'back' - our parent channel */
  gpr_uint8 calling_back;
  /* have we or our parent sent goaway yet? - dup suppression */
  gpr_uint8 sent_goaway;
  /* are we currently sending farewell (in this file: goaway + disconnect) */
  gpr_uint8 sending_farewell;
  /* have we sent farewell (goaway + disconnect) */
  gpr_uint8 sent_farewell;

  grpc_iomgr_closure finally_destroy_channel_closure;
  grpc_iomgr_closure send_farewells_closure;
} lb_channel_data;

typedef struct { grpc_child_channel *channel; } lb_call_data;

static void lb_start_transport_op(grpc_call_element *elem,
                                  grpc_transport_op *op) {
  grpc_call_next_op(elem, op);
}

/* Currently we assume all channel operations should just be pushed up. */
static void lb_channel_op(grpc_channel_element *elem,
                          grpc_channel_element *from_elem,
                          grpc_channel_op *op) {
  lb_channel_data *chand = elem->channel_data;
  grpc_channel_element *back;
  int calling_back = 0;

  switch (op->dir) {
    case GRPC_CALL_UP:
      gpr_mu_lock(&chand->mu);
      back = chand->back;
      if (back) {
        chand->calling_back++;
        calling_back = 1;
      }
      gpr_mu_unlock(&chand->mu);
      if (back) {
        back->filter->channel_op(chand->back, elem, op);
      } else if (op->type == GRPC_TRANSPORT_GOAWAY) {
        gpr_slice_unref(op->data.goaway.message);
      }
      break;
    case GRPC_CALL_DOWN:
      grpc_channel_next_op(elem, op);
      break;
  }

  gpr_mu_lock(&chand->mu);
  switch (op->type) {
    case GRPC_TRANSPORT_CLOSED:
      chand->disconnected = 1;
      maybe_destroy_channel(grpc_channel_stack_from_top_element(elem));
      break;
    case GRPC_CHANNEL_GOAWAY:
      chand->sent_goaway = 1;
      break;
    case GRPC_CHANNEL_DISCONNECT:
    case GRPC_TRANSPORT_GOAWAY:
    case GRPC_ACCEPT_CALL:
      break;
  }

  if (calling_back) {
    chand->calling_back--;
    gpr_cv_signal(&chand->cv);
    maybe_destroy_channel(grpc_channel_stack_from_top_element(elem));
  }
  gpr_mu_unlock(&chand->mu);
}

/* Constructor for call_data */
static void lb_init_call_elem(grpc_call_element *elem,
                              const void *server_transport_data,
                              grpc_transport_op *initial_op) {}

/* Destructor for call_data */
static void lb_destroy_call_elem(grpc_call_element *elem) {}

/* Constructor for channel_data */
static void lb_init_channel_elem(grpc_channel_element *elem,
                                 const grpc_channel_args *args,
                                 grpc_mdctx *metadata_context, int is_first,
                                 int is_last) {
  lb_channel_data *chand = elem->channel_data;
  GPR_ASSERT(is_first);
  GPR_ASSERT(!is_last);
  gpr_mu_init(&chand->mu);
  gpr_cv_init(&chand->cv);
  chand->back = NULL;
  chand->destroyed = 0;
  chand->disconnected = 0;
  chand->active_calls = 0;
  chand->sent_goaway = 0;
  chand->calling_back = 0;
  chand->sending_farewell = 0;
  chand->sent_farewell = 0;
}

/* Destructor for channel_data */
static void lb_destroy_channel_elem(grpc_channel_element *elem) {
  lb_channel_data *chand = elem->channel_data;
  gpr_mu_destroy(&chand->mu);
  gpr_cv_destroy(&chand->cv);
}

const grpc_channel_filter grpc_child_channel_top_filter = {
    lb_start_transport_op, 
    lb_channel_op,           

    sizeof(lb_call_data),
    lb_init_call_elem,     
    lb_destroy_call_elem,    

    sizeof(lb_channel_data),
    lb_init_channel_elem,  
    lb_destroy_channel_elem, 

    "child-channel",
};

/* grpc_child_channel proper */

#define LINK_BACK_ELEM_FROM_CHANNEL(channel) \
  grpc_channel_stack_element((channel), 0)

#define LINK_BACK_ELEM_FROM_CALL(call) grpc_call_stack_element((call), 0)

static void finally_destroy_channel(void *c, int success) {
  /* ignore success or not... this is a destruction callback and will only
     happen once - the only purpose here is to release resources */
  grpc_child_channel *channel = c;
  lb_channel_data *chand = LINK_BACK_ELEM_FROM_CHANNEL(channel)->channel_data;
  /* wait for the initiator to leave the mutex */
  gpr_mu_lock(&chand->mu);
  gpr_mu_unlock(&chand->mu);
  grpc_channel_stack_destroy(channel);
  gpr_free(channel);
}

static void send_farewells(void *c, int success) {
  grpc_child_channel *channel = c;
  grpc_channel_element *lbelem = LINK_BACK_ELEM_FROM_CHANNEL(channel);
  lb_channel_data *chand = lbelem->channel_data;
  int send_goaway;
  grpc_channel_op op;

  gpr_mu_lock(&chand->mu);
  send_goaway = !chand->sent_goaway;
  chand->sent_goaway = 1;
  gpr_mu_unlock(&chand->mu);

  if (send_goaway) {
    op.type = GRPC_CHANNEL_GOAWAY;
    op.dir = GRPC_CALL_DOWN;
    op.data.goaway.status = GRPC_STATUS_OK;
    op.data.goaway.message = gpr_slice_from_copied_string("Client disconnect");
    grpc_channel_next_op(lbelem, &op);
  }

  op.type = GRPC_CHANNEL_DISCONNECT;
  op.dir = GRPC_CALL_DOWN;
  grpc_channel_next_op(lbelem, &op);

  gpr_mu_lock(&chand->mu);
  chand->sending_farewell = 0;
  chand->sent_farewell = 1;
  maybe_destroy_channel(channel);
  gpr_mu_unlock(&chand->mu);
}

static void maybe_destroy_channel(grpc_child_channel *channel) {
  lb_channel_data *chand = LINK_BACK_ELEM_FROM_CHANNEL(channel)->channel_data;
  if (chand->destroyed && chand->disconnected && chand->active_calls == 0 &&
      !chand->sending_farewell && !chand->calling_back) {
    chand->finally_destroy_channel_closure.cb = finally_destroy_channel;
    chand->finally_destroy_channel_closure.cb_arg = channel;
    grpc_iomgr_add_callback(&chand->finally_destroy_channel_closure);
  } else if (chand->destroyed && !chand->disconnected &&
             chand->active_calls == 0 && !chand->sending_farewell &&
             !chand->sent_farewell) {
    chand->sending_farewell = 1;
    chand->send_farewells_closure.cb = send_farewells;
    chand->send_farewells_closure.cb_arg = channel;
    grpc_iomgr_add_callback(&chand->send_farewells_closure);
  }
}

grpc_child_channel *grpc_child_channel_create(
    grpc_channel_element *parent, const grpc_channel_filter **filters,
    size_t filter_count, const grpc_channel_args *args,
    grpc_mdctx *metadata_context) {
  grpc_channel_stack *stk =
      gpr_malloc(grpc_channel_stack_size(filters, filter_count));
  lb_channel_data *lb;

  grpc_channel_stack_init(filters, filter_count, args, metadata_context, stk);

  lb = LINK_BACK_ELEM_FROM_CHANNEL(stk)->channel_data;
  gpr_mu_lock(&lb->mu);
  lb->back = parent;
  gpr_mu_unlock(&lb->mu);

  return stk;
}

void grpc_child_channel_destroy(grpc_child_channel *channel,
                                int wait_for_callbacks) {
  grpc_channel_element *lbelem = LINK_BACK_ELEM_FROM_CHANNEL(channel);
  lb_channel_data *chand = lbelem->channel_data;

  gpr_mu_lock(&chand->mu);
  while (wait_for_callbacks && chand->calling_back) {
    gpr_cv_wait(&chand->cv, &chand->mu, gpr_inf_future);
  }

  chand->back = NULL;
  chand->destroyed = 1;
  maybe_destroy_channel(channel);
  gpr_mu_unlock(&chand->mu);
}

void grpc_child_channel_handle_op(grpc_child_channel *channel,
                                  grpc_channel_op *op) {
  grpc_channel_next_op(LINK_BACK_ELEM_FROM_CHANNEL(channel), op);
}

grpc_child_call *grpc_child_channel_create_call(grpc_child_channel *channel,
                                                grpc_call_element *parent,
                                                grpc_transport_op *initial_op) {
  grpc_call_stack *stk = gpr_malloc((channel)->call_stack_size);
  grpc_call_element *lbelem;
  lb_call_data *lbcalld;
  lb_channel_data *lbchand;

  grpc_call_stack_init(channel, NULL, initial_op, stk);
  lbelem = LINK_BACK_ELEM_FROM_CALL(stk);
  lbchand = lbelem->channel_data;
  lbcalld = lbelem->call_data;
  lbcalld->channel = channel;

  gpr_mu_lock(&lbchand->mu);
  lbchand->active_calls++;
  gpr_mu_unlock(&lbchand->mu);

  return stk;
}

void grpc_child_call_destroy(grpc_child_call *call) {
  grpc_call_element *lbelem = LINK_BACK_ELEM_FROM_CALL(call);
  lb_call_data *calld = lbelem->call_data;
  lb_channel_data *chand = lbelem->channel_data;
  grpc_child_channel *channel = calld->channel;
  grpc_call_stack_destroy(call);
  gpr_free(call);
  gpr_mu_lock(&chand->mu);
  chand->active_calls--;
  maybe_destroy_channel(channel);
  gpr_mu_unlock(&chand->mu);
}

grpc_call_element *grpc_child_call_get_top_element(grpc_child_call *call) {
  return LINK_BACK_ELEM_FROM_CALL(call);
}
