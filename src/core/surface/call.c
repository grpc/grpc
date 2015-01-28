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

#include "src/core/surface/call.h"
#include "src/core/channel/channel_stack.h"
#include "src/core/channel/metadata_buffer.h"
#include "src/core/iomgr/alarm.h"
#include "src/core/support/string.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/completion_queue.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_IN_MASK(op, mask) (((1 << (op)) & (mask)) != 0)

typedef struct {
  size_t md_out_count;
  size_t md_out_capacity;
  grpc_metadata *md_out;
  grpc_byte_buffer *msg_out;

  /* input buffers */
  grpc_metadata_array md_in;
  grpc_metadata_array trail_md_in;
  grpc_recv_status status_in;
  size_t msg_in_read_idx;
  grpc_byte_buffer_array msg_in;

  void *finished_tag;
} legacy_state;

typedef enum { REQ_INITIAL = 0, REQ_READY, REQ_DONE } req_state;

typedef enum {
  SEND_NOTHING,
  SEND_INITIAL_METADATA,
  SEND_MESSAGE,
  SEND_TRAILING_METADATA,
  SEND_FINISH
} send_action;

typedef struct {
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  grpc_op_error status;
} completed_request;

typedef struct reqinfo {
  req_state state;
  grpc_ioreq_data data;
  struct reqinfo *master;
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  gpr_uint32 need_mask;
  gpr_uint32 complete_mask;
} reqinfo;

struct grpc_call {
  grpc_completion_queue *cq;
  grpc_channel *channel;
  grpc_mdctx *metadata_context;
  /* TODO(ctiller): share with cq if possible? */
  gpr_mu mu;

  gpr_uint8 is_client;
  gpr_uint8 got_initial_metadata;
  gpr_uint8 have_alarm;
  gpr_uint8 read_closed;
  gpr_uint8 stream_closed;
  gpr_uint8 got_status_code;
  gpr_uint8 sending;
  gpr_uint8 num_completed_requests;

  reqinfo requests[GRPC_IOREQ_OP_COUNT];
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  grpc_byte_buffer_array buffered_messages;
  grpc_metadata_array buffered_initial_metadata;
  grpc_metadata_array buffered_trailing_metadata;
  size_t write_index;

  grpc_status_code status_code;
  grpc_mdstr *status_details;

  grpc_alarm alarm;

  gpr_refcount internal_refcount;

  legacy_state *legacy_state;
};

#define CALL_STACK_FROM_CALL(call) ((grpc_call_stack *)((call)+1))
#define CALL_FROM_CALL_STACK(call_stack) (((grpc_call *)(call_stack)) - 1)
#define CALL_ELEM_FROM_CALL(call, idx) \
  grpc_call_stack_element(CALL_STACK_FROM_CALL(call), idx)
#define CALL_FROM_TOP_ELEM(top_elem) \
  CALL_FROM_CALL_STACK(grpc_call_stack_from_top_element(top_elem))

#define SWAP(type, x, y) \
  do {                   \
    type temp = x;       \
    x = y;               \
    y = temp;            \
  } while (0)

static void do_nothing(void *ignored, grpc_op_error also_ignored) {}
static send_action choose_send_action(grpc_call *call);
static void enact_send_action(grpc_call *call, send_action sa);

grpc_call *grpc_call_create(grpc_channel *channel,
                            const void *server_transport_data) {
  grpc_channel_stack *channel_stack = grpc_channel_get_channel_stack(channel);
  grpc_call *call =
      gpr_malloc(sizeof(grpc_call) + channel_stack->call_stack_size);
  memset(call, 0, sizeof(grpc_call));
  gpr_mu_init(&call->mu);
  call->channel = channel;
  call->is_client = server_transport_data == NULL;
  grpc_channel_internal_ref(channel);
  call->metadata_context = grpc_channel_get_metadata_context(channel);
  gpr_ref_init(&call->internal_refcount, 1);
  grpc_call_stack_init(channel_stack, server_transport_data,
                       CALL_STACK_FROM_CALL(call));
  return call;
}

