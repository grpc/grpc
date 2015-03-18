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

#include "src/core/channel/connected_channel.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "src/core/support/string.h"
#include "src/core/transport/transport.h"
#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>

#define MAX_BUFFER_LENGTH 8192
/* the protobuf library will (by default) start warning at 100megs */
#define DEFAULT_MAX_MESSAGE_LENGTH (100 * 1024 * 1024)

typedef struct connected_channel_channel_data {
  grpc_transport *transport;
  gpr_uint32 max_message_length;
} channel_data;

typedef struct connected_channel_call_data {
  grpc_call_element *elem;
  grpc_stream_op_buffer outgoing_sopb;

  gpr_uint32 max_message_length;
  gpr_uint32 incoming_message_length;
  gpr_uint8 reading_message;
  gpr_uint8 got_metadata_boundary;
  gpr_uint8 got_read_close;
  gpr_slice_buffer incoming_message;
  gpr_uint32 outgoing_buffer_length_estimate;
} call_data;

/* We perform a small hack to locate transport data alongside the connected
   channel data in call allocations, to allow everything to be pulled in minimal
   cache line requests */
#define TRANSPORT_STREAM_FROM_CALL_DATA(calld) ((grpc_stream *)((calld) + 1))
#define CALL_DATA_FROM_TRANSPORT_STREAM(transport_stream) \
  (((call_data *)(transport_stream)) - 1)

/* Copy the contents of a byte buffer into stream ops */
static void copy_byte_buffer_to_stream_ops(grpc_byte_buffer *byte_buffer,
                                           grpc_stream_op_buffer *sopb) {
  size_t i;

  switch (byte_buffer->type) {
    case GRPC_BB_SLICE_BUFFER:
      for (i = 0; i < byte_buffer->data.slice_buffer.count; i++) {
        gpr_slice slice = byte_buffer->data.slice_buffer.slices[i];
        gpr_slice_ref(slice);
        grpc_sopb_add_slice(sopb, slice);
      }
      break;
  }
}

/* Flush queued stream operations onto the transport */
static void end_bufferable_op(grpc_call_op *op, channel_data *chand,
                              call_data *calld, int is_last) {
  size_t nops;

  if (op->flags & GRPC_WRITE_BUFFER_HINT) {
    if (calld->outgoing_buffer_length_estimate < MAX_BUFFER_LENGTH) {
      op->done_cb(op->user_data, GRPC_OP_OK);
      return;
    }
  }

  calld->outgoing_buffer_length_estimate = 0;
  grpc_sopb_add_flow_ctl_cb(&calld->outgoing_sopb, op->done_cb, op->user_data);

  nops = calld->outgoing_sopb.nops;
  calld->outgoing_sopb.nops = 0;
  grpc_transport_send_batch(chand->transport,
                            TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                            calld->outgoing_sopb.ops, nops, is_last);
}

/* Intercept a call operation and either push it directly up or translate it
   into transport stream operations */
static void call_op(grpc_call_element *elem, grpc_call_element *from_elem,
                    grpc_call_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  switch (op->type) {
    case GRPC_SEND_METADATA:
      grpc_sopb_add_metadata(&calld->outgoing_sopb, op->data.metadata);
      grpc_sopb_add_flow_ctl_cb(&calld->outgoing_sopb, op->done_cb,
                                op->user_data);
      break;
    case GRPC_SEND_DEADLINE:
      grpc_sopb_add_deadline(&calld->outgoing_sopb, op->data.deadline);
      grpc_sopb_add_flow_ctl_cb(&calld->outgoing_sopb, op->done_cb,
                                op->user_data);
      break;
    case GRPC_SEND_START:
      grpc_transport_add_to_pollset(chand->transport, op->data.start.pollset);
      grpc_sopb_add_metadata_boundary(&calld->outgoing_sopb);
      end_bufferable_op(op, chand, calld, 0);
      break;
    case GRPC_SEND_MESSAGE:
      grpc_sopb_add_begin_message(&calld->outgoing_sopb,
                                  grpc_byte_buffer_length(op->data.message),
                                  op->flags);
      /* fall-through */
    case GRPC_SEND_PREFORMATTED_MESSAGE:
      copy_byte_buffer_to_stream_ops(op->data.message, &calld->outgoing_sopb);
      calld->outgoing_buffer_length_estimate +=
          (5 + grpc_byte_buffer_length(op->data.message));
      end_bufferable_op(op, chand, calld, 0);
      break;
    case GRPC_SEND_FINISH:
      end_bufferable_op(op, chand, calld, 1);
      break;
    case GRPC_REQUEST_DATA:
      /* re-arm window updates if they were disarmed by finish_message */
      grpc_transport_set_allow_window_updates(
          chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld), 1);
      break;
    case GRPC_CANCEL_OP:
      grpc_transport_abort_stream(chand->transport,
                                  TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                  GRPC_STATUS_CANCELLED);
      break;
    default:
      GPR_ASSERT(op->dir == GRPC_CALL_UP);
      grpc_call_next_op(elem, op);
      break;
  }
}

