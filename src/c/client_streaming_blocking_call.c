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

#include "src/c/client_streaming_blocking_call.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc_c/grpc_c.h>
#include <grpc_c/status.h>
#include "src/c/alloc.h"
#include "src/c/completion_queue.h"

GRPC_client_writer *GRPC_client_streaming_blocking_call(
    const GRPC_method rpc_method, GRPC_client_context *const context,
    void *response) {
  grpc_completion_queue *cq = GRPC_completion_queue_create();
  grpc_call *call = grpc_channel_create_call(
      context->channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq, rpc_method.name, "",
      context->deadline, NULL);
  context->call = call;
  context->rpc_method = rpc_method;

  GRPC_call_op_set set = {{grpc_op_send_metadata},
                          .context = GRPC_client_context_to_base(context),
                          .user_tag = &set};

  GRPC_client_writer *writer = GRPC_ALLOC_STRUCT(
      GRPC_client_writer,
      {.context = context,
       .call = call,
       .finish_ops = {{grpc_op_recv_metadata, grpc_op_recv_object,
                       grpc_op_client_send_close, grpc_op_client_recv_status},
                      .context = GRPC_client_context_to_base(context)},
       .cq = cq,
       .response = response});
  writer->finish_ops.user_tag = &writer->finish_ops;

  GRPC_start_batch_from_op_set(writer->call, &set,
                               GRPC_client_context_to_base(writer->context),
                               (GRPC_message){0, 0}, NULL);
  GRPC_completion_queue_pluck_internal(cq, &set);
  return writer;
}

bool GRPC_client_streaming_blocking_write(GRPC_client_writer *writer,
                                          const GRPC_message request) {
  GRPC_call_op_set set = {
      {grpc_op_send_object},
      .context = GRPC_client_context_to_base(writer->context),
      .user_tag = &set};

  GRPC_start_batch_from_op_set(writer->call, &set,
                               GRPC_client_context_to_base(writer->context),
                               request, NULL);
  return GRPC_completion_queue_pluck_internal(writer->cq, &set);
}

GRPC_status GRPC_client_writer_terminate(GRPC_client_writer *writer) {
  GRPC_start_batch_from_op_set(writer->call, &writer->finish_ops,
                               GRPC_client_context_to_base(writer->context),
                               (GRPC_message){0, 0}, writer->response);
  GRPC_completion_queue_pluck_internal(writer->cq, &writer->finish_ops);
  GRPC_completion_queue_shutdown(writer->cq);
  GRPC_completion_queue_shutdown_wait(writer->cq);
  GRPC_completion_queue_destroy(writer->cq);
  grpc_call_destroy(writer->call);
  writer->context->call = NULL;
  GRPC_client_context *context = writer->context;
  gpr_free(writer);
  return context->status;
}