legacy_state *get_legacy_state(grpc_call *call) {
  if (call->legacy_state == NULL) {
    call->legacy_state = gpr_malloc(sizeof(legacy_state));
    memset(call->legacy_state, 0, sizeof(legacy_state));
  }
  return call->legacy_state;
}

void grpc_call_internal_ref(grpc_call *c) { gpr_ref(&c->internal_refcount); }

void grpc_call_internal_unref(grpc_call *c) {
  if (gpr_unref(&c->internal_refcount)) {
    grpc_call_stack_destroy(CALL_STACK_FROM_CALL(c));
    grpc_channel_internal_unref(c->channel);
    gpr_mu_destroy(&c->mu);
    if (c->legacy_state) {
      gpr_free(c->legacy_state->md_out);
      gpr_free(c->legacy_state->md_in.metadata);
      gpr_free(c->legacy_state->trail_md_in.metadata);
      gpr_free(c->legacy_state->status_in.details);
      gpr_free(c->legacy_state);
    }
    gpr_free(c);
  }
}

static grpc_call_error bind_cq(grpc_call *call, grpc_completion_queue *cq) {
  if (call->cq) return GRPC_CALL_ERROR_ALREADY_INVOKED;
  call->cq = cq;
  return GRPC_CALL_OK;
}

static void request_more_data(grpc_call *call) {
  grpc_call_op op;

  /* call down */
  op.type = GRPC_REQUEST_DATA;
  op.dir = GRPC_CALL_DOWN;
  op.flags = 0;
  op.done_cb = do_nothing;
  op.user_data = NULL;

  grpc_call_execute_op(call, &op);
}

static void lock(grpc_call *call) { gpr_mu_lock(&call->mu); }

static void unlock(grpc_call *call) {
  send_action sa;
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  int num_completed_requests = call->num_completed_requests;
  int i;

  if (num_completed_requests != 0) {
    memcpy(completed_requests, call->completed_requests,
           sizeof(completed_requests));
    call->num_completed_requests = 0;
  }

  if (!call->sending) {
    sa = choose_send_action(call);
    if (sa != SEND_NOTHING) {
      call->sending = 1;
    }
  }

  gpr_mu_unlock(&call->mu);

  if (sa != SEND_NOTHING) {
    enact_send_action(call, sa);
  }

  for (i = 0; i < num_completed_requests; i++) {
    completed_requests[i].on_complete(call, completed_requests[i].status,
                                      completed_requests[i].user_data);
  }
}

static void finish_ioreq_op(grpc_call *call, grpc_ioreq_op op,
                            grpc_op_error status) {
  reqinfo *master = call->requests[op].master;
  completed_request *cr;
  size_t i;
  switch (call->requests[op].state) {
    case REQ_INITIAL: /* not started yet */
      return;
    case REQ_DONE: /* already finished */
      abort();
      return;
    case REQ_READY:
      master->complete_mask |= 1 << op;
      call->requests[op].state =
          (op == GRPC_IOREQ_SEND_MESSAGES || op == GRPC_IOREQ_RECV_MESSAGES)
              ? REQ_INITIAL
              : REQ_DONE;
      if (master->complete_mask == master->need_mask ||
          status == GRPC_OP_ERROR) {
        for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
          if (call->requests[i].master == master) {
            call->requests[i].master = NULL;
          }
        }
        cr = &call->completed_requests[call->num_completed_requests++];
        cr->status = status;
        cr->on_complete = master->on_complete;
        cr->user_data = master->user_data;
      }
  }
}

static void finish_write_step(void *pc, grpc_op_error error) {
  grpc_call *call = pc;
  lock(call);
  if (error == GRPC_OP_OK) {
    if (call->write_index ==
        call->requests[GRPC_IOREQ_SEND_MESSAGES].data.send_messages.count) {
      finish_ioreq_op(call, GRPC_IOREQ_SEND_MESSAGES, GRPC_OP_OK);
    }
  } else {
    finish_ioreq_op(call, GRPC_IOREQ_SEND_MESSAGES, GRPC_OP_ERROR);
  }
  call->sending = 0;
  unlock(call);
}

