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
#include "src/core/surface/byte_buffer_queue.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/completion_queue.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_IN_MASK(op, mask) (((1 << (op)) & (mask)) != 0)

typedef struct legacy_state legacy_state;
static void destroy_legacy_state(legacy_state *ls);

typedef enum { REQ_INITIAL = 0, REQ_READY, REQ_DONE } req_state;

typedef enum {
  SEND_NOTHING,
  SEND_INITIAL_METADATA,
  SEND_MESSAGE,
  SEND_TRAILING_METADATA_AND_FINISH,
  SEND_FINISH
} send_action;

typedef struct {
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  grpc_op_error status;
} completed_request;

/* See reqinfo.set below for a description */
#define REQSET_EMPTY 255
#define REQSET_DONE 254

/* The state of an ioreq */
typedef struct reqinfo {
  /* User supplied parameters */
  grpc_ioreq_data data;
  /* In which set is this ioreq?
     This value could be:
       - an element of grpc_ioreq_op enumeration, in which case
         it designates the master ioreq in a set of requests
       - REQSET_EMPTY, in which case this reqinfo type has no application
         request against it
       - REQSET_DONE, in which case this reqinfo has been satisfied for
         all time for this call, and no further use will be made of it */
  gpr_uint8 set;
  grpc_op_error status;
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  gpr_uint32 need_mask;
  gpr_uint32 complete_mask;
} reqinfo;

typedef enum {
  STATUS_FROM_API_OVERRIDE = 0,
  STATUS_FROM_WIRE,
  STATUS_SOURCE_COUNT
} status_source;

typedef struct {
  gpr_uint8 set;
  grpc_status_code code;
  grpc_mdstr *details;
} received_status;

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
  gpr_uint8 sending;
  gpr_uint8 num_completed_requests;
  gpr_uint8 need_more_data;

  reqinfo requests[GRPC_IOREQ_OP_COUNT];
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  grpc_byte_buffer_queue incoming_queue;
  grpc_metadata_array buffered_initial_metadata;
  grpc_metadata_array buffered_trailing_metadata;
  grpc_mdelem **owned_metadata;
  size_t owned_metadata_count;
  size_t owned_metadata_capacity;

  received_status status[STATUS_SOURCE_COUNT];

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
  size_t i;
  grpc_channel_stack *channel_stack = grpc_channel_get_channel_stack(channel);
  grpc_call *call =
      gpr_malloc(sizeof(grpc_call) + channel_stack->call_stack_size);
  memset(call, 0, sizeof(grpc_call));
  gpr_mu_init(&call->mu);
  call->channel = channel;
  call->is_client = server_transport_data == NULL;
  for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
    call->requests[i].set = REQSET_EMPTY;
  }
  if (call->is_client) {
    call->requests[GRPC_IOREQ_SEND_TRAILING_METADATA].set = REQSET_DONE;
    call->requests[GRPC_IOREQ_SEND_STATUS].set = REQSET_DONE;
  }
  grpc_channel_internal_ref(channel);
  call->metadata_context = grpc_channel_get_metadata_context(channel);
  /* one ref is dropped in response to destroy, the other in
     stream_closed */
  gpr_ref_init(&call->internal_refcount, 2);
  grpc_call_stack_init(channel_stack, server_transport_data,
                       CALL_STACK_FROM_CALL(call));
  return call;
}

void grpc_call_internal_ref(grpc_call *c) { gpr_ref(&c->internal_refcount); }

static void destroy_call(void *call, int ignored_success) {
  size_t i;
  grpc_call *c = call;
  grpc_call_stack_destroy(CALL_STACK_FROM_CALL(c));
  grpc_channel_internal_unref(c->channel);
  gpr_mu_destroy(&c->mu);
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (c->status[i].details) {
      grpc_mdstr_unref(c->status[i].details);
    }
  }
  for (i = 0; i < c->owned_metadata_count; i++) {
    grpc_mdelem_unref(c->owned_metadata[i]);
  }
  gpr_free(c->owned_metadata);
  gpr_free(c->buffered_initial_metadata.metadata);
  gpr_free(c->buffered_trailing_metadata.metadata);
  if (c->legacy_state) {
    destroy_legacy_state(c->legacy_state);
  }
  gpr_free(c);
}

