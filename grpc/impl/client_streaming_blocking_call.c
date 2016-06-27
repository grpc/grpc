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
#include "client_streaming_blocking_call.h"
#include "tag.h"
#include "completion_queue.h"
#include "alloc.h"

grpc_client_writer *GRPC_client_streaming_blocking_call(GRPC_channel *channel,
                                                                 const GRPC_method rpc_method,
                                                                 GRPC_context *const context,
                                                                 GRPC_message *response) {

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
      grpc_op_send_metadata
    },
    .context = context,
    .user_tag = &set
  };

  grpc_client_writer *writer = GRPC_ALLOC_STRUCT(grpc_client_writer, {
    .context = context,
    .call = call,
    .finish_ops = {
      {
        grpc_op_recv_metadata,
        grpc_op_recv_object,
        grpc_op_send_close,
        grpc_op_recv_status
      },
      .context = context,
    },
    .cq = cq,
    .response = response
  });
  writer->finish_ops.user_tag = &writer->finish_ops;

  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(&set, &rpc_method, context, (GRPC_message) {}, NULL, ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops, nops, TAG(&set), NULL));
  GRPC_completion_queue_pluck_internal(cq, TAG(&set));
  return writer;
}

bool GRPC_client_streaming_blocking_write(grpc_client_writer *writer, const GRPC_message request) {
  grpc_call_op_set set = {
    {
      grpc_op_send_object
    },
    .context = writer->context,
    .user_tag = &set
  };

  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(&set, &writer->context->rpc_method, writer->context, request, NULL, ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(writer->call, ops, nops, TAG(&set), NULL));
  return GRPC_completion_queue_pluck_internal(writer->cq, TAG(&set));
}

GRPC_status GRPC_client_writer_terminate(grpc_client_writer *writer) {
  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(&writer->finish_ops, &writer->context->rpc_method, writer->context, (GRPC_message) {}, writer->response, ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(writer->call, ops, nops, TAG(&writer->finish_ops), NULL));
  GRPC_completion_queue_pluck_internal(writer->cq, TAG(&writer->finish_ops));
  GRPC_completion_queue_shutdown_and_destroy(writer->cq);
  grpc_call_destroy(writer->call);
  writer->context->call = NULL;
  grpc_context *context = writer->context;
  free(writer);
  return context->status;
}