static void finish_finish_step(void *pc, grpc_op_error error) {
  grpc_call *call = pc;
  lock(call);
  if (error == GRPC_OP_OK) {
    finish_ioreq_op(call, GRPC_IOREQ_SEND_CLOSE, GRPC_OP_OK);
  } else {
    gpr_log(GPR_ERROR, "not implemented");
    abort();
  }
  call->sending = 0;
  unlock(call);
}

static void finish_start_step(void *pc, grpc_op_error error) {
  grpc_call *call = pc;
  lock(call);
  if (error == GRPC_OP_OK) {
    finish_ioreq_op(call, GRPC_IOREQ_SEND_INITIAL_METADATA, GRPC_OP_OK);
  } else {
    gpr_log(GPR_ERROR, "not implemented");
    abort();
  }
  call->sending = 0;
  unlock(call);
}

static send_action choose_send_action(grpc_call *call) {
  switch (call->requests[GRPC_IOREQ_SEND_INITIAL_METADATA].state) {
    case REQ_INITIAL:
      return SEND_NOTHING;
    case REQ_READY:
      return SEND_INITIAL_METADATA;
    case REQ_DONE:
      break;
  }
  switch (call->requests[GRPC_IOREQ_SEND_MESSAGES].state) {
    case REQ_INITIAL:
      return SEND_NOTHING;
    case REQ_READY:
      return SEND_MESSAGE;
    case REQ_DONE:
      break;
  }
  switch (call->requests[GRPC_IOREQ_SEND_TRAILING_METADATA].state) {
    case REQ_INITIAL:
      return SEND_NOTHING;
    case REQ_READY:
      finish_ioreq_op(call, GRPC_IOREQ_SEND_TRAILING_METADATA, GRPC_OP_OK);
      return SEND_TRAILING_METADATA;
    case REQ_DONE:
      break;
  }
  switch (call->requests[GRPC_IOREQ_SEND_CLOSE].state) {
    default:
      return SEND_NOTHING;
    case REQ_READY:
      return SEND_FINISH;
  }
}

static void enact_send_action(grpc_call *call, send_action sa) {
  grpc_ioreq_data data;
  grpc_call_op op;
  int i;

  switch (sa) {
    case SEND_NOTHING:
      abort();
      break;
    case SEND_INITIAL_METADATA:
      data = call->requests[GRPC_IOREQ_SEND_INITIAL_METADATA].data;
      for (i = 0; i < data.send_metadata.count; i++) {
        const grpc_metadata *md = &data.send_metadata.metadata[i];
        grpc_call_element_send_metadata(
            CALL_ELEM_FROM_CALL(call, 0),
            grpc_mdelem_from_string_and_buffer(call->metadata_context, md->key,
                                               (const gpr_uint8 *)md->value,
                                               md->value_length));
      }
      op.type = GRPC_SEND_START;
      op.dir = GRPC_CALL_DOWN;
      op.flags = 0;
      op.data.start.pollset = grpc_cq_pollset(call->cq);
      op.done_cb = finish_start_step;
      op.user_data = call;
      grpc_call_execute_op(call, &op);
      break;
    case SEND_MESSAGE:
      data = call->requests[GRPC_IOREQ_SEND_MESSAGES].data;
      op.type = GRPC_SEND_MESSAGE;
      op.dir = GRPC_CALL_DOWN;
      op.flags = 0;
      op.data.message = data.send_messages.messages[call->write_index];
      op.done_cb = finish_write_step;
      op.user_data = call;
      grpc_call_execute_op(call, &op);
      break;
    case SEND_TRAILING_METADATA:
      data = call->requests[GRPC_IOREQ_SEND_TRAILING_METADATA].data;
      for (i = 0; i < data.send_metadata.count; i++) {
        const grpc_metadata *md = &data.send_metadata.metadata[i];
        grpc_call_element_send_metadata(
            CALL_ELEM_FROM_CALL(call, 0),
            grpc_mdelem_from_string_and_buffer(call->metadata_context, md->key,
                                               (const gpr_uint8 *)md->value,
                                               md->value_length));
      }
      lock(call);
      call->sending = 0;
      unlock(call);
      break;
    case SEND_FINISH:
      op.type = GRPC_SEND_FINISH;
      op.dir = GRPC_CALL_DOWN;
      op.flags = 0;
      op.done_cb = finish_finish_step;
      op.user_data = call;
      grpc_call_execute_op(call, &op);
      break;
  }
}