void grpc_call_internal_unref(grpc_call *c, int allow_immediate_deletion) {
  if (gpr_unref(&c->internal_refcount)) {
    if (allow_immediate_deletion) {
      destroy_call(c, 1);
    } else {
      grpc_iomgr_add_callback(destroy_call, c);
    }
  }
}

static void set_status_code(grpc_call *call, status_source source,
                            gpr_uint32 status) {
  call->status[source].set = 1;
  call->status[source].code = status;
}

static void set_status_details(grpc_call *call, status_source source,
                               grpc_mdstr *status) {
  if (call->status[source].details != NULL) {
    grpc_mdstr_unref(call->status[source].details);
  }
  call->status[source].details = status;
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
  send_action sa = SEND_NOTHING;
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  int num_completed_requests = call->num_completed_requests;
  int need_more_data =
      call->need_more_data &&
      call->requests[GRPC_IOREQ_SEND_INITIAL_METADATA].set == REQSET_DONE;
  int i;

  if (need_more_data) {
    call->need_more_data = 0;
  }

  if (num_completed_requests != 0) {
    memcpy(completed_requests, call->completed_requests,
           sizeof(completed_requests));
    call->num_completed_requests = 0;
  }

  if (!call->sending) {
    sa = choose_send_action(call);
    if (sa != SEND_NOTHING) {
      call->sending = 1;
      grpc_call_internal_ref(call);
    }
  }

  gpr_mu_unlock(&call->mu);

  if (need_more_data) {
    request_more_data(call);
  }

  if (sa != SEND_NOTHING) {
    enact_send_action(call, sa);
  }

  for (i = 0; i < num_completed_requests; i++) {
    completed_requests[i].on_complete(call, completed_requests[i].status,
                                      completed_requests[i].user_data);
  }
}

static void get_final_status(grpc_call *call, grpc_recv_status_args args) {
  int i;
  for (i = 0; i < STATUS_SOURCE_COUNT; i++) {
    if (call->status[i].set) {
      *args.code = call->status[i].code;
      if (!args.details) return;
      if (call->status[i].details) {
        gpr_slice details = call->status[i].details->slice;
        size_t len = GPR_SLICE_LENGTH(details);
        if (len + 1 > *args.details_capacity) {
          *args.details_capacity =
              GPR_MAX(len + 1, *args.details_capacity * 3 / 2);
          *args.details = gpr_realloc(*args.details, *args.details_capacity);
        }
        memcpy(*args.details, GPR_SLICE_START_PTR(details), len);
        (*args.details)[len] = 0;
      } else {
        goto no_details;
      }
      return;
    }
  }
  *args.code = GRPC_STATUS_UNKNOWN;
  if (!args.details) return;

no_details:
  if (0 == *args.details_capacity) {
    *args.details_capacity = 8;
    *args.details = gpr_malloc(*args.details_capacity);
  }
  **args.details = 0;
}

static void finish_ioreq_op(grpc_call *call, grpc_ioreq_op op,
                            grpc_op_error status) {
  completed_request *cr;
  size_t i;
  if (call->requests[op].set < GRPC_IOREQ_OP_COUNT) {
    reqinfo *master = &call->requests[call->requests[op].set];
    /* ioreq is live: we need to do something */
    master->complete_mask |= 1 << op;
    call->requests[op].set =
        (op == GRPC_IOREQ_SEND_MESSAGE || op == GRPC_IOREQ_RECV_MESSAGE)
            ? REQSET_EMPTY
            : REQSET_DONE;
    if (master->complete_mask == master->need_mask || status == GRPC_OP_ERROR) {
      if (OP_IN_MASK(GRPC_IOREQ_RECV_STATUS, master->need_mask)) {
        get_final_status(
            call, call->requests[GRPC_IOREQ_RECV_STATUS].data.recv_status);
      }
      for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
        if (call->requests[i].set == op) {
          if (call->requests[i].status != GRPC_OP_OK) {
            status = GRPC_OP_ERROR;
          }
          call->requests[i].set = REQSET_EMPTY;
        }
      }
      cr = &call->completed_requests[call->num_completed_requests++];
      cr->status = status;
      cr->on_complete = master->on_complete;
      cr->user_data = master->user_data;
    }
  }
}

