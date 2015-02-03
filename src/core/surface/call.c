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

#define INVALID_TAG ((void *)0xdeadbeef)

/* Pending read queue

   This data structure tracks reads that need to be presented to the completion
   queue but are waiting for the application to ask for them. */

#define INITIAL_PENDING_READ_COUNT 4

typedef struct {
  grpc_byte_buffer *byte_buffer;
  void *user_data;
  void (*on_finish)(void *user_data, grpc_op_error error);
} pending_read;

/* TODO(ctiller): inline an element or two into this struct to avoid per-call
                  allocations */
typedef struct {
  pending_read *data;
  size_t count;
  size_t capacity;
} pending_read_array;

typedef struct {
  size_t drain_pos;
  pending_read_array filling;
  pending_read_array draining;
} pending_read_queue;

static void pra_init(pending_read_array *array) {
  array->data = gpr_malloc(sizeof(pending_read) * INITIAL_PENDING_READ_COUNT);
  array->count = 0;
  array->capacity = INITIAL_PENDING_READ_COUNT;
}

static void pra_destroy(pending_read_array *array,
                        size_t finish_starting_from) {
  size_t i;
  for (i = finish_starting_from; i < array->count; i++) {
    array->data[i].on_finish(array->data[i].user_data, GRPC_OP_ERROR);
  }
  gpr_free(array->data);
}

/* Append an operation to an array, expanding as needed */
static void pra_push(pending_read_array *a, grpc_byte_buffer *buffer,
                     void (*on_finish)(void *user_data, grpc_op_error error),
                     void *user_data) {
  if (a->count == a->capacity) {
    a->capacity *= 2;
    a->data = gpr_realloc(a->data, sizeof(pending_read) * a->capacity);
  }
  a->data[a->count].byte_buffer = buffer;
  a->data[a->count].user_data = user_data;
  a->data[a->count].on_finish = on_finish;
  a->count++;
}

static void prq_init(pending_read_queue *q) {
  q->drain_pos = 0;
  pra_init(&q->filling);
  pra_init(&q->draining);
}

static void prq_destroy(pending_read_queue *q) {
  pra_destroy(&q->filling, 0);
  pra_destroy(&q->draining, q->drain_pos);
}

static int prq_is_empty(pending_read_queue *q) {
  return (q->drain_pos == q->draining.count && q->filling.count == 0);
}

static void prq_push(pending_read_queue *q, grpc_byte_buffer *buffer,
                     void (*on_finish)(void *user_data, grpc_op_error error),
                     void *user_data) {
  pra_push(&q->filling, buffer, on_finish, user_data);
}

/* Take the first queue element and move it to the completion queue. Do nothing
   if q is empty */
static int prq_pop_to_cq(pending_read_queue *q, void *tag, grpc_call *call,
                         grpc_completion_queue *cq) {
  pending_read_array temp_array;
  pending_read *pr;

  if (q->drain_pos == q->draining.count) {
    if (q->filling.count == 0) {
      return 0;
    }
    q->draining.count = 0;
    q->drain_pos = 0;
    /* swap arrays */
    temp_array = q->filling;
    q->filling = q->draining;
    q->draining = temp_array;
  }

  pr = q->draining.data + q->drain_pos;
  q->drain_pos++;
  grpc_cq_end_read(cq, tag, call, pr->on_finish, pr->user_data,
                   pr->byte_buffer);
  return 1;
}

/* grpc_call proper */

/* the state of a call, based upon which functions have been called against
   said call */
typedef enum {
  CALL_CREATED,
  CALL_BOUNDCQ,
  CALL_STARTED,
  CALL_FINISHED
} call_state;

struct grpc_call {
  grpc_completion_queue *cq;
  grpc_channel *channel;
  grpc_mdctx *metadata_context;

  call_state state;
  gpr_uint8 is_client;
  gpr_uint8 have_write;
  grpc_metadata_buffer incoming_metadata;