static grpc_call_error start_ioreq_error(grpc_call *call,
                                         gpr_uint32 mutated_ops,
                                         grpc_call_error ret) {
  size_t i;
  for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
    if (mutated_ops & (1 << i)) {
      call->requests[i].master = NULL;
    }
  }
  return ret;
}

static grpc_call_error start_ioreq(grpc_call *call, const grpc_ioreq *reqs,
                                   size_t nreqs,
                                   grpc_ioreq_completion_func completion,
                                   void *user_data) {
  size_t i;
  gpr_uint32 have_ops = 0;
  gpr_uint32 precomplete = 0;
  grpc_ioreq_op op;
  reqinfo *master = NULL;
  reqinfo *requests = call->requests;
  grpc_ioreq_data data;

  for (i = 0; i < nreqs; i++) {
    op = reqs[i].op;
    if (requests[op].master) {
      return start_ioreq_error(call, have_ops,
                               GRPC_CALL_ERROR_TOO_MANY_OPERATIONS);
    }
    switch (requests[op].state) {
      case REQ_INITIAL:
        break;
      case REQ_READY:
        return start_ioreq_error(call, have_ops,
                                 GRPC_CALL_ERROR_TOO_MANY_OPERATIONS);
      case REQ_DONE:
        return start_ioreq_error(call, have_ops,
                                 GRPC_CALL_ERROR_ALREADY_INVOKED);
    }
    if (master == NULL) {
      master = &requests[op];
    }
    have_ops |= 1 << op;
    data = reqs[i].data;

    switch (op) {
      default:
        break;
      case GRPC_IOREQ_RECV_MESSAGES:
        data.recv_messages->count = 0;
        if (call->buffered_messages.count > 0) {
          SWAP(grpc_byte_buffer_array, *data.recv_messages,
               call->buffered_messages);
          precomplete |= 1 << op;
          abort();
        }
        break;
      case GRPC_IOREQ_SEND_MESSAGES:
        call->write_index = 0;
        break;
    }

    requests[op].state = REQ_READY;
    requests[op].data = data;
    requests[op].master = master;
  }

  GPR_ASSERT(master != NULL);
  master->need_mask = have_ops;
  master->complete_mask = precomplete;
  master->on_complete = completion;
  master->user_data = user_data;

  if (OP_IN_MASK(GRPC_IOREQ_RECV_MESSAGES, have_ops & ~precomplete)) {
    request_more_data(call);
  }

  return GRPC_CALL_OK;
}

static void call_start_ioreq_done(grpc_call *call, grpc_op_error status,
                                  void *user_data) {
  grpc_cq_end_ioreq(call->cq, user_data, call, do_nothing, NULL, status);
}

grpc_call_error grpc_call_start_ioreq(grpc_call *call, const grpc_ioreq *reqs,
                                      size_t nreqs, void *tag) {
  grpc_call_error err;
  lock(call);
  err = start_ioreq(call, reqs, nreqs, call_start_ioreq_done, tag);
  unlock(call);
  return err;
}

grpc_call_error grpc_call_start_ioreq_and_call_back(
    grpc_call *call, const grpc_ioreq *reqs, size_t nreqs,
    grpc_ioreq_completion_func on_complete, void *user_data) {
  grpc_call_error err;
  lock(call);
  err = start_ioreq(call, reqs, nreqs, on_complete, user_data);
  unlock(call);
  return err;
}

void grpc_call_destroy(grpc_call *c) {
  int cancel;
  gpr_mu_lock(&c->mu);
  if (c->have_alarm) {
    grpc_alarm_cancel(&c->alarm);
    c->have_alarm = 0;
  }
  cancel = !c->stream_closed;
  gpr_mu_unlock(&c->mu);
  if (cancel) grpc_call_cancel(c);
  grpc_call_internal_unref(c);
}