static void finish_send_op(grpc_call *call, grpc_ioreq_op op,
                           grpc_op_error error) {
  lock(call);
  finish_ioreq_op(call, op, error);
  call->sending = 0;
  unlock(call);
  grpc_call_internal_unref(call, 0);
}

static void finish_write_step(void *pc, grpc_op_error error) {
  finish_send_op(pc, GRPC_IOREQ_SEND_MESSAGE, error);
}

static void finish_finish_step(void *pc, grpc_op_error error) {
  finish_send_op(pc, GRPC_IOREQ_SEND_CLOSE, error);
}

static void finish_start_step(void *pc, grpc_op_error error) {
  finish_send_op(pc, GRPC_IOREQ_SEND_INITIAL_METADATA, error);
}

static send_action choose_send_action(grpc_call *call) {
  switch (call->requests[GRPC_IOREQ_SEND_INITIAL_METADATA].set) {
    case REQSET_EMPTY:
      return SEND_NOTHING;
    default:
      return SEND_INITIAL_METADATA;
    case REQSET_DONE:
      break;
  }
  switch (call->requests[GRPC_IOREQ_SEND_MESSAGE].set) {
    case REQSET_EMPTY:
      return SEND_NOTHING;
    default:
      return SEND_MESSAGE;
    case REQSET_DONE:
      break;
  }
  switch (call->requests[GRPC_IOREQ_SEND_CLOSE].set) {
    case REQSET_EMPTY:
    case REQSET_DONE:
      return SEND_NOTHING;
    default:
      if (call->is_client) {
        return SEND_FINISH;
      } else if (call->requests[GRPC_IOREQ_SEND_TRAILING_METADATA].set !=
                     REQSET_EMPTY &&
                 call->requests[GRPC_IOREQ_SEND_STATUS].set != REQSET_EMPTY) {
        finish_ioreq_op(call, GRPC_IOREQ_SEND_TRAILING_METADATA, GRPC_OP_OK);
        finish_ioreq_op(call, GRPC_IOREQ_SEND_STATUS, GRPC_OP_OK);
        return SEND_TRAILING_METADATA_AND_FINISH;
      } else {
        return SEND_NOTHING;
      }
  }
}

static void send_metadata(grpc_call *call, grpc_mdelem *elem) {
  grpc_call_op op;
  op.type = GRPC_SEND_METADATA;
  op.dir = GRPC_CALL_DOWN;
  op.flags = 0;
  op.data.metadata = elem;
  op.done_cb = do_nothing;
  op.user_data = NULL;
  grpc_call_execute_op(call, &op);
}