  /* protects variables in this section */
  gpr_mu read_mu;
  gpr_uint8 received_start;
  gpr_uint8 start_ok;
  gpr_uint8 reads_done;
  gpr_uint8 received_finish;
  gpr_uint8 received_metadata;
  gpr_uint8 have_read;
  gpr_uint8 have_alarm;
  gpr_uint8 pending_writes_done;
  gpr_uint8 got_status_code;
  /* The current outstanding read message tag (only valid if have_read == 1) */
  void *read_tag;
  void *metadata_tag;
  void *finished_tag;
  pending_read_queue prq;

  grpc_alarm alarm;

  /* The current outstanding send message/context/invoke/end tag (only valid if
     have_write == 1) */
  void *write_tag;
  grpc_byte_buffer *pending_write;
  gpr_uint32 pending_write_flags;

  /* The final status of the call */
  grpc_status_code status_code;
  grpc_mdstr *status_details;

  gpr_refcount internal_refcount;
};

#define CALL_STACK_FROM_CALL(call) ((grpc_call_stack *)((call) + 1))
#define CALL_FROM_CALL_STACK(call_stack) (((grpc_call *)(call_stack)) - 1)
#define CALL_ELEM_FROM_CALL(call, idx) \
  grpc_call_stack_element(CALL_STACK_FROM_CALL(call), idx)
#define CALL_FROM_TOP_ELEM(top_elem) \
  CALL_FROM_CALL_STACK(grpc_call_stack_from_top_element(top_elem))

static void do_nothing(void *ignored, grpc_op_error also_ignored) {}

grpc_call *grpc_call_create(grpc_channel *channel,
                            const void *server_transport_data) {
  grpc_channel_stack *channel_stack = grpc_channel_get_channel_stack(channel);
  grpc_call *call =
      gpr_malloc(sizeof(grpc_call) + channel_stack->call_stack_size);
  call->cq = NULL;
  call->channel = channel;
  grpc_channel_internal_ref(channel);
  call->metadata_context = grpc_channel_get_metadata_context(channel);
  call->state = CALL_CREATED;
  call->is_client = (server_transport_data == NULL);
  call->write_tag = INVALID_TAG;
  call->read_tag = INVALID_TAG;
  call->metadata_tag = INVALID_TAG;
  call->finished_tag = INVALID_TAG;
  call->have_read = 0;
  call->have_write = 0;
  call->have_alarm = 0;
  call->received_metadata = 0;
  call->got_status_code = 0;
  call->start_ok = 0;
  call->status_code =
      server_transport_data != NULL ? GRPC_STATUS_OK : GRPC_STATUS_UNKNOWN;
  call->status_details = NULL;
  call->received_finish = 0;
  call->reads_done = 0;
  call->received_start = 0;
  call->pending_write = NULL;
  call->pending_writes_done = 0;
  grpc_metadata_buffer_init(&call->incoming_metadata);
  gpr_ref_init(&call->internal_refcount, 1);
  grpc_call_stack_init(channel_stack, server_transport_data,
                       CALL_STACK_FROM_CALL(call));
  prq_init(&call->prq);
  gpr_mu_init(&call->read_mu);
  return call;
}

void grpc_call_internal_ref(grpc_call *c) { gpr_ref(&c->internal_refcount); }

void grpc_call_internal_unref(grpc_call *c) {
  if (gpr_unref(&c->internal_refcount)) {
    grpc_call_stack_destroy(CALL_STACK_FROM_CALL(c));
    grpc_metadata_buffer_destroy(&c->incoming_metadata, GRPC_OP_OK);
    if (c->status_details) {
      grpc_mdstr_unref(c->status_details);
    }
    prq_destroy(&c->prq);
    gpr_mu_destroy(&c->read_mu);
    grpc_channel_internal_unref(c->channel);
    gpr_free(c);
  }
}

void grpc_call_destroy(grpc_call *c) {
  int cancel;
  gpr_mu_lock(&c->read_mu);
  if (c->have_alarm) {
    grpc_alarm_cancel(&c->alarm);
    c->have_alarm = 0;
  }
  cancel = !c->received_finish;
  gpr_mu_unlock(&c->read_mu);
  if (cancel) grpc_call_cancel(c);
  grpc_call_internal_unref(c);
}

static void maybe_set_status_code(grpc_call *call, gpr_uint32 status) {
  if (!call->got_status_code) {
    call->status_code = status;
    call->got_status_code = 1;
  }
}