static void maybe_set_status_code(grpc_call *call, gpr_uint32 status) {
  if (call->got_status_code) return;
  call->status_code = status;
  call->got_status_code = 1;
}

static void maybe_set_status_details(grpc_call *call, grpc_mdstr *status) {
  if (call->status_details != NULL) {
    grpc_mdstr_unref(status);
    return;
  }
  call->status_details = status;
}

grpc_call_error grpc_call_cancel(grpc_call *c) {
  grpc_call_element *elem;
  grpc_call_op op;

  op.type = GRPC_CANCEL_OP;
  op.dir = GRPC_CALL_DOWN;
  op.flags = 0;
  op.done_cb = do_nothing;
  op.user_data = NULL;

  elem = CALL_ELEM_FROM_CALL(c, 0);
  elem->filter->call_op(elem, NULL, &op);

  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_cancel_with_status(grpc_call *c,
                                             grpc_status_code status,
                                             const char *description) {
  grpc_mdstr *details =
      description ? grpc_mdstr_from_string(c->metadata_context, description)
                  : NULL;
  gpr_mu_lock(&c->mu);
  maybe_set_status_code(c, status);
  if (details) {
    maybe_set_status_details(c, details);
  }
  gpr_mu_unlock(&c->mu);
  return grpc_call_cancel(c);
}

void grpc_call_execute_op(grpc_call *call, grpc_call_op *op) {
  grpc_call_element *elem;
  GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
  elem = CALL_ELEM_FROM_CALL(call, 0);
  elem->filter->call_op(elem, NULL, op);
}

grpc_call_error grpc_call_add_metadata(grpc_call *call, grpc_metadata *metadata,
                                       gpr_uint32 flags) {
  legacy_state *ls;
  grpc_metadata *mdout;

  lock(call);
  ls = get_legacy_state(call);

  if (ls->md_out_count == ls->md_out_capacity) {
    ls->md_out_capacity =
        GPR_MAX(ls->md_out_count * 3 / 2, ls->md_out_count + 8);
    ls->md_out =
        gpr_realloc(ls->md_out, sizeof(grpc_mdelem *) * ls->md_out_capacity);
  }
  mdout = &ls->md_out[ls->md_out_count++];
  mdout->key = gpr_strdup(metadata->key);
  mdout->value = gpr_malloc(metadata->value_length);
  mdout->value_length = metadata->value_length;
  memcpy(mdout->value, metadata->value, metadata->value_length);

  unlock(call);

  return GRPC_CALL_OK;
}

static void finish_status(grpc_call *call, grpc_op_error status, void *tag) {
  legacy_state *ls;

  lock(call);
  ls = get_legacy_state(call);
  unlock(call);

  if (status == GRPC_OP_OK) {
    grpc_cq_end_finished(call->cq, tag, call, do_nothing, NULL,
                         ls->status_in.status, ls->status_in.details,
                         ls->trail_md_in.metadata, ls->trail_md_in.count);
  } else {
    grpc_cq_end_finished(call->cq, tag, call, do_nothing, NULL,
                         GRPC_STATUS_UNKNOWN, "Read status failed", NULL, 0);
  }
}

static void finish_recv_metadata(grpc_call *call, grpc_op_error status,
                                 void *tag) {
  legacy_state *ls;

  lock(call);
  ls = get_legacy_state(call);
  if (status == GRPC_OP_OK) {
    grpc_cq_end_client_metadata_read(call->cq, tag, call, do_nothing, NULL,
                                     ls->md_in.count, ls->md_in.metadata);

  } else {
    grpc_cq_end_client_metadata_read(call->cq, tag, call, do_nothing, NULL, 0,
                                     NULL);
  }
  unlock(call);
}

static void finish_send_metadata(grpc_call *call, grpc_op_error status,
                                 void *metadata_read_tag) {
  grpc_ioreq reqs[2];
  legacy_state *ls;

  lock(call);
  if (status == GRPC_OP_OK) {
    ls = get_legacy_state(call);
    reqs[0].op = GRPC_IOREQ_RECV_INITIAL_METADATA;
    reqs[0].data.recv_metadata = &ls->md_in;
    GPR_ASSERT(GRPC_CALL_OK == start_ioreq(call, reqs, 1, finish_recv_metadata,
                                           metadata_read_tag));

    ls = get_legacy_state(call);
    reqs[0].op = GRPC_IOREQ_RECV_TRAILING_METADATA;
    reqs[0].data.recv_metadata = &ls->trail_md_in;
    reqs[1].op = GRPC_IOREQ_RECV_STATUS;
    reqs[1].data.recv_status = &ls->status_in;
    GPR_ASSERT(GRPC_CALL_OK ==
               start_ioreq(call, reqs, 2, finish_status, ls->finished_tag));
  } else {
    ls = get_legacy_state(call);
    grpc_cq_end_client_metadata_read(call->cq, metadata_read_tag, call,
                                     do_nothing, NULL, 0, NULL);
    grpc_cq_end_finished(call->cq, ls->finished_tag, call, do_nothing, NULL,
                         GRPC_STATUS_UNKNOWN, "Failed to read initial metadata",
                         NULL, 0);
  }
  unlock(call);
}

grpc_call_error grpc_call_invoke(grpc_call *call, grpc_completion_queue *cq,
                                 void *metadata_read_tag, void *finished_tag,
                                 gpr_uint32 flags) {
  grpc_ioreq req;
  legacy_state *ls = get_legacy_state(call);
  grpc_call_error err;

  grpc_cq_begin_op(cq, call, GRPC_CLIENT_METADATA_READ);
  grpc_cq_begin_op(cq, call, GRPC_FINISHED);

  lock(call);
  err = bind_cq(call, cq);
  if (err != GRPC_CALL_OK) return err;

  req.op = GRPC_IOREQ_SEND_INITIAL_METADATA;
  req.data.send_metadata.count = ls->md_out_count;
  req.data.send_metadata.metadata = ls->md_out;
  err = start_ioreq(call, &req, 1, finish_send_metadata, metadata_read_tag);
  unlock(call);
  return err;
}

grpc_call_error grpc_call_server_accept(grpc_call *call,
                                        grpc_completion_queue *cq,
                                        void *finished_tag) {
  grpc_ioreq req;
  grpc_call_error err;

  /* inform the completion queue of an incoming operation (corresponding to
     finished_tag) */
  grpc_cq_begin_op(cq, call, GRPC_FINISHED);

  err = bind_cq(call, cq);
  if (err != GRPC_CALL_OK) return err;

  req.op = GRPC_IOREQ_RECV_STATUS;
  req.data.recv_status = &get_legacy_state(call)->status_in;
  err = start_ioreq(call, &req, 1, finish_status, finished_tag);
  unlock(call);
  return err;
}

grpc_call_error grpc_call_server_end_initial_metadata(grpc_call *call,
                                                      gpr_uint32 flags) {
  return GRPC_CALL_OK;
}

void grpc_call_client_initial_metadata_complete(
    grpc_call_element *surface_element) {
  grpc_call *call = grpc_call_from_top_element(surface_element);
  lock(call);
  call->got_initial_metadata = 1;
  finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, GRPC_OP_OK);
  unlock(call);
}

