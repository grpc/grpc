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

typedef struct connected_channel_channel_data {
  grpc_transport *transport;
} channel_data;

typedef struct connected_channel_call_data { void *unused; } call_data;

/* We perform a small hack to locate transport data alongside the connected
   channel data in call allocations, to allow everything to be pulled in minimal
   cache line requests */
#define TRANSPORT_STREAM_FROM_CALL_DATA(calld) ((grpc_stream *)((calld) + 1))
#define CALL_DATA_FROM_TRANSPORT_STREAM(transport_stream) \
  (((call_data *)(transport_stream)) - 1)

/* Intercept a call operation and either push it directly up or translate it
   into transport stream operations */
static void con_start_transport_op(grpc_call_element *elem,
                                   grpc_transport_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  grpc_transport_perform_op(chand->transport,
                            TRANSPORT_STREAM_FROM_CALL_DATA(calld), op);
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
                           const void *server_transport_data,
                           grpc_transport_op *initial_op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  int r;

  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  r = grpc_transport_init_stream(chand->transport,
                                 TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                 server_transport_data, initial_op);
  GPR_ASSERT(r == 0);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  grpc_transport_destroy_stream(chand->transport,
                                TRANSPORT_STREAM_FROM_CALL_DATA(calld));
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_channel_element *elem,
                              const grpc_channel_args *args, grpc_mdctx *mdctx,
                              int is_first, int is_last) {
  channel_data *cd = (channel_data *)elem->channel_data;
  GPR_ASSERT(!is_first);
  GPR_ASSERT(is_last);
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  cd->transport = NULL;
}

/* Destructor for channel_data */
static void destroy_channel_elem(grpc_channel_element *elem) {
  channel_data *cd = (channel_data *)elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_channel_filter);
  grpc_transport_destroy(cd->transport);
}

const grpc_channel_filter grpc_connected_channel_filter = {
    con_start_transport_op, channel_op, sizeof(call_data), init_call_elem,
    destroy_call_elem, sizeof(channel_data), init_channel_elem,
    destroy_channel_elem, "connected",
};

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
    accept_stream, transport_goaway, transport_closed,
};

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