static void enact_send_action(grpc_call *call, send_action sa) {
  grpc_ioreq_data data;
  grpc_call_op op;
  size_t i;
  char status_str[GPR_LTOA_MIN_BUFSIZE];

  switch (sa) {
    case SEND_NOTHING:
      abort();
      break;
    case SEND_INITIAL_METADATA:
      data = call->requests[GRPC_IOREQ_SEND_INITIAL_METADATA].data;
      for (i = 0; i < data.send_metadata.count; i++) {
        const grpc_metadata *md = &data.send_metadata.metadata[i];
        send_metadata(call,
                      grpc_mdelem_from_string_and_buffer(
                          call->metadata_context, md->key,
                          (const gpr_uint8 *)md->value, md->value_length));
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
      data = call->requests[GRPC_IOREQ_SEND_MESSAGE].data;
      op.type = GRPC_SEND_MESSAGE;
      op.dir = GRPC_CALL_DOWN;
      op.flags = 0;
      op.data.message = data.send_message;
      op.done_cb = finish_write_step;
      op.user_data = call;
      grpc_call_execute_op(call, &op);
      break;
    case SEND_TRAILING_METADATA_AND_FINISH:
      /* send trailing metadata */
      data = call->requests[GRPC_IOREQ_SEND_TRAILING_METADATA].data;
      for (i = 0; i < data.send_metadata.count; i++) {
        const grpc_metadata *md = &data.send_metadata.metadata[i];
        send_metadata(call,
                      grpc_mdelem_from_string_and_buffer(
                          call->metadata_context, md->key,
                          (const gpr_uint8 *)md->value, md->value_length));
      }
      /* send status */
      /* TODO(ctiller): cache common status values */
      data = call->requests[GRPC_IOREQ_SEND_STATUS].data;
      gpr_ltoa(data.send_status.code, status_str);
      send_metadata(
          call,
          grpc_mdelem_from_metadata_strings(
              call->metadata_context,
              grpc_mdstr_ref(grpc_channel_get_status_string(call->channel)),
              grpc_mdstr_from_string(call->metadata_context, status_str)));
      if (data.send_status.details) {
        send_metadata(
            call,
            grpc_mdelem_from_metadata_strings(
                call->metadata_context,
                grpc_mdstr_ref(grpc_channel_get_message_string(call->channel)),
                grpc_mdstr_from_string(call->metadata_context,
                                       data.send_status.details)));
      }
    /* fallthrough: see choose_send_action for details */
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
      call->requests[i].set = REQSET_EMPTY;
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
  grpc_ioreq_op op;
  reqinfo *requests = call->requests;
  reqinfo *master;
  grpc_ioreq_data data;
  gpr_uint8 set;

  if (nreqs == 0) {
    return GRPC_CALL_OK;
  }

  set = reqs[0].op;

  for (i = 0; i < nreqs; i++) {
    op = reqs[i].op;
    if (requests[op].set < GRPC_IOREQ_OP_COUNT) {
      return start_ioreq_error(call, have_ops,
                               GRPC_CALL_ERROR_TOO_MANY_OPERATIONS);
    } else if (requests[op].set == REQSET_DONE) {
      return start_ioreq_error(call, have_ops, GRPC_CALL_ERROR_ALREADY_INVOKED);
    }
    have_ops |= 1 << op;
    data = reqs[i].data;

    requests[op].data = data;
    requests[op].set = set;
  }

  master = &requests[set];
  master->need_mask = have_ops;
  master->complete_mask = 0;
  master->on_complete = completion;
  master->user_data = user_data;

  for (i = 0; i < nreqs; i++) {
    op = reqs[i].op;
    data = reqs[i].data;
    switch (op) {
      case GRPC_IOREQ_OP_COUNT:
        gpr_log(GPR_ERROR, "should never reach here");
        abort();
        break;
      case GRPC_IOREQ_RECV_MESSAGE:
        *data.recv_message = grpc_bbq_pop(&call->incoming_queue);
        if (*data.recv_message) {
          finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, GRPC_OP_OK);
          if (call->stream_closed && grpc_bbq_empty(&call->incoming_queue)) {
            finish_ioreq_op(call, GRPC_IOREQ_RECV_CLOSE, GRPC_OP_OK);
          }
        } else {
          /* no message: either end of stream or we need more bytes */
          if (call->read_closed) {
            finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, GRPC_OP_OK);
            if (call->stream_closed) {
              /* stream closed AND we've drained all messages: signal to the application */
              finish_ioreq_op(call, GRPC_IOREQ_RECV_CLOSE, GRPC_OP_OK);
            }
          } else {
            call->need_more_data = 1;
          }
        }
        break;
      case GRPC_IOREQ_RECV_STATUS:
        if (call->read_closed) {
          finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS, GRPC_OP_OK);
        }
        break;
      case GRPC_IOREQ_RECV_CLOSE:
        if (call->stream_closed) {
          finish_ioreq_op(call, GRPC_IOREQ_RECV_CLOSE, GRPC_OP_OK);
        }
        break;
      case GRPC_IOREQ_SEND_CLOSE:
        if (requests[GRPC_IOREQ_SEND_MESSAGE].set == REQSET_EMPTY) {
          requests[GRPC_IOREQ_SEND_MESSAGE].set = REQSET_DONE;
        }
        if (call->stream_closed) {
          finish_ioreq_op(call, GRPC_IOREQ_SEND_CLOSE, GRPC_OP_ERROR);
        }
        break;
      case GRPC_IOREQ_SEND_MESSAGE:
      case GRPC_IOREQ_SEND_INITIAL_METADATA:
      case GRPC_IOREQ_SEND_TRAILING_METADATA:
      case GRPC_IOREQ_SEND_STATUS:
        if (call->stream_closed) {
          finish_ioreq_op(call, op, GRPC_OP_ERROR);
        }
        break;
      case GRPC_IOREQ_RECV_INITIAL_METADATA:
        data.recv_metadata->count = 0;
        if (call->buffered_initial_metadata.count > 0) {
          SWAP(grpc_metadata_array, *data.recv_metadata,
               call->buffered_initial_metadata);
        }
        if (call->got_initial_metadata) {
          finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, GRPC_OP_OK);
        } else if (call->stream_closed) {
          finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA,
                          GRPC_OP_ERROR);
        }
        break;
      case GRPC_IOREQ_RECV_TRAILING_METADATA:
        data.recv_metadata->count = 0;
        if (call->buffered_trailing_metadata.count > 0) {
          SWAP(grpc_metadata_array, *data.recv_metadata,
               call->buffered_trailing_metadata);
        }
        if (call->read_closed) {
          finish_ioreq_op(call, GRPC_IOREQ_RECV_TRAILING_METADATA, GRPC_OP_OK);
        }
        break;
    }
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
  lock(c);
  if (c->have_alarm) {
    grpc_alarm_cancel(&c->alarm);
    c->have_alarm = 0;
  }
  cancel = !c->stream_closed;
  unlock(c);
  if (cancel) grpc_call_cancel(c);
  grpc_call_internal_unref(c, 1);
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
  lock(c);
  set_status_code(c, STATUS_FROM_API_OVERRIDE, status);
  set_status_details(c, STATUS_FROM_API_OVERRIDE, details);
  unlock(c);
  return grpc_call_cancel(c);
}