static void finish_read(grpc_call *call, grpc_op_error error, void *tag) {
  legacy_state *ls;
  lock(call);
  ls = get_legacy_state(call);
  if (ls->msg_in.count == 0) {
    grpc_cq_end_read(call->cq, tag, call, do_nothing, NULL, NULL);
  } else {
    grpc_cq_end_read(call->cq, tag, call, do_nothing, NULL,
                     ls->msg_in.buffers[ls->msg_in_read_idx++]);
  }
  unlock(call);
}

grpc_call_error grpc_call_start_read(grpc_call *call, void *tag) {
  legacy_state *ls;
  grpc_ioreq req;
  grpc_call_error err;

  grpc_cq_begin_op(call->cq, call, GRPC_READ);

  lock(call);
  ls = get_legacy_state(call);

  if (ls->msg_in_read_idx == ls->msg_in.count) {
    ls->msg_in_read_idx = 0;
    req.op = GRPC_IOREQ_RECV_MESSAGES;
    req.data.recv_messages = &ls->msg_in;
    err = start_ioreq(call, &req, 1, finish_read, tag);
  } else {
    err = GRPC_CALL_OK;
    grpc_cq_end_read(call->cq, tag, call, do_nothing, NULL,
                     ls->msg_in.buffers[ls->msg_in_read_idx++]);
  }
  unlock(call);
  return err;
}

