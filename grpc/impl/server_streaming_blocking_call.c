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


#include <grpc/support/log.h>
#include "server_streaming_blocking_call.h"
#include "../completion_queue_public.h"
#include "alloc.h"
#include "completion_queue.h"
#include "tag.h"

GRPC_client_reader *GRPC_server_streaming_blocking_call(GRPC_channel *channel,
                                                        const GRPC_method rpc_method,
                                                        GRPC_context *const context,
                                                        const GRPC_message request) {
  grpc_completion_queue *cq = GRPC_completion_queue_create();
  grpc_call *call = grpc_channel_create_call(channel,
                                             NULL,
                                             GRPC_PROPAGATE_DEFAULTS,
                                             cq,
                                             rpc_method.name,
                                             "",
                                             context->deadline,
                                             NULL);
  context->call = call;
  context->rpc_method = rpc_method;

  grpc_call_op_set set = {
    {
      grpc_op_send_metadata,
      grpc_op_send_object,
      grpc_op_send_close
    },
    .context = context,
    .user_tag = &set
  };

  grpc_client_reader *reader = GRPC_ALLOC_STRUCT(grpc_client_reader, {
    .context = context,
    .call = call,
    .cq = cq,
  });

  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(&set, &rpc_method, context, request, NULL, ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops, nops, TAG(&set), NULL));
  GRPC_completion_queue_pluck_internal(cq, TAG(&set));
  return reader;
}

bool GRPC_server_streaming_blocking_read(GRPC_client_reader *reader, GRPC_message *response) {
  grpc_call_op_set set_meta = {
    {
      grpc_op_recv_metadata,
      grpc_op_recv_object
    },
    .context = reader->context,
    .user_tag = &set_meta
  };
  grpc_call_op_set set_no_meta = {
    {
      grpc_op_recv_metadata,
      grpc_op_recv_object
    },
    .context = reader->context,
    .user_tag = &set_no_meta
  };
  grpc_call_op_set *set = NULL;
  if (reader->context->initial_metadata_received == false) {
    set = &set_meta;
  } else {
    set = &set_no_meta;
  }

  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(set, &reader->context->rpc_method, reader->context, (GRPC_message) {}, response, ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(reader->call, ops, nops, TAG(set), NULL));
  return GRPC_completion_queue_pluck_internal(reader->cq, TAG(set)) && set->message_received;
}

GRPC_status GRPC_client_reader_terminate(GRPC_client_reader *reader) {
  grpc_call_op_set set = {
    {
      grpc_op_recv_status
    },
    .context = reader->context,
    .user_tag = &set
  };
  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(&set, &reader->context->rpc_method, reader->context, (GRPC_message) {}, NULL, ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(reader->call, ops, nops, TAG(&set), NULL));
  GRPC_completion_queue_pluck_internal(reader->cq, TAG(&set));
  GRPC_completion_queue_shutdown_and_destroy(reader->cq);
  grpc_call_destroy(reader->call);
  reader->context->call = NULL;
  grpc_context *context = reader->context;
  free(reader);
  return context->status;
}