void grpc_call_execute_op(grpc_call *call, grpc_call_op *op) {
  grpc_call_element *elem;
  GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
  elem = CALL_ELEM_FROM_CALL(call, 0);
  elem->filter->call_op(elem, NULL, op);
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
  grpc_call_internal_unref(call, 1);
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

static void mark_read_closed(grpc_call *call) {
  call->read_closed = 1;
  finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, GRPC_OP_OK);
  finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, GRPC_OP_OK);
  finish_ioreq_op(call, GRPC_IOREQ_RECV_TRAILING_METADATA, GRPC_OP_OK);
  finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS, GRPC_OP_OK);
}

void grpc_call_read_closed(grpc_call_element *elem) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  lock(call);
  GPR_ASSERT(!call->read_closed);
  mark_read_closed(call);
  unlock(call);
}

void grpc_call_stream_closed(grpc_call_element *elem) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  lock(call);
  GPR_ASSERT(!call->stream_closed);
  if (!call->read_closed) {
    mark_read_closed(call);
  }
  call->stream_closed = 1;
  if (grpc_bbq_empty(&call->incoming_queue)) {
    finish_ioreq_op(call, GRPC_IOREQ_RECV_CLOSE, GRPC_OP_OK);
  }
  unlock(call);
  grpc_call_internal_unref(call, 0);
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
  lock(call);
  if (call->requests[GRPC_IOREQ_RECV_MESSAGE].set < GRPC_IOREQ_OP_COUNT) {
    /* there's an outstanding read */
    *call->requests[GRPC_IOREQ_RECV_MESSAGE].data.recv_message = byte_buffer;
    finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, GRPC_OP_OK);
  } else {
    grpc_bbq_push(&call->incoming_queue, byte_buffer);
  }
  unlock(call);
}

