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

#include "src/core/lib/channel/connected_channel.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/transport.h"

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
static void con_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                          grpc_call_element *elem,
                                          grpc_transport_stream_op *op) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  GRPC_CALL_LOG_OP(GPR_INFO, elem, op);

  grpc_transport_perform_stream_op(exec_ctx, chand->transport,
                                   TRANSPORT_STREAM_FROM_CALL_DATA(calld), op);
}

static void con_start_transport_op(grpc_exec_ctx *exec_ctx,
                                   grpc_channel_element *elem,
                                   grpc_transport_op *op) {
  channel_data *chand = elem->channel_data;
  grpc_transport_perform_op(exec_ctx, chand->transport, op);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  int r = grpc_transport_init_stream(
      exec_ctx, chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld),
      &args->call_stack->refcount, args->server_transport_data);
  return r == 0 ? GRPC_ERROR_NONE
                : GRPC_ERROR_CREATE("transport stream initialization failed");
}

static void set_pollset_or_pollset_set(grpc_exec_ctx *exec_ctx,
                                       grpc_call_element *elem,
                                       grpc_polling_entity *pollent) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_transport_set_pops(exec_ctx, chand->transport,
                          TRANSPORT_STREAM_FROM_CALL_DATA(calld), pollent);
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              void *and_free_memory) {
  call_data *calld = elem->call_data;
  channel_data *chand = elem->channel_data;
  grpc_transport_destroy_stream(exec_ctx, chand->transport,
                                TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                and_free_memory);
}

/* Constructor for channel_data */
static grpc_error *init_channel_elem(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  channel_data *cd = (channel_data *)elem->channel_data;
  GPR_ASSERT(args->is_last);
  cd->transport = NULL;
  return GRPC_ERROR_NONE;
}

/* Destructor for channel_data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *cd = (channel_data *)elem->channel_data;
  grpc_transport_destroy(exec_ctx, cd->transport);
}

static char *con_get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  channel_data *chand = elem->channel_data;
  return grpc_transport_get_peer(exec_ctx, chand->transport);
}

/* No-op. */
static void con_get_channel_info(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem,
                                 const grpc_channel_info *channel_info) {}

const grpc_channel_filter grpc_connected_filter = {
    con_start_transport_stream_op,
    con_start_transport_op,
    sizeof(call_data),
    init_call_elem,
    set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    con_get_peer,
    con_get_channel_info,
    "connected",
};

static void bind_transport(grpc_channel_stack *channel_stack,
                           grpc_channel_element *elem, void *t) {
  channel_data *cd = (channel_data *)elem->channel_data;
  GPR_ASSERT(elem->filter == &grpc_connected_filter);
  GPR_ASSERT(cd->transport == NULL);
  cd->transport = t;

  /* HACK(ctiller): increase call stack size for the channel to make space
     for channel data. We need a cleaner (but performant) way to do this,
     and I'm not sure what that is yet.
     This is only "safe" because call stacks place no additional data after
     the last call element, and the last call element MUST be the connected
     channel. */
  channel_stack->call_stack_size += grpc_transport_stream_size(t);
}

bool grpc_add_connected_filter(grpc_exec_ctx *exec_ctx,
                               grpc_channel_stack_builder *builder,
                               void *arg_must_be_null) {
  GPR_ASSERT(arg_must_be_null == NULL);
  grpc_transport *t = grpc_channel_stack_builder_get_transport(builder);
  GPR_ASSERT(t != NULL);
  return grpc_channel_stack_builder_append_filter(
      builder, &grpc_connected_filter, bind_transport, t);
}

grpc_stream *grpc_connected_channel_get_stream(grpc_call_element *elem) {
  call_data *calld = elem->call_data;
  return TRANSPORT_STREAM_FROM_CALL_DATA(calld);
}
