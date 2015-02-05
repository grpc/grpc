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

typedef struct legacy_state legacy_state;
static void destroy_legacy_state(legacy_state *ls);

typedef enum { REQ_INITIAL = 0, REQ_READY, REQ_DONE } req_state;

typedef enum {
  SEND_NOTHING,
  SEND_INITIAL_METADATA,
  SEND_BUFFERED_INITIAL_METADATA,
  SEND_MESSAGE,
  SEND_BUFFERED_MESSAGE,
  SEND_TRAILING_METADATA_AND_FINISH,
  SEND_FINISH
} send_action;

typedef struct {
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  grpc_op_error status;
} completed_request;

/* See request_set in grpc_call below for a description */
#define REQSET_EMPTY 255
#define REQSET_DONE 254

typedef struct {
  /* Overall status of the operation: starts OK, may degrade to
     non-OK */
  grpc_op_error status;
  /* Completion function to call at the end of the operation */
  grpc_ioreq_completion_func on_complete;
  void *user_data;
  /* a bit mask of which request ops are needed (1u << opid) */
  gpr_uint32 need_mask;
  /* a bit mask of which request ops are now completed */
  gpr_uint32 complete_mask;
} reqinfo_master;

/* Status data for a request can come from several sources; this
   enumerates them all, and acts as a priority sorting for which
   status to return to the application - earlier entries override
   later ones */
typedef enum {
  /* Status came from the application layer overriding whatever
     the wire says */
  STATUS_FROM_API_OVERRIDE = 0,
  /* Status came from 'the wire' - or somewhere below the surface
     layer */
  STATUS_FROM_WIRE,
  STATUS_SOURCE_COUNT
} status_source;

typedef struct {
  gpr_uint8 is_set;
  grpc_status_code code;
  grpc_mdstr *details;
} received_status;

/* How far through the GRPC stream have we read? */
typedef enum {
  /* We are still waiting for initial metadata to complete */
  READ_STATE_INITIAL = 0,
  /* We have gotten initial metadata, and are reading either
     messages or trailing metadata */
  READ_STATE_GOT_INITIAL_METADATA,
  /* The stream is closed for reading */
  READ_STATE_READ_CLOSED,
  /* The stream is closed for reading & writing */
  READ_STATE_STREAM_CLOSED
} read_state;

typedef enum {
  WRITE_STATE_INITIAL = 0,
  WRITE_STATE_STARTED,
  WRITE_STATE_WRITE_CLOSED
} write_state;

struct grpc_call {
  grpc_completion_queue *cq;
  grpc_channel *channel;
  grpc_mdctx *metadata_context;
  /* TODO(ctiller): share with cq if possible? */
  gpr_mu mu;

  /* how far through the stream have we read? */
  read_state read_state;
  /* how far through the stream have we written? */
  write_state write_state;
  /* client or server call */
  gpr_uint8 is_client;
  /* is the alarm set */
  gpr_uint8 have_alarm;
  /* are we currently performing a send operation */
  gpr_uint8 sending;
  /* pairs with completed_requests */
  gpr_uint8 num_completed_requests;
  /* flag that we need to request more data */
  gpr_uint8 need_more_data;

  /* Active ioreqs.
     request_set and request_data contain one element per active ioreq
     operation.

     request_set[op] is an integer specifying a set of operations to which
     the request belongs:
       - if it is < GRPC_IOREQ_OP_COUNT, then this operation is pending
         completion, and the integer represents to which group of operations
         the ioreq belongs. Each group is represented by one master, and the
         integer in request_set is an index into masters to find the master
         data.
       - if it is REQSET_EMPTY, the ioreq op is inactive and available to be
         started
       - finally, if request_set[op] is REQSET_DONE, then the operation is
         complete and unavailable to be started again

     request_data[op] is the request data as supplied by the initiator of
     a request, and is valid iff request_set[op] <= GRPC_IOREQ_OP_COUNT.
     The set fields are as per the request type specified by op.

     Finally, one element of masters is set per active _set_ of ioreq
     operations. It describes work left outstanding, result status, and
     what work to perform upon operation completion. As one ioreq of each
     op type can be active at once, by convention we choose the first element
     of the group to be the master -- ie the master of in-progress operation
     op is masters[request_set[op]]. This allows constant time allocation
     and a strong upper bound of a count of masters to be calculated. */
  gpr_uint8 request_set[GRPC_IOREQ_OP_COUNT];
  grpc_ioreq_data request_data[GRPC_IOREQ_OP_COUNT];
  reqinfo_master masters[GRPC_IOREQ_OP_COUNT];