/* Currently we assume all channel operations should just be pushed up. */
static void channel_op(grpc_channel_element *elem,
                       grpc_channel_element *from_elem, grpc_channel_op *op) {
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);

  switch (op->type) {
    case GRPC_CHANNEL_GOAWAY:
      grpc_transport_goaway(chand->transport, op->data.goaway.status,
                            op->data.goaway.message);
      break;
    case GRPC_CHANNEL_DISCONNECT:
      grpc_transport_close(chand->transport);
      break;
    default:
      GPR_ASSERT(op->dir == GRPC_CALL_UP);
      grpc_channel_next_op(elem, op);
      break;
  }
}

/* Constructor for call_data */
static void init_call_elem(grpc_call_element *elem,
                           const void *server_transport_data) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  int r;

  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  calld->elem = elem;
  grpc_sopb_init(&calld->outgoing_sopb);

  calld->reading_message = 0;
  calld->got_metadata_boundary = 0;
  calld->got_read_close = 0;
  calld->outgoing_buffer_length_estimate = 0;
  calld->max_message_length = chand->max_message_length;
  gpr_slice_buffer_init(&calld->incoming_message);
  r = grpc_transport_init_stream(chand->transport,
                                 TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                 server_transport_data);
  GPR_ASSERT(r == 0);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  grpc_sopb_destroy(&calld->outgoing_sopb);
  gpr_slice_buffer_destroy(&calld->incoming_message);
  grpc_transport_destroy_stream(chand->transport,
                                TRANSPORT_STREAM_FROM_CALL_DATA(calld));
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  channel_data *cd = (channel_data *)elem->channel_data;
  size_t i;
  GPR_ASSERT(!is_first);
  GPR_ASSERT(is_last);
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  cd->transport = NULL;

  cd->max_message_length = DEFAULT_MAX_MESSAGE_LENGTH;
  if (args) {
    for (i = 0; i < args->num_args; i++) {
      if (0 == strcmp(args->args[i].key, GRPC_ARG_MAX_MESSAGE_LENGTH)) {
        if (args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s ignored: it must be an integer",
                  GRPC_ARG_MAX_MESSAGE_LENGTH);
        } else if (args->args[i].value.integer < 0) {
          gpr_log(GPR_ERROR, "%s ignored: it must be >= 0",
                  GRPC_ARG_MAX_MESSAGE_LENGTH);
        } else {
          cd->max_message_length = args->args[i].value.integer;
        }
      }
    }
  }
}

/* Destructor for channel_data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *cd = (channel_data *)elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  grpc_transport_destroy(cd->transport);
}

const grpc_channel_filter grpc_connected_channel_filter = {
    call_op,           channel_op,           sizeof(call_data),
    init_call_elem,    destroy_call_elem,    sizeof(channel_data),
    init_channel_elem, destroy_channel_elem, "connected", };

static gpr_slice alloc_recv_buffer(void *user_data, grpc_transport *transport,
                                   grpc_stream *stream, size_t size_hint) {
  return gpr_slice_malloc(size_hint);
}

/* Transport callback to accept a new stream... calls up to handle it */
static void accept_stream(void *user_data, grpc_transport *transport,
                          const void *transport_server_data) {
  grpc_channel_element *elem = user_data;
  channel_data *chand = elem->channel_data;
  grpc_channel_op op;

  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  GPR_ASSERT(chand->transport == transport);

  op.type = GRPC_ACCEPT_CALL;
  op.dir = GRPC_CALL_UP;
  op.data.accept_call.transport = transport;
  op.data.accept_call.transport_server_data = transport_server_data;
  channel_op(elem, NULL, &op);
}