static void maybe_set_status_details(grpc_call *call, grpc_mdstr *status) {
  if (!call->status_details) {
    call->status_details = grpc_mdstr_ref(status);
  }
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
  gpr_mu_lock(&c->read_mu);
  maybe_set_status_code(c, status);
  if (details) {
    maybe_set_status_details(c, details);
  }
  gpr_mu_unlock(&c->read_mu);
  return grpc_call_cancel(c);
}

void grpc_call_execute_op(grpc_call *call, grpc_call_op *op) {
  grpc_call_element *elem;
  GPR_ASSERT(op->dir == GRPC_CALL_DOWN);
  elem = CALL_ELEM_FROM_CALL(call, 0);
  elem->filter->call_op(elem, NULL, op);
}

void grpc_call_add_mdelem(grpc_call *call, grpc_mdelem *mdelem,
                          gpr_uint32 flags) {
  grpc_call_element *elem;
  grpc_call_op op;

  GPR_ASSERT(call->state < CALL_FINISHED);

  op.type = GRPC_SEND_METADATA;
  op.dir = GRPC_CALL_DOWN;
  op.flags = flags;
  op.done_cb = do_nothing;
  op.user_data = NULL;
  op.data.metadata = mdelem;

  elem = CALL_ELEM_FROM_CALL(call, 0);
  elem->filter->call_op(elem, NULL, &op);
}

grpc_call_error grpc_call_add_metadata_old(grpc_call *call,
                                           grpc_metadata *metadata,
                                           gpr_uint32 flags) {
  grpc_mdelem *mdelem;

  if (call->is_client) {
    if (call->state >= CALL_STARTED) {
      return GRPC_CALL_ERROR_ALREADY_INVOKED;
    }
  } else {
    if (call->state >= CALL_FINISHED) {
      return GRPC_CALL_ERROR_ALREADY_FINISHED;
    }
  }

  mdelem = grpc_mdelem_from_string_and_buffer(
      call->metadata_context, metadata->key, (gpr_uint8 *)metadata->value,
      metadata->value_length);
  grpc_call_add_mdelem(call, mdelem, flags);
  return GRPC_CALL_OK;
}

static void finish_call(grpc_call *call) {
  size_t count;
  grpc_metadata *elements;
  count = grpc_metadata_buffer_count(&call->incoming_metadata);
  elements = grpc_metadata_buffer_extract_elements(&call->incoming_metadata);
  grpc_cq_end_finished(
      call->cq, call->finished_tag, call, grpc_metadata_buffer_cleanup_elements,
      elements, call->status_code,
      call->status_details
          ? (char *)grpc_mdstr_as_c_string(call->status_details)
          : NULL,
      elements, count);
}

static void done_write(void *user_data, grpc_op_error error) {
  grpc_call *call = user_data;
  void *tag = call->write_tag;

  GPR_ASSERT(call->have_write);
  call->have_write = 0;
  call->write_tag = INVALID_TAG;
  grpc_cq_end_write_accepted(call->cq, tag, call, NULL, NULL, error);
}

static void done_writes_done(void *user_data, grpc_op_error error) {
  grpc_call *call = user_data;
  void *tag = call->write_tag;

  GPR_ASSERT(call->have_write);
  call->have_write = 0;
  call->write_tag = INVALID_TAG;
  grpc_cq_end_finish_accepted(call->cq, tag, call, NULL, NULL, error);
}