void grpc_call_recv_metadata(grpc_call_element *elem, grpc_mdelem *md) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  grpc_mdstr *key = md->key;
  grpc_metadata_array *dest;
  grpc_metadata *mdusr;

  lock(call);
  if (key == grpc_channel_get_status_string(call->channel)) {
    set_status_code(call, STATUS_FROM_WIRE, decode_status(md));
    grpc_mdelem_unref(md);
  } else if (key == grpc_channel_get_message_string(call->channel)) {
    set_status_details(call, STATUS_FROM_WIRE, grpc_mdstr_ref(md->value));
    grpc_mdelem_unref(md);
  } else {
    if (!call->got_initial_metadata) {
      dest = call->requests[GRPC_IOREQ_RECV_INITIAL_METADATA].set <
                     GRPC_IOREQ_OP_COUNT
                 ? call->requests[GRPC_IOREQ_RECV_INITIAL_METADATA]
                       .data.recv_metadata
                 : &call->buffered_initial_metadata;
    } else {
      dest = call->requests[GRPC_IOREQ_RECV_TRAILING_METADATA].set <
                     GRPC_IOREQ_OP_COUNT
                 ? call->requests[GRPC_IOREQ_RECV_TRAILING_METADATA]
                       .data.recv_metadata
                 : &call->buffered_trailing_metadata;
    }
    if (dest->count == dest->capacity) {
      dest->capacity = GPR_MAX(dest->capacity + 8, dest->capacity * 2);
      dest->metadata =
          gpr_realloc(dest->metadata, sizeof(grpc_metadata) * dest->capacity);
    }
    mdusr = &dest->metadata[dest->count++];
    mdusr->key = (char *)grpc_mdstr_as_c_string(md->key);
    mdusr->value = (char *)grpc_mdstr_as_c_string(md->value);
    mdusr->value_length = GPR_SLICE_LENGTH(md->value->slice);
    if (call->owned_metadata_count == call->owned_metadata_capacity) {
      call->owned_metadata_capacity = GPR_MAX(
          call->owned_metadata_capacity + 8, call->owned_metadata_capacity * 2);
      call->owned_metadata =
          gpr_realloc(call->owned_metadata,
                      sizeof(grpc_mdelem *) * call->owned_metadata_capacity);
    }
    call->owned_metadata[call->owned_metadata_count++] = md;
  }
  unlock(call);
}

grpc_call_stack *grpc_call_get_call_stack(grpc_call *call) {
  return CALL_STACK_FROM_CALL(call);
}

/*
 * LEGACY API IMPLEMENTATION
 * All this code will disappear as soon as wrappings are updated
 */

struct legacy_state {
  gpr_uint8 md_out_buffer;
  size_t md_out_count[2];
  size_t md_out_capacity[2];
  grpc_metadata *md_out[2];
  grpc_byte_buffer *msg_out;

  /* input buffers */
  grpc_metadata_array initial_md_in;
  grpc_metadata_array trailing_md_in;

  size_t details_capacity;
  char *details;
  grpc_status_code status;

  size_t msg_in_read_idx;
  grpc_byte_buffer *msg_in;

  void *finished_tag;
};

static legacy_state *get_legacy_state(grpc_call *call) {
  if (call->legacy_state == NULL) {
    call->legacy_state = gpr_malloc(sizeof(legacy_state));
    memset(call->legacy_state, 0, sizeof(legacy_state));
  }
  return call->legacy_state;
}

static void destroy_legacy_state(legacy_state *ls) {
  size_t i, j;
  for (i = 0; i < 2; i++) {
    for (j = 0; j < ls->md_out_count[i]; j++) {
      gpr_free(ls->md_out[i][j].key);
      gpr_free(ls->md_out[i][j].value);
    }
    gpr_free(ls->md_out[i]);
  }
  gpr_free(ls->initial_md_in.metadata);
  gpr_free(ls->trailing_md_in.metadata);
  gpr_free(ls);
}