static void recv_error(channel_data *chand, call_data *calld, int line,
                       const char *message) {
  gpr_log_message(__FILE__, line, GPR_LOG_SEVERITY_ERROR, message);

  if (chand->transport) {
    grpc_transport_abort_stream(chand->transport,
                                TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                GRPC_STATUS_INVALID_ARGUMENT);
  }
}

static void do_nothing(void *calldata, grpc_op_error error) {}

static void finish_message(channel_data *chand, call_data *calld) {
  grpc_call_element *elem = calld->elem;
  grpc_call_op call_op;
  call_op.dir = GRPC_CALL_UP;
  call_op.flags = 0;
  /* if we got all the bytes for this message, call up the stack */
  call_op.type = GRPC_RECV_MESSAGE;
  call_op.done_cb = do_nothing;
  /* TODO(ctiller): this could be a lot faster if coded directly */
  call_op.data.message = grpc_byte_buffer_create(
      calld->incoming_message.slices, calld->incoming_message.count);
  gpr_slice_buffer_reset_and_unref(&calld->incoming_message);

  /* disable window updates until we get a request more from above */
  grpc_transport_set_allow_window_updates(
      chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld), 0);

  GPR_ASSERT(calld->incoming_message.count == 0);
  calld->reading_message = 0;
  grpc_call_next_op(elem, &call_op);
}

/* Handle incoming stream ops from the transport, translating them into
   call_ops to pass up the call stack */
static void recv_batch(void *user_data, grpc_transport *transport,
                       grpc_stream *stream, grpc_stream_op *ops,
                       size_t ops_count, grpc_stream_state final_state) {
  call_data *calld = CALL_DATA_FROM_TRANSPORT_STREAM(stream);
  grpc_call_element *elem = calld->elem;
  channel_data *chand = elem->channel_data;
  grpc_stream_op *stream_op;
  grpc_call_op call_op;
  size_t i;
  gpr_uint32 length;

  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);

  for (i = 0; i < ops_count; i++) {
    stream_op = ops + i;
    switch (stream_op->type) {
      case GRPC_OP_FLOW_CTL_CB:
        gpr_log(GPR_ERROR,
                "should not receive flow control ops from transport");
        abort();
        break;
      case GRPC_NO_OP:
        break;
      case GRPC_OP_METADATA:
        call_op.type = GRPC_RECV_METADATA;
        call_op.dir = GRPC_CALL_UP;
        call_op.flags = 0;
        call_op.data.metadata = stream_op->data.metadata;
        call_op.done_cb = do_nothing;
        call_op.user_data = NULL;
        grpc_call_next_op(elem, &call_op);
        break;
      case GRPC_OP_DEADLINE:
        call_op.type = GRPC_RECV_DEADLINE;
        call_op.dir = GRPC_CALL_UP;
        call_op.flags = 0;
        call_op.data.deadline = stream_op->data.deadline;
        call_op.done_cb = do_nothing;
        call_op.user_data = NULL;
        grpc_call_next_op(elem, &call_op);
        break;
      case GRPC_OP_METADATA_BOUNDARY:
        if (!calld->got_metadata_boundary) {
          calld->got_metadata_boundary = 1;
          call_op.type = GRPC_RECV_END_OF_INITIAL_METADATA;
          call_op.dir = GRPC_CALL_UP;
          call_op.flags = 0;
          call_op.done_cb = do_nothing;
          call_op.user_data = NULL;
          grpc_call_next_op(elem, &call_op);
        }
        break;
      case GRPC_OP_BEGIN_MESSAGE:
        /* can't begin a message when we're still reading a message */
        if (calld->reading_message) {
          char *message = NULL;
          gpr_asprintf(&message,
                       "Message terminated early; read %d bytes, expected %d",
                       (int)calld->incoming_message.length,
                       (int)calld->incoming_message_length);
          recv_error(chand, calld, __LINE__, message);
          gpr_free(message);
          return;
        }
        /* stash away parameters, and prepare for incoming slices */
        length = stream_op->data.begin_message.length;
        if (length > calld->max_message_length) {
          char *message = NULL;
          gpr_asprintf(
              &message,
              "Maximum message length of %d exceeded by a message of length %d",
              calld->max_message_length, length);
          recv_error(chand, calld, __LINE__, message);
          gpr_free(message);
        } else if (length > 0) {
          calld->reading_message = 1;
          calld->incoming_message_length = length;
        } else {
          finish_message(chand, calld);
        }
        break;
      case GRPC_OP_SLICE:
        if (GPR_SLICE_LENGTH(stream_op->data.slice) == 0) {
          gpr_slice_unref(stream_op->data.slice);
          break;
        }
        /* we have to be reading a message to know what to do here */
        if (!calld->reading_message) {
          recv_error(chand, calld, __LINE__,
                     "Received payload data while not reading a message");
          return;
        }
        /* append the slice to the incoming buffer */
        gpr_slice_buffer_add(&calld->incoming_message, stream_op->data.slice);
        if (calld->incoming_message.length > calld->incoming_message_length) {
          /* if we got too many bytes, complain */
          char *message = NULL;
          gpr_asprintf(&message,
                       "Receiving message overflow; read %d bytes, expected %d",
                       (int)calld->incoming_message.length,
                       (int)calld->incoming_message_length);
          recv_error(chand, calld, __LINE__, message);
          gpr_free(message);
          return;
        } else if (calld->incoming_message.length ==
                   calld->incoming_message_length) {
          finish_message(chand, calld);
        }
    }
  }
  /* if the stream closed, then call up the stack to let it know */
  if (!calld->got_read_close && (final_state == GRPC_STREAM_RECV_CLOSED ||
                                 final_state == GRPC_STREAM_CLOSED)) {
    calld->got_read_close = 1;
    if (calld->reading_message) {
      char *message = NULL;
      gpr_asprintf(&message,
                   "Last message truncated; read %d bytes, expected %d",
                   (int)calld->incoming_message.length,
                   (int)calld->incoming_message_length);
      recv_error(chand, calld, __LINE__, message);
      gpr_free(message);
    }
    call_op.type = GRPC_RECV_HALF_CLOSE;
    call_op.dir = GRPC_CALL_UP;
    call_op.flags = 0;
    call_op.done_cb = do_nothing;
    call_op.user_data = NULL;
    grpc_call_next_op(elem, &call_op);
  }
  if (final_state == GRPC_STREAM_CLOSED) {
    call_op.type = GRPC_RECV_FINISH;
    call_op.dir = GRPC_CALL_UP;
    call_op.flags = 0;
    call_op.done_cb = do_nothing;
    call_op.user_data = NULL;
    grpc_call_next_op(elem, &call_op);
  }
}