  /* Dynamic array of ioreq's that have completed: the count of
     elements is queued in num_completed_requests.
     This list is built up under lock(), and flushed entirely during
     unlock().
     We know the upper bound of the number of elements as we can only
     have one ioreq of each type active at once. */
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  /* Incoming buffer of messages */
  grpc_byte_buffer_queue incoming_queue;
  /* Buffered read metadata waiting to be returned to the application.
     Element 0 is initial metadata, element 1 is trailing metadata. */
  grpc_metadata_array buffered_metadata[2];
  /* All metadata received - unreffed at once at the end of the call */
  grpc_mdelem **owned_metadata;
  size_t owned_metadata_count;
  size_t owned_metadata_capacity;

  /* Received call statuses from various sources */
  received_status status[STATUS_SOURCE_COUNT];

  /* Deadline alarm - if have_alarm is non-zero */
  grpc_alarm alarm;

  /* Call refcount - to keep the call alive during asynchronous operations */
  gpr_refcount internal_refcount;

  /* Data that the legacy api needs to track. To be deleted at some point
     soon */
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
    call->request_set[i] = REQSET_EMPTY;
  }
  if (call->is_client) {
    call->request_set[GRPC_IOREQ_SEND_TRAILING_METADATA] = REQSET_DONE;
    call->request_set[GRPC_IOREQ_SEND_STATUS] = REQSET_DONE;
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
  for (i = 0; i < GPR_ARRAY_SIZE(c->buffered_metadata); i++) {
    gpr_free(c->buffered_metadata[i].metadata);
  }
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
  call->status[source].is_set = 1;
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

static int is_op_live(grpc_call *call, grpc_ioreq_op op) {
  gpr_uint8 set = call->request_set[op];
  reqinfo_master *master;
  if (set >= GRPC_IOREQ_OP_COUNT) return 0;
  master = &call->masters[set];
  return (master->complete_mask & (1u << op)) == 0;
}

static void lock(grpc_call *call) { gpr_mu_lock(&call->mu); }

static void unlock(grpc_call *call) {
  send_action sa = SEND_NOTHING;
  completed_request completed_requests[GRPC_IOREQ_OP_COUNT];
  int num_completed_requests = call->num_completed_requests;
  int need_more_data = call->need_more_data &&
                       !is_op_live(call, GRPC_IOREQ_SEND_INITIAL_METADATA);
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
    if (call->status[i].is_set) {
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

static void finish_live_ioreq_op(grpc_call *call, grpc_ioreq_op op,
                                 grpc_op_error status) {
  completed_request *cr;
  gpr_uint8 master_set = call->request_set[op];
  reqinfo_master *master;
  size_t i;
  /* ioreq is live: we need to do something */
  master = &call->masters[master_set];
  master->complete_mask |= 1u << op;
  if (status != GRPC_OP_OK) {
    master->status = status;
    master->complete_mask = master->need_mask;
  }
  if (master->complete_mask == master->need_mask) {
    for (i = 0; i < GRPC_IOREQ_OP_COUNT; i++) {
      if (call->request_set[i] != master_set) {
        continue;
      }
      call->request_set[i] = REQSET_DONE;
      switch ((grpc_ioreq_op)i) {
        case GRPC_IOREQ_RECV_MESSAGE:
        case GRPC_IOREQ_SEND_MESSAGE:
          if (master->status == GRPC_OP_OK) {
            call->request_set[i] = REQSET_EMPTY;
          } else {
            call->write_state = WRITE_STATE_WRITE_CLOSED;
          }
          break;
        case GRPC_IOREQ_RECV_CLOSE:
        case GRPC_IOREQ_SEND_INITIAL_METADATA:
        case GRPC_IOREQ_SEND_TRAILING_METADATA:
        case GRPC_IOREQ_SEND_STATUS:
        case GRPC_IOREQ_SEND_CLOSE:
          break;
        case GRPC_IOREQ_RECV_STATUS:
          get_final_status(
              call, call->request_data[GRPC_IOREQ_RECV_STATUS].recv_status);
          break;
        case GRPC_IOREQ_RECV_INITIAL_METADATA:
          SWAP(grpc_metadata_array, call->buffered_metadata[0],
               *call->request_data[GRPC_IOREQ_RECV_INITIAL_METADATA]
                    .recv_metadata);
          break;
        case GRPC_IOREQ_RECV_TRAILING_METADATA:
          SWAP(grpc_metadata_array, call->buffered_metadata[1],
               *call->request_data[GRPC_IOREQ_RECV_TRAILING_METADATA]
                    .recv_metadata);
          break;
        case GRPC_IOREQ_OP_COUNT:
          abort();
          break;
      }
    }
    cr = &call->completed_requests[call->num_completed_requests++];
    cr->status = master->status;
    cr->on_complete = master->on_complete;
    cr->user_data = master->user_data;
  }
}

static void finish_ioreq_op(grpc_call *call, grpc_ioreq_op op,
                            grpc_op_error status) {
  if (is_op_live(call, op)) {
    finish_live_ioreq_op(call, op, status);
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
  switch (call->write_state) {
    case WRITE_STATE_INITIAL:
      if (is_op_live(call, GRPC_IOREQ_SEND_INITIAL_METADATA)) {
        call->write_state = WRITE_STATE_STARTED;
        return is_op_live(call, GRPC_IOREQ_SEND_MESSAGE) ||
                       is_op_live(call, GRPC_IOREQ_SEND_CLOSE)
                   ? SEND_BUFFERED_INITIAL_METADATA
                   : SEND_INITIAL_METADATA;
      }
      return SEND_NOTHING;
    case WRITE_STATE_STARTED:
      if (is_op_live(call, GRPC_IOREQ_SEND_MESSAGE)) {
        return is_op_live(call, GRPC_IOREQ_SEND_CLOSE) ? SEND_BUFFERED_MESSAGE
                                                       : SEND_MESSAGE;
      }
      if (is_op_live(call, GRPC_IOREQ_SEND_CLOSE)) {
        call->write_state = WRITE_STATE_WRITE_CLOSED;
        finish_ioreq_op(call, GRPC_IOREQ_SEND_TRAILING_METADATA, GRPC_OP_OK);
        finish_ioreq_op(call, GRPC_IOREQ_SEND_STATUS, GRPC_OP_OK);
        return call->is_client ? SEND_FINISH
                               : SEND_TRAILING_METADATA_AND_FINISH;
      }
      return SEND_NOTHING;
    case WRITE_STATE_WRITE_CLOSED:
      return SEND_NOTHING;
  }
  gpr_log(GPR_ERROR, "should never reach here");
  abort();
  return SEND_NOTHING;
}

static void send_metadata(grpc_call *call, grpc_mdelem *elem) {
  grpc_call_op op;
  op.type = GRPC_SEND_METADATA;
  op.dir = GRPC_CALL_DOWN;
  op.flags = GRPC_WRITE_BUFFER_HINT;
  op.data.metadata = elem;
  op.done_cb = do_nothing;
  op.user_data = NULL;
  grpc_call_execute_op(call, &op);
}

static void enact_send_action(grpc_call *call, send_action sa) {
  grpc_ioreq_data data;
  grpc_call_op op;
  size_t i;
  gpr_uint32 flags = 0;
  char status_str[GPR_LTOA_MIN_BUFSIZE];

  switch (sa) {
    case SEND_NOTHING:
      abort();
      break;
    case SEND_BUFFERED_INITIAL_METADATA:
      flags |= GRPC_WRITE_BUFFER_HINT;
    /* fallthrough */
    case SEND_INITIAL_METADATA:
      data = call->request_data[GRPC_IOREQ_SEND_INITIAL_METADATA];
      for (i = 0; i < data.send_metadata.count; i++) {
        const grpc_metadata *md = &data.send_metadata.metadata[i];
        send_metadata(call,
                      grpc_mdelem_from_string_and_buffer(
                          call->metadata_context, md->key,
                          (const gpr_uint8 *)md->value, md->value_length));
      }
      op.type = GRPC_SEND_START;
      op.dir = GRPC_CALL_DOWN;
      op.flags = flags;
      op.data.start.pollset = grpc_cq_pollset(call->cq);
      op.done_cb = finish_start_step;
      op.user_data = call;
      grpc_call_execute_op(call, &op);
      break;
    case SEND_BUFFERED_MESSAGE:
      flags |= GRPC_WRITE_BUFFER_HINT;
    /* fallthrough */
    case SEND_MESSAGE:
      data = call->request_data[GRPC_IOREQ_SEND_MESSAGE];
      op.type = GRPC_SEND_MESSAGE;
      op.dir = GRPC_CALL_DOWN;
      op.flags = flags;
      op.data.message = data.send_message;
      op.done_cb = finish_write_step;
      op.user_data = call;
      grpc_call_execute_op(call, &op);
      break;
    case SEND_TRAILING_METADATA_AND_FINISH:
      /* send trailing metadata */
      data = call->request_data[GRPC_IOREQ_SEND_TRAILING_METADATA];
      for (i = 0; i < data.send_metadata.count; i++) {
        const grpc_metadata *md = &data.send_metadata.metadata[i];
        send_metadata(call,
                      grpc_mdelem_from_string_and_buffer(
                          call->metadata_context, md->key,
                          (const gpr_uint8 *)md->value, md->value_length));
      }
      /* send status */
      /* TODO(ctiller): cache common status values */
      data = call->request_data[GRPC_IOREQ_SEND_STATUS];
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
    if (mutated_ops & (1u << i)) {
      call->request_set[i] = REQSET_EMPTY;
    }
  }
  return ret;
}

static void finish_read_ops(grpc_call *call) {
  int empty;

  if (is_op_live(call, GRPC_IOREQ_RECV_MESSAGE)) {
    empty =
        (NULL == (*call->request_data[GRPC_IOREQ_RECV_MESSAGE].recv_message =
                      grpc_bbq_pop(&call->incoming_queue)));
    if (!empty) {
      finish_live_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, GRPC_OP_OK);
      empty = grpc_bbq_empty(&call->incoming_queue);
    }
  } else {
    empty = grpc_bbq_empty(&call->incoming_queue);
  }

  switch (call->read_state) {
    case READ_STATE_STREAM_CLOSED:
      if (empty) {
        finish_ioreq_op(call, GRPC_IOREQ_RECV_CLOSE, GRPC_OP_OK);
      }
    /* fallthrough */
    case READ_STATE_READ_CLOSED:
      if (empty) {
        finish_ioreq_op(call, GRPC_IOREQ_RECV_MESSAGE, GRPC_OP_OK);
      }
      finish_ioreq_op(call, GRPC_IOREQ_RECV_STATUS, GRPC_OP_OK);
      finish_ioreq_op(call, GRPC_IOREQ_RECV_TRAILING_METADATA, GRPC_OP_OK);
    /* fallthrough */
    case READ_STATE_GOT_INITIAL_METADATA:
      finish_ioreq_op(call, GRPC_IOREQ_RECV_INITIAL_METADATA, GRPC_OP_OK);
    /* fallthrough */
    case READ_STATE_INITIAL:
      /* do nothing */
      break;
  }
}

static void early_out_write_ops(grpc_call *call) {
  switch (call->write_state) {
    case WRITE_STATE_WRITE_CLOSED:
      finish_ioreq_op(call, GRPC_IOREQ_SEND_MESSAGE, GRPC_OP_ERROR);
      finish_ioreq_op(call, GRPC_IOREQ_SEND_STATUS, GRPC_OP_ERROR);
      finish_ioreq_op(call, GRPC_IOREQ_SEND_TRAILING_METADATA, GRPC_OP_ERROR);
      finish_ioreq_op(call, GRPC_IOREQ_SEND_CLOSE, GRPC_OP_OK);
    /* fallthrough */
    case WRITE_STATE_STARTED:
      finish_ioreq_op(call, GRPC_IOREQ_SEND_INITIAL_METADATA, GRPC_OP_ERROR);
    /* fallthrough */
    case WRITE_STATE_INITIAL:
      /* do nothing */
      break;
  }
}

static grpc_call_error start_ioreq(grpc_call *call, const grpc_ioreq *reqs,
                                   size_t nreqs,
                                   grpc_ioreq_completion_func completion,
                                   void *user_data) {
  size_t i;
  gpr_uint32 have_ops = 0;
  grpc_ioreq_op op;
  reqinfo_master *master;
  grpc_ioreq_data data;
  gpr_uint8 set;

  if (nreqs == 0) {
    return GRPC_CALL_OK;
  }

  set = reqs[0].op;

  for (i = 0; i < nreqs; i++) {
    op = reqs[i].op;
    if (call->request_set[op] < GRPC_IOREQ_OP_COUNT) {
      return start_ioreq_error(call, have_ops,
                               GRPC_CALL_ERROR_TOO_MANY_OPERATIONS);
    } else if (call->request_set[op] == REQSET_DONE) {
      return start_ioreq_error(call, have_ops, GRPC_CALL_ERROR_ALREADY_INVOKED);
    }
    have_ops |= 1u << op;
    data = reqs[i].data;

    call->request_data[op] = data;
    call->request_set[op] = set;
  }

  master = &call->masters[set];
  master->status = GRPC_OP_OK;
  master->need_mask = have_ops;
  master->complete_mask = 0;
  master->on_complete = completion;
  master->user_data = user_data;

  if (have_ops & (1u << GRPC_IOREQ_RECV_MESSAGE)) {
    call->need_more_data = 1;
  }

  finish_read_ops(call);
  early_out_write_ops(call);

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
  cancel = c->read_state != READ_STATE_STREAM_CLOSED;
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

static void set_read_state(grpc_call *call, read_state state) {
  lock(call);
  GPR_ASSERT(call->read_state < state);
  call->read_state = state;
  finish_read_ops(call);
  unlock(call);
}

void grpc_call_read_closed(grpc_call_element *elem) {
  set_read_state(CALL_FROM_TOP_ELEM(elem), READ_STATE_READ_CLOSED);
}

void grpc_call_stream_closed(grpc_call_element *elem) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  set_read_state(call, READ_STATE_STREAM_CLOSED);
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
  grpc_bbq_push(&call->incoming_queue, byte_buffer);
  finish_read_ops(call);
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
    dest = &call->buffered_metadata[call->read_state >=
                                    READ_STATE_GOT_INITIAL_METADATA];
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

void grpc_call_initial_metadata_complete(grpc_call_element *surface_element) {
  grpc_call *call = grpc_call_from_top_element(surface_element);
  set_read_state(call, READ_STATE_GOT_INITIAL_METADATA);
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

grpc_call_error grpc_call_add_metadata_old(grpc_call *call,
                                           grpc_metadata *metadata,
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

grpc_call_error grpc_call_invoke_old(grpc_call *call, grpc_completion_queue *cq,
                                     void *metadata_read_tag,
                                     void *finished_tag, gpr_uint32 flags) {
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

grpc_call_error grpc_call_server_accept_old(grpc_call *call,
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

grpc_call_error grpc_call_server_end_initial_metadata_old(grpc_call *call,
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

grpc_call_error grpc_call_start_read_old(grpc_call *call, void *tag) {
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

grpc_call_error grpc_call_start_write_old(grpc_call *call,
                                          grpc_byte_buffer *byte_buffer,
                                          void *tag, gpr_uint32 flags) {
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

grpc_call_error grpc_call_writes_done_old(grpc_call *call, void *tag) {
  grpc_ioreq req;
  grpc_call_error err;
  grpc_cq_begin_op(call->cq, call, GRPC_FINISH_ACCEPTED);

  lock(call);
  req.op = GRPC_IOREQ_SEND_CLOSE;
  err = start_ioreq(call, &req, 1, finish_finish, tag);
  unlock(call);

  return err;
}

grpc_call_error grpc_call_start_write_status_old(grpc_call *call,
                                                 grpc_status_code status,
                                                 const char *details,
                                                 void *tag) {
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