grpc_call_error grpc_call_add_metadata(grpc_call *call, grpc_metadata *metadata,
                                       gpr_uint32 flags) {
  legacy_state *ls;
  grpc_metadata *mdout;

  lock(call);
  ls = get_legacy_state(call);

  if (ls->md_out_count[ls->md_out_buffer] ==
      ls->md_out_capacity[ls->md_out_buffer]) {
    ls->md_out_capacity[ls->md_out_buffer] =
        GPR_MAX(ls->md_out_capacity[ls->md_out_buffer] * 3 / 2,
                ls->md_out_capacity[ls->md_out_buffer] + 8);
    ls->md_out[ls->md_out_buffer] = gpr_realloc(
        ls->md_out[ls->md_out_buffer],
        sizeof(grpc_metadata) * ls->md_out_capacity[ls->md_out_buffer]);
  }
  mdout = &ls->md_out[ls->md_out_buffer][ls->md_out_count[ls->md_out_buffer]++];
  mdout->key = gpr_strdup(metadata->key);
  mdout->value = gpr_malloc(metadata->value_length);
  mdout->value_length = metadata->value_length;
  memcpy(mdout->value, metadata->value, metadata->value_length);

  unlock(call);

  return GRPC_CALL_OK;
}

static void finish_status(grpc_call *call, grpc_op_error status,
                          void *ignored) {
  legacy_state *ls;

  lock(call);
  ls = get_legacy_state(call);
  grpc_cq_end_finished(call->cq, ls->finished_tag, call, do_nothing, NULL,
                       ls->status, ls->details, ls->trailing_md_in.metadata,
                       ls->trailing_md_in.count);
  unlock(call);
}

static void finish_recv_metadata(grpc_call *call, grpc_op_error status,
                                 void *tag) {
  legacy_state *ls;

  lock(call);
  ls = get_legacy_state(call);
  if (status == GRPC_OP_OK) {
    grpc_cq_end_client_metadata_read(call->cq, tag, call, do_nothing, NULL,
                                     ls->initial_md_in.count,
                                     ls->initial_md_in.metadata);

  } else {
    grpc_cq_end_client_metadata_read(call->cq, tag, call, do_nothing, NULL, 0,
                                     NULL);
  }
  unlock(call);
}

static void finish_send_metadata(grpc_call *call, grpc_op_error status,
                                 void *tag) {}

grpc_call_error grpc_call_invoke(grpc_call *call, grpc_completion_queue *cq,
                                 void *metadata_read_tag, void *finished_tag,
                                 gpr_uint32 flags) {
  grpc_ioreq reqs[3];
  legacy_state *ls;
  grpc_call_error err;

  grpc_cq_begin_op(cq, call, GRPC_CLIENT_METADATA_READ);
  grpc_cq_begin_op(cq, call, GRPC_FINISHED);

  lock(call);
  ls = get_legacy_state(call);
  err = bind_cq(call, cq);
  if (err != GRPC_CALL_OK) goto done;

  ls->finished_tag = finished_tag;

  reqs[0].op = GRPC_IOREQ_SEND_INITIAL_METADATA;
  reqs[0].data.send_metadata.count = ls->md_out_count[ls->md_out_buffer];
  reqs[0].data.send_metadata.metadata = ls->md_out[ls->md_out_buffer];
  ls->md_out_buffer++;
  err = start_ioreq(call, reqs, 1, finish_send_metadata, NULL);
  if (err != GRPC_CALL_OK) goto done;

  reqs[0].op = GRPC_IOREQ_RECV_INITIAL_METADATA;
  reqs[0].data.recv_metadata = &ls->initial_md_in;
  err = start_ioreq(call, reqs, 1, finish_recv_metadata, metadata_read_tag);
  if (err != GRPC_CALL_OK) goto done;

  reqs[0].op = GRPC_IOREQ_RECV_TRAILING_METADATA;
  reqs[0].data.recv_metadata = &ls->trailing_md_in;
  reqs[1].op = GRPC_IOREQ_RECV_STATUS;
  reqs[1].data.recv_status.details = &ls->details;
  reqs[1].data.recv_status.details_capacity = &ls->details_capacity;
  reqs[1].data.recv_status.code = &ls->status;
  reqs[2].op = GRPC_IOREQ_RECV_CLOSE;
  err = start_ioreq(call, reqs, 3, finish_status, NULL);
  if (err != GRPC_CALL_OK) goto done;

done:
  unlock(call);
  return err;
}