static void finish_write(grpc_call *call, grpc_op_error status, void *tag) {
  grpc_cq_end_write_accepted(call->cq, tag, call, do_nothing, NULL, status);
}

grpc_call_error grpc_call_start_write(grpc_call *call,
                                      grpc_byte_buffer *byte_buffer, void *tag,
                                      gpr_uint32 flags) {
  grpc_ioreq req;
  legacy_state *ls;
  grpc_call_error err;

  grpc_cq_begin_op(call->cq, call, GRPC_WRITE_ACCEPTED);

  lock(call);
  ls = get_legacy_state(call);
  ls->msg_out = byte_buffer;
  req.op = GRPC_IOREQ_SEND_MESSAGES;
  req.data.send_messages.count = 1;
  req.data.send_messages.messages = &ls->msg_out;
  err = start_ioreq(call, &req, 1, finish_write, tag);
  unlock(call);

  return err;
}

static void finish_finish(grpc_call *call, grpc_op_error status, void *tag) {
  grpc_cq_end_finish_accepted(call->cq, tag, call, do_nothing, NULL, status);
}

grpc_call_error grpc_call_writes_done(grpc_call *call, void *tag) {
  grpc_ioreq req;
  grpc_call_error err;
  grpc_cq_begin_op(call->cq, call, GRPC_FINISH_ACCEPTED);

  lock(call);
  req.op = GRPC_IOREQ_SEND_CLOSE;
  err = start_ioreq(call, &req, 1, finish_finish, tag);
  unlock(call);

  return err;
}

grpc_call_error grpc_call_start_write_status(grpc_call *call,
                                             grpc_status_code status,
                                             const char *details, void *tag) {
  grpc_ioreq req;
  grpc_call_error err;
  grpc_cq_begin_op(call->cq, call, GRPC_FINISH_ACCEPTED);

  lock(call);
  req.op = GRPC_IOREQ_SEND_CLOSE;
  req.data.send_close.status = status;
  req.data.send_close.details = details;
  err = start_ioreq(call, &req, 1, finish_finish, tag);
  unlock(call);

  return err;
}

grpc_call *grpc_call_from_top_element(grpc_call_element *elem) {
  return CALL_FROM_TOP_ELEM(elem);
}

static void call_alarm(void *arg, int success) {
  grpc_call *call = arg;
  if (success) {
    if (call->is_client) {
      grpc_call_cancel_with_status(call, GRPC_STATUS_DEADLINE_EXCEEDED,
                                   "Deadline Exceeded");
    } else {
      grpc_call_cancel(call);
    }
  }
  grpc_call_internal_unref(call);
}

void grpc_call_set_deadline(grpc_call_element *elem, gpr_timespec deadline) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);

  if (call->have_alarm) {
    gpr_log(GPR_ERROR, "Attempt to set deadline alarm twice");
  }
  grpc_call_internal_ref(call);
  call->have_alarm = 1;
  grpc_alarm_init(&call->alarm, deadline, call_alarm, call, gpr_now());
}

void grpc_call_read_closed(grpc_call_element *elem) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  lock(call);
  GPR_ASSERT(!call->read_closed);
  call->read_closed = 1;
  finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGES, GRPC_OP_OK);
  finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, GRPC_OP_OK);
  finish_ioreq_op(call, GRPC_IOREQ_RECV_TRAILING_METADATA, GRPC_OP_OK);
  unlock(call);
}

