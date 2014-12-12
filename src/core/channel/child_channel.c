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

#include "src/core/channel/child_channel.h"
#include "src/core/iomgr/iomgr.h"
#include <grpc/support/alloc.h>

/* Link back filter: passes up calls to the client channel, pushes down calls
   down */

static void unref_channel(grpc_child_channel *channel);

typedef struct {
  gpr_mu mu;
  gpr_cv cv;
  grpc_channel_element *back;
  gpr_refcount refs;
  int calling_back;
  int sent_goaway;
} lb_channel_data;

typedef struct {
  grpc_call_element *back;
  gpr_refcount refs;
  grpc_child_channel *channel;
} lb_call_data;

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

static void delayed_unref(void *elem, grpc_iomgr_cb_status status) {
  unref_channel(grpc_channel_stack_from_top_element(elem));
}

/* Currently we assume all channel operations should just be pushed up. */
static void lb_channel_op(grpc_channel_element *elem,
                          grpc_channel_element *from_elem,
                          grpc_channel_op *op) {
  lb_channel_data *chand = elem->channel_data;
  grpc_channel_element *back;

  switch (op->dir) {
    case GRPC_CALL_UP:
      gpr_mu_lock(&chand->mu);
      back = chand->back;
      if (back) chand->calling_back++;
      gpr_mu_unlock(&chand->mu);
      if (back) {
        back->filter->channel_op(chand->back, elem, op);
        gpr_mu_lock(&chand->mu);
        chand->calling_back--;
        gpr_cv_broadcast(&chand->cv);
        gpr_mu_unlock(&chand->mu);
      }
      break;
    case GRPC_CALL_DOWN:
      grpc_channel_next_op(elem, op);
      break;
  }

  switch (op->type) {
    case GRPC_TRANSPORT_CLOSED:
      grpc_iomgr_add_callback(delayed_unref, elem);
      break;
    case GRPC_CHANNEL_GOAWAY:
      gpr_mu_lock(&chand->mu);
      chand->sent_goaway = 1;
      gpr_mu_unlock(&chand->mu);
      break;
    case GRPC_CHANNEL_DISCONNECT:
    case GRPC_TRANSPORT_GOAWAY:
    case GRPC_ACCEPT_CALL:
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
  lb_channel_data *chand = elem->channel_data;
  GPR_ASSERT(is_first);
  GPR_ASSERT(!is_last);
  gpr_mu_init(&chand->mu);
  gpr_cv_init(&chand->cv);
  /* one ref for getting grpc_child_channel_destroy called, one for getting
     disconnected */
  gpr_ref_init(&chand->refs, 2);
  chand->back = NULL;
  chand->sent_goaway = 0;
  chand->calling_back = 0;
}

/* Destructor for channel_data */
static void lb_destroy_channel_elem(grpc_channel_element *elem) {
  lb_channel_data *chand = elem->channel_data;
  gpr_mu_destroy(&chand->mu);
  gpr_cv_destroy(&chand->cv);
}

const grpc_channel_filter grpc_child_channel_top_filter = {
    lb_call_op, lb_channel_op,

    sizeof(lb_call_data), lb_init_call_elem, lb_destroy_call_elem,

    sizeof(lb_channel_data), lb_init_channel_elem, lb_destroy_channel_elem,

    "child-channel",
};

/* grpc_child_channel proper */

#define LINK_BACK_ELEM_FROM_CHANNEL(channel) \
  grpc_channel_stack_element((channel), 0)

#define LINK_BACK_ELEM_FROM_CALL(call) grpc_call_stack_element((call), 0)

static void unref_channel(grpc_child_channel *channel) {
  lb_channel_data *lb = LINK_BACK_ELEM_FROM_CHANNEL(channel)->channel_data;
  if (gpr_unref(&lb->refs)) {
    grpc_channel_stack_destroy(channel);
    gpr_free(channel);
  }
}

static void ref_channel(grpc_child_channel *channel) {
  lb_channel_data *lb = LINK_BACK_ELEM_FROM_CHANNEL(channel)->channel_data;
  gpr_ref(&lb->refs);
}

static void unref_call(grpc_child_call *call) {
  lb_call_data *lb = LINK_BACK_ELEM_FROM_CALL(call)->call_data;
  if (gpr_unref(&lb->refs)) {
    grpc_child_channel *channel = lb->channel;
    grpc_call_stack_destroy(call);
    gpr_free(call);
    unref_channel(channel);
  }
}

#if 0
static void ref_call(grpc_child_call *call) {
  lb_call_data *lb = LINK_BACK_ELEM_FROM_CALL(call)->call_data;
  gpr_ref(&lb->refs);
}
#endif

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

  return (grpc_child_channel *)stk;
}

void grpc_child_channel_destroy(grpc_child_channel *channel) {
  grpc_channel_op op;
  int send_goaway = 0;
  grpc_channel_element *lbelem = LINK_BACK_ELEM_FROM_CHANNEL(channel);
  lb_channel_data *chand = lbelem->channel_data;

  gpr_mu_lock(&chand->mu);
  while (chand->calling_back) {
    gpr_cv_wait(&chand->cv, &chand->mu, gpr_inf_future);
  }
  send_goaway = !chand->sent_goaway;
  chand->sent_goaway = 1;
  chand->back = NULL;
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

  unref_channel(channel);
}

void grpc_child_channel_handle_op(grpc_child_channel *channel,
                                  grpc_channel_op *op) {
  grpc_channel_next_op(LINK_BACK_ELEM_FROM_CHANNEL(channel), op);
}

grpc_child_call *grpc_child_channel_create_call(grpc_child_channel *channel,
                                                grpc_call_element *parent) {
  grpc_call_stack *stk = gpr_malloc((channel)->call_stack_size);
  lb_call_data *lbcalld;
  ref_channel(channel);

  grpc_call_stack_init(channel, NULL, stk);
  lbcalld = LINK_BACK_ELEM_FROM_CALL(stk)->call_data;
  gpr_ref_init(&lbcalld->refs, 1);
  lbcalld->back = parent;
  lbcalld->channel = channel;

  return (grpc_child_call *)stk;
}

void grpc_child_call_destroy(grpc_child_call *call) { unref_call(call); }

grpc_call_element *grpc_child_call_get_top_element(grpc_child_call *call) {
  return LINK_BACK_ELEM_FROM_CALL(call);
}