static void call_started(void *user_data, grpc_op_error error) {
  grpc_call *call = user_data;
  grpc_call_element *elem;
  grpc_byte_buffer *pending_write = NULL;
  gpr_uint32 pending_write_flags = 0;
  gpr_uint8 pending_writes_done = 0;
  int ok;
  grpc_call_op op;

  gpr_mu_lock(&call->read_mu);
  GPR_ASSERT(!call->received_start);
  call->received_start = 1;
  ok = call->start_ok = (error == GRPC_OP_OK);
  pending_write = call->pending_write;
  pending_write_flags = call->pending_write_flags;
  pending_writes_done = call->pending_writes_done;
  gpr_mu_unlock(&call->read_mu);

  if (pending_write) {
    if (ok) {
      op.type = GRPC_SEND_MESSAGE;
      op.dir = GRPC_CALL_DOWN;
      op.flags = pending_write_flags;
      op.done_cb = done_write;
      op.user_data = call;
      op.data.message = pending_write;

      elem = CALL_ELEM_FROM_CALL(call, 0);
      elem->filter->call_op(elem, NULL, &op);
    } else {
      done_write(call, error);
    }
    grpc_byte_buffer_destroy(pending_write);
  }
  if (pending_writes_done) {
    if (ok) {
      op.type = GRPC_SEND_FINISH;
      op.dir = GRPC_CALL_DOWN;
      op.flags = 0;
      op.done_cb = done_writes_done;
      op.user_data = call;

      elem = CALL_ELEM_FROM_CALL(call, 0);
      elem->filter->call_op(elem, NULL, &op);
    } else {
      done_writes_done(call, error);
    }
  }

  grpc_call_internal_unref(call);
}