void grpc_call_stream_closed(grpc_call_element *elem) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  lock(call);
  if (!call->read_closed) {
    call->read_closed = 1;
    finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGES, GRPC_OP_OK);
    finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, GRPC_OP_OK);
    finish_ioreq_op(call, GRPC_IOREQ_RECV_TRAILING_METADATA, GRPC_OP_OK);
  }
  call->stream_closed = 1;
  finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS, GRPC_OP_OK);
  unlock(call);
}

/* we offset status by a small amount when storing it into transport metadata
   as metadata cannot store a 0 value (which is used as OK for grpc_status_codes
   */
#define STATUS_OFFSET 1
static void destroy_status(void *ignored) {}

static gpr_uint32 decode_status(grpc_mdelem *md) {
  gpr_uint32 status;
  void *user_data = grpc_mdelem_get_user_data(md, destroy_status);
  if (user_data) {
    status = ((gpr_uint32)(gpr_intptr) user_data) - STATUS_OFFSET;
  } else {
    if (!gpr_parse_bytes_to_uint32(grpc_mdstr_as_c_string(md->value),
                                   GPR_SLICE_LENGTH(md->value->slice),
                                   &status)) {
      status = GRPC_STATUS_UNKNOWN; /* could not parse status code */
    }
    grpc_mdelem_set_user_data(md, destroy_status,
                              (void *)(gpr_intptr)(status + STATUS_OFFSET));
  }
  return status;
}

void grpc_call_recv_message(grpc_call_element *elem,
                            grpc_byte_buffer *byte_buffer) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  grpc_byte_buffer_array *dest;
  lock(call);
  if (call->requests[GRPC_IOREQ_RECV_MESSAGES].master != NULL) {
    dest = call->requests[GRPC_IOREQ_RECV_MESSAGES].data.recv_messages;
  } else {
    dest = &call->buffered_messages;
  }
  if (dest->count == dest->capacity) {
    dest->capacity = GPR_MAX(dest->capacity + 1, dest->capacity * 3 / 2);
    dest->buffers =
        gpr_realloc(dest->buffers, sizeof(grpc_byte_buffer *) * dest->capacity);
  }
  dest->buffers[dest->count++] = byte_buffer;
  finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGES, GRPC_OP_OK);
  unlock(call);
}

void grpc_call_recv_metadata(grpc_call_element *elem, grpc_mdelem *md) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  grpc_mdstr *key = md->key;
  grpc_metadata_array *dest;
  grpc_metadata *mdusr;

  lock(call);
  if (key == grpc_channel_get_status_string(call->channel)) {
    maybe_set_status_code(call, decode_status(md));
    grpc_mdelem_unref(md);
  } else if (key == grpc_channel_get_message_string(call->channel)) {
    maybe_set_status_details(call, md->value);
    grpc_mdelem_unref(md);
  } else {
    if (!call->got_initial_metadata) {
      dest = call->requests[GRPC_IOREQ_RECV_INITIAL_METADATA].state == REQ_READY
                 ? call->requests[GRPC_IOREQ_RECV_INITIAL_METADATA]
                       .data.recv_metadata
                 : &call->buffered_initial_metadata;
    } else {
      dest = call->requests[GRPC_IOREQ_RECV_INITIAL_METADATA].state == REQ_READY
                 ? call->requests[GRPC_IOREQ_RECV_INITIAL_METADATA]
                       .data.recv_metadata
                 : &call->buffered_trailing_metadata;
    }
    if (dest->count == dest->capacity) {
      dest->capacity = GPR_MAX(dest->capacity + 1, dest->capacity * 3 / 2);
      dest->metadata =
          gpr_realloc(dest->metadata, sizeof(grpc_metadata) * dest->capacity);
    }
    mdusr = &dest->metadata[dest->count++];
    mdusr->key = (char *)grpc_mdstr_as_c_string(md->key);
    mdusr->value = (char *)grpc_mdstr_as_c_string(md->value);
    mdusr->value_length = GPR_SLICE_LENGTH(md->value->slice);
  }
  unlock(call);
}

grpc_call_stack *grpc_call_get_call_stack(grpc_call *call) {
  return CALL_STACK_FROM_CALL(call);
}