static void transport_goaway(void *user_data, grpc_transport *transport,
                             grpc_status_code status, gpr_slice debug) {
  /* transport got goaway ==> call up and handle it */
  grpc_channel_element *elem = user_data;
  channel_data *chand = elem->channel_data;
  grpc_channel_op op;

  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  GPR_ASSERT(chand->transport == transport);

  op.type = GRPC_TRANSPORT_GOAWAY;
  op.dir = GRPC_CALL_UP;
  op.data.goaway.status = status;
  op.data.goaway.message = debug;
  channel_op(elem, NULL, &op);
}

static void transport_closed(void *user_data, grpc_transport *transport) {
  /* transport was closed ==> call up and handle it */
  grpc_channel_element *elem = user_data;
  channel_data *chand = elem->channel_data;
  grpc_channel_op op;

  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  GPR_ASSERT(chand->transport == transport);

  op.type = GRPC_TRANSPORT_CLOSED;
  op.dir = GRPC_CALL_UP;
  channel_op(elem, NULL, &op);
}

const grpc_transport_callbacks connected_channel_transport_callbacks = {
    alloc_recv_buffer, accept_stream,    recv_batch,
    transport_goaway,  transport_closed, };

grpc_transport_setup_result grpc_connected_channel_bind_transport(
    grpc_channel_stack *channel_stack, grpc_transport *transport) {
  /* Assumes that the connected channel filter is always the last filter
     in a channel stack */
  grpc_channel_element *elem = grpc_channel_stack_last_element(channel_stack);
  channel_data *cd = (channel_data *)elem->channel_data;
  grpc_transport_setup_result ret;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  GPR_ASSERT(cd->transport == NULL);
  cd->transport = transport;

  /* HACK(ctiller): increase call stack size for the channel to make space
     for channel data. We need a cleaner (but performant) way to do this,
     and I'm not sure what that is yet.
     This is only "safe" because call stacks place no additional data after
     the last call element, and the last call element MUST be the connected
     channel. */
  channel_stack->call_stack_size += grpc_transport_stream_size(transport);

  ret.user_data = elem;
  ret.callbacks = &connected_channel_transport_callbacks;
  return ret;
}