grpc_call_error grpc_call_invoke_old(grpc_call *call, grpc_completion_queue *cq,
                                     void *metadata_read_tag,
                                     void *finished_tag, gpr_uint32 flags) {
  grpc_call_element *elem;
  grpc_call_op op;

  /* validate preconditions */
  if (!call->is_client) {
    gpr_log(GPR_ERROR, "can only call %s on clients", __FUNCTION__);
    return GRPC_CALL_ERROR_NOT_ON_SERVER;
  }

  if (call->state >= CALL_STARTED || call->cq) {
    gpr_log(GPR_ERROR, "call is already invoked");
    return GRPC_CALL_ERROR_ALREADY_INVOKED;
  }

  if (call->have_write) {
    gpr_log(GPR_ERROR, "can only have one pending write operation at a time");
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }

  if (call->have_read) {
    gpr_log(GPR_ERROR, "can only have one pending read operation at a time");
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }

  if (flags & GRPC_WRITE_NO_COMPRESS) {
    return GRPC_CALL_ERROR_INVALID_FLAGS;
  }

  /* inform the completion queue of an incoming operation */
  grpc_cq_begin_op(cq, call, GRPC_FINISHED);
  grpc_cq_begin_op(cq, call, GRPC_CLIENT_METADATA_READ);

  gpr_mu_lock(&call->read_mu);

  /* update state */
  call->cq = cq;
  call->state = CALL_STARTED;
  call->finished_tag = finished_tag;

  if (call->received_finish) {
    /* handle early cancellation */
    grpc_cq_end_client_metadata_read(call->cq, metadata_read_tag, call, NULL,
                                     NULL, 0, NULL);
    finish_call(call);

    /* early out.. unlock & return */
    gpr_mu_unlock(&call->read_mu);
    return GRPC_CALL_OK;
  }

  call->metadata_tag = metadata_read_tag;

  gpr_mu_unlock(&call->read_mu);

  /* call down the filter stack */
  op.type = GRPC_SEND_START;
  op.dir = GRPC_CALL_DOWN;
  op.flags = flags;
  op.done_cb = call_started;
  op.data.start.pollset = grpc_cq_pollset(cq);
  op.user_data = call;
  grpc_call_internal_ref(call);

  elem = CALL_ELEM_FROM_CALL(call, 0);
  elem->filter->call_op(elem, NULL, &op);

  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_server_accept_old(grpc_call *call,
                                            grpc_completion_queue *cq,
                                            void *finished_tag) {
  /* validate preconditions */
  if (call->is_client) {
    gpr_log(GPR_ERROR, "can only call %s on servers", __FUNCTION__);
    return GRPC_CALL_ERROR_NOT_ON_CLIENT;
  }

  if (call->state >= CALL_BOUNDCQ) {
    gpr_log(GPR_ERROR, "call is already accepted");
    return GRPC_CALL_ERROR_ALREADY_ACCEPTED;
  }

  /* inform the completion queue of an incoming operation (corresponding to
     finished_tag) */
  grpc_cq_begin_op(cq, call, GRPC_FINISHED);

  /* update state */
  gpr_mu_lock(&call->read_mu);
  call->state = CALL_BOUNDCQ;
  call->cq = cq;
  call->finished_tag = finished_tag;
  call->received_start = 1;
  if (prq_is_empty(&call->prq) && call->received_finish) {
    finish_call(call);

    /* early out.. unlock & return */
    gpr_mu_unlock(&call->read_mu);
    return GRPC_CALL_OK;
  }
  gpr_mu_unlock(&call->read_mu);

  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_server_end_initial_metadata_old(grpc_call *call,
                                                          gpr_uint32 flags) {
  grpc_call_element *elem;
  grpc_call_op op;

  /* validate preconditions */
  if (call->is_client) {
    gpr_log(GPR_ERROR, "can only call %s on servers", __FUNCTION__);
    return GRPC_CALL_ERROR_NOT_ON_CLIENT;
  }

  if (call->state >= CALL_STARTED) {
    gpr_log(GPR_ERROR, "call is already started");
    return GRPC_CALL_ERROR_ALREADY_INVOKED;
  }

  if (flags & GRPC_WRITE_NO_COMPRESS) {
    return GRPC_CALL_ERROR_INVALID_FLAGS;
  }

  /* update state */
  call->state = CALL_STARTED;

  /* call down */
  op.type = GRPC_SEND_START;
  op.dir = GRPC_CALL_DOWN;
  op.flags = flags;
  op.done_cb = do_nothing;
  op.data.start.pollset = grpc_cq_pollset(call->cq);
  op.user_data = NULL;

  elem = CALL_ELEM_FROM_CALL(call, 0);
  elem->filter->call_op(elem, NULL, &op);

  return GRPC_CALL_OK;
}

void grpc_call_client_initial_metadata_complete(
    grpc_call_element *surface_element) {
  grpc_call *call = grpc_call_from_top_element(surface_element);
  size_t count;
  grpc_metadata *elements;

  gpr_mu_lock(&call->read_mu);
  count = grpc_metadata_buffer_count(&call->incoming_metadata);
  elements = grpc_metadata_buffer_extract_elements(&call->incoming_metadata);

  GPR_ASSERT(!call->received_metadata);
  grpc_cq_end_client_metadata_read(call->cq, call->metadata_tag, call,
                                   grpc_metadata_buffer_cleanup_elements,
                                   elements, count, elements);
  call->received_metadata = 1;
  call->metadata_tag = INVALID_TAG;
  gpr_mu_unlock(&call->read_mu);
}

static void request_more_data(grpc_call *call) {
  grpc_call_element *elem;
  grpc_call_op op;

  /* call down */
  op.type = GRPC_REQUEST_DATA;
  op.dir = GRPC_CALL_DOWN;
  op.flags = 0;
  op.done_cb = do_nothing;
  op.user_data = NULL;

  elem = CALL_ELEM_FROM_CALL(call, 0);
  elem->filter->call_op(elem, NULL, &op);
}

grpc_call_error grpc_call_start_read_old(grpc_call *call, void *tag) {
  gpr_uint8 request_more = 0;

  switch (call->state) {
    case CALL_CREATED:
      return GRPC_CALL_ERROR_NOT_INVOKED;
    case CALL_BOUNDCQ:
    case CALL_STARTED:
      break;
    case CALL_FINISHED:
      return GRPC_CALL_ERROR_ALREADY_FINISHED;
  }

  gpr_mu_lock(&call->read_mu);

  if (call->have_read) {
    gpr_mu_unlock(&call->read_mu);
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }

  grpc_cq_begin_op(call->cq, call, GRPC_READ);

  if (!prq_pop_to_cq(&call->prq, tag, call, call->cq)) {
    if (call->reads_done) {
      grpc_cq_end_read(call->cq, tag, call, do_nothing, NULL, NULL);
    } else {
      call->read_tag = tag;
      call->have_read = 1;
      request_more = call->received_start;
    }
  } else if (prq_is_empty(&call->prq) && call->received_finish) {
    finish_call(call);
  }

  gpr_mu_unlock(&call->read_mu);

  if (request_more) {
    request_more_data(call);
  }

  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_start_write_old(grpc_call *call,
                                          grpc_byte_buffer *byte_buffer,
                                          void *tag, gpr_uint32 flags) {
  grpc_call_element *elem;
  grpc_call_op op;

  switch (call->state) {
    case CALL_CREATED:
    case CALL_BOUNDCQ:
      return GRPC_CALL_ERROR_NOT_INVOKED;
    case CALL_STARTED:
      break;
    case CALL_FINISHED:
      return GRPC_CALL_ERROR_ALREADY_FINISHED;
  }

  if (call->have_write) {
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }

  grpc_cq_begin_op(call->cq, call, GRPC_WRITE_ACCEPTED);

  /* TODO(ctiller): if flags & GRPC_WRITE_BUFFER_HINT == 0, this indicates a
     flush, and that flush should be propogated down from here */
  if (byte_buffer == NULL) {
    grpc_cq_end_write_accepted(call->cq, tag, call, NULL, NULL, GRPC_OP_OK);
    return GRPC_CALL_OK;
  }

  call->write_tag = tag;
  call->have_write = 1;

  gpr_mu_lock(&call->read_mu);
  if (!call->received_start) {
    call->pending_write = grpc_byte_buffer_copy(byte_buffer);
    call->pending_write_flags = flags;

    gpr_mu_unlock(&call->read_mu);
  } else {
    gpr_mu_unlock(&call->read_mu);

    op.type = GRPC_SEND_MESSAGE;
    op.dir = GRPC_CALL_DOWN;
    op.flags = flags;
    op.done_cb = done_write;
    op.user_data = call;
    op.data.message = byte_buffer;

    elem = CALL_ELEM_FROM_CALL(call, 0);
    elem->filter->call_op(elem, NULL, &op);
  }

  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_writes_done_old(grpc_call *call, void *tag) {
  grpc_call_element *elem;
  grpc_call_op op;

  if (!call->is_client) {
    return GRPC_CALL_ERROR_NOT_ON_SERVER;
  }

  switch (call->state) {
    case CALL_CREATED:
    case CALL_BOUNDCQ:
      return GRPC_CALL_ERROR_NOT_INVOKED;
    case CALL_FINISHED:
      return GRPC_CALL_ERROR_ALREADY_FINISHED;
    case CALL_STARTED:
      break;
  }

  if (call->have_write) {
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }

  grpc_cq_begin_op(call->cq, call, GRPC_FINISH_ACCEPTED);

  call->write_tag = tag;
  call->have_write = 1;

  gpr_mu_lock(&call->read_mu);
  if (!call->received_start) {
    call->pending_writes_done = 1;

    gpr_mu_unlock(&call->read_mu);
  } else {
    gpr_mu_unlock(&call->read_mu);

    op.type = GRPC_SEND_FINISH;
    op.dir = GRPC_CALL_DOWN;
    op.flags = 0;
    op.done_cb = done_writes_done;
    op.user_data = call;

    elem = CALL_ELEM_FROM_CALL(call, 0);
    elem->filter->call_op(elem, NULL, &op);
  }

  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_start_write_status_old(grpc_call *call,
                                                 grpc_status_code status,
                                                 const char *details,
                                                 void *tag) {
  grpc_call_element *elem;
  grpc_call_op op;

  if (call->is_client) {
    return GRPC_CALL_ERROR_NOT_ON_CLIENT;
  }

  switch (call->state) {
    case CALL_CREATED:
    case CALL_BOUNDCQ:
      return GRPC_CALL_ERROR_NOT_INVOKED;
    case CALL_FINISHED:
      return GRPC_CALL_ERROR_ALREADY_FINISHED;
    case CALL_STARTED:
      break;
  }

  if (call->have_write) {
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }

  elem = CALL_ELEM_FROM_CALL(call, 0);

  if (details && details[0]) {
    grpc_mdelem *md = grpc_mdelem_from_strings(call->metadata_context,
                                               "grpc-message", details);

    op.type = GRPC_SEND_METADATA;
    op.dir = GRPC_CALL_DOWN;
    op.flags = 0;
    op.done_cb = do_nothing;
    op.user_data = NULL;
    op.data.metadata = md;
    elem->filter->call_op(elem, NULL, &op);
  }

  /* always send status */
  {
    grpc_mdelem *md;
    char buffer[GPR_LTOA_MIN_BUFSIZE];
    gpr_ltoa(status, buffer);
    md =
        grpc_mdelem_from_strings(call->metadata_context, "grpc-status", buffer);

    op.type = GRPC_SEND_METADATA;
    op.dir = GRPC_CALL_DOWN;
    op.flags = 0;
    op.done_cb = do_nothing;
    op.user_data = NULL;
    op.data.metadata = md;
    elem->filter->call_op(elem, NULL, &op);
  }

  grpc_cq_begin_op(call->cq, call, GRPC_FINISH_ACCEPTED);

  call->state = CALL_FINISHED;
  call->write_tag = tag;
  call->have_write = 1;

  op.type = GRPC_SEND_FINISH;
  op.dir = GRPC_CALL_DOWN;
  op.flags = 0;
  op.done_cb = done_writes_done;
  op.user_data = call;

  elem->filter->call_op(elem, NULL, &op);

  return GRPC_CALL_OK;
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
    status = ((gpr_uint32)(gpr_intptr)user_data) - STATUS_OFFSET;
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

void grpc_call_recv_metadata(grpc_call_element *elem, grpc_call_op *op) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);
  grpc_mdelem *md = op->data.metadata;
  grpc_mdstr *key = md->key;

  if (key == grpc_channel_get_status_string(call->channel)) {
    maybe_set_status_code(call, decode_status(md));
    grpc_mdelem_unref(md);
    op->done_cb(op->user_data, GRPC_OP_OK);
  } else if (key == grpc_channel_get_message_string(call->channel)) {
    maybe_set_status_details(call, md->value);
    grpc_mdelem_unref(md);
    op->done_cb(op->user_data, GRPC_OP_OK);
  } else {
    grpc_metadata_buffer_queue(&call->incoming_metadata, op);
  }
}

void grpc_call_recv_finish(grpc_call_element *elem, int is_full_close) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);

  gpr_mu_lock(&call->read_mu);

  if (call->have_read) {
    grpc_cq_end_read(call->cq, call->read_tag, call, do_nothing, NULL, NULL);
    call->read_tag = INVALID_TAG;
    call->have_read = 0;
  }
  if (call->is_client && !call->received_metadata && call->cq) {
    size_t count;
    grpc_metadata *elements;

    call->received_metadata = 1;

    count = grpc_metadata_buffer_count(&call->incoming_metadata);
    elements = grpc_metadata_buffer_extract_elements(&call->incoming_metadata);
    grpc_cq_end_client_metadata_read(call->cq, call->metadata_tag, call,
                                     grpc_metadata_buffer_cleanup_elements,
                                     elements, count, elements);
  }
  if (is_full_close) {
    if (call->have_alarm) {
      grpc_alarm_cancel(&call->alarm);
      call->have_alarm = 0;
    }
    call->received_finish = 1;
    if (prq_is_empty(&call->prq) && call->cq != NULL) {
      finish_call(call);
    }
  } else {
    call->reads_done = 1;
  }
  gpr_mu_unlock(&call->read_mu);
}

void grpc_call_recv_message(grpc_call_element *elem, grpc_byte_buffer *message,
                            void (*on_finish)(void *user_data,
                                              grpc_op_error error),
                            void *user_data) {
  grpc_call *call = CALL_FROM_TOP_ELEM(elem);

  gpr_mu_lock(&call->read_mu);
  if (call->have_read) {
    grpc_cq_end_read(call->cq, call->read_tag, call, on_finish, user_data,
                     message);
    call->read_tag = INVALID_TAG;
    call->have_read = 0;
  } else {
    prq_push(&call->prq, message, on_finish, user_data);
  }
  gpr_mu_unlock(&call->read_mu);
}

grpc_call *grpc_call_from_top_element(grpc_call_element *elem) {
  return CALL_FROM_TOP_ELEM(elem);
}

grpc_metadata_buffer *grpc_call_get_metadata_buffer(grpc_call *call) {
  return &call->incoming_metadata;
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

grpc_call_stack *grpc_call_get_call_stack(grpc_call *call) {
  return CALL_STACK_FROM_CALL(call);
}