grpc_call_error grpc_call_server_accept(grpc_call *call,
                                        grpc_completion_queue *cq,
                                        void *finished_tag) {
  grpc_ioreq reqs[2];
  grpc_call_error err;
  legacy_state *ls;

  /* inform the completion queue of an incoming operation (corresponding to
     finished_tag) */
  grpc_cq_begin_op(cq, call, GRPC_FINISHED);

  lock(call);
  ls = get_legacy_state(call);

  err = bind_cq(call, cq);
  if (err != GRPC_CALL_OK) return err;

  ls->finished_tag = finished_tag;

  reqs[0].op = GRPC_IOREQ_RECV_STATUS;
  reqs[0].data.recv_status.details = NULL;
  reqs[0].data.recv_status.details_capacity = 0;
  reqs[0].data.recv_status.code = &ls->status;
  reqs[1].op = GRPC_IOREQ_RECV_CLOSE;
  err = start_ioreq(call, reqs, 2, finish_status, NULL);
  unlock(call);
  return err;
}

static void finish_send_initial_metadata(grpc_call *call, grpc_op_error status,
                                         void *tag) {}

grpc_call_error grpc_call_server_end_initial_metadata(grpc_call *call,
                                                      gpr_uint32 flags) {
  grpc_ioreq req;
  grpc_call_error err;
  legacy_state *ls;

  lock(call);
  ls = get_legacy_state(call);
  req.op = GRPC_IOREQ_SEND_INITIAL_METADATA;
  req.data.send_metadata.count = ls->md_out_count[ls->md_out_buffer];
  req.data.send_metadata.metadata = ls->md_out[ls->md_out_buffer];
  err = start_ioreq(call, &req, 1, finish_send_initial_metadata, NULL);
  unlock(call);

  return err;
}

void grpc_call_initial_metadata_complete(grpc_call_element *surface_element) {
  grpc_call *call = grpc_call_from_top_element(surface_element);
  lock(call);
  call->got_initial_metadata = 1;
  finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, GRPC_OP_OK);
  unlock(call);
}

static void finish_read_event(void *p, grpc_op_error error) {
  if (p) grpc_byte_buffer_destroy(p);
}

static void finish_read(grpc_call *call, grpc_op_error error, void *tag) {
  legacy_state *ls;
  grpc_byte_buffer *msg;
  lock(call);
  ls = get_legacy_state(call);
  msg = ls->msg_in;
  grpc_cq_end_read(call->cq, tag, call, finish_read_event, msg, msg);
  unlock(call);
}

grpc_call_error grpc_call_start_read(grpc_call *call, void *tag) {
  legacy_state *ls;
  grpc_ioreq req;
  grpc_call_error err;

  grpc_cq_begin_op(call->cq, call, GRPC_READ);

  lock(call);
  ls = get_legacy_state(call);
  req.op = GRPC_IOREQ_RECV_MESSAGE;
  req.data.recv_message = &ls->msg_in;
  err = start_ioreq(call, &req, 1, finish_read, tag);
  unlock(call);
  return err;
}

static void finish_write(grpc_call *call, grpc_op_error status, void *tag) {
  lock(call);
  grpc_byte_buffer_destroy(get_legacy_state(call)->msg_out);
  unlock(call);
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
  ls->msg_out = grpc_byte_buffer_copy(byte_buffer);
  req.op = GRPC_IOREQ_SEND_MESSAGE;
  req.data.send_message = ls->msg_out;
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
  grpc_ioreq reqs[3];
  grpc_call_error err;
  legacy_state *ls;
  grpc_cq_begin_op(call->cq, call, GRPC_FINISH_ACCEPTED);

  lock(call);
  ls = get_legacy_state(call);
  reqs[0].op = GRPC_IOREQ_SEND_TRAILING_METADATA;
  reqs[0].data.send_metadata.count = ls->md_out_count[ls->md_out_buffer];
  reqs[0].data.send_metadata.metadata = ls->md_out[ls->md_out_buffer];
  reqs[1].op = GRPC_IOREQ_SEND_STATUS;
  reqs[1].data.send_status.code = status;
  /* MEMLEAK */
  reqs[1].data.send_status.details = gpr_strdup(details);
  reqs[2].op = GRPC_IOREQ_SEND_CLOSE;
  err = start_ioreq(call, reqs, 3, finish_finish, tag);
  unlock(call);

  return err;
}
