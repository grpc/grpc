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

#include "src/c/server_streaming_blocking_call.h"
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc_c/completion_queue.h>
#include <grpc_c/grpc_c.h>
#include <grpc_c/status.h>
#include "src/c/alloc.h"
#include "src/c/completion_queue.h"

GRPC_client_reader *GRPC_server_streaming_blocking_call(
    const GRPC_method rpc_method, GRPC_client_context *const context,
    const GRPC_message request) {
  grpc_completion_queue *cq = GRPC_completion_queue_create();
  grpc_call *call = grpc_channel_create_call(
      context->channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq, rpc_method.name, "",
      context->deadline, NULL);
  context->call = call;
  context->rpc_method = rpc_method;

  GRPC_call_op_set set = {
      {grpc_op_send_metadata, grpc_op_send_object, grpc_op_client_send_close},
      .context = GRPC_client_context_to_base(context),
      .user_tag = &set};

  GRPC_client_reader *reader = GRPC_ALLOC_STRUCT(
      GRPC_client_reader, {
                              .context = context, .call = call, .cq = cq,
                          });

  GRPC_start_batch_from_op_set(reader->call, &set,
                               GRPC_client_context_to_base(reader->context),
                               request, NULL);
  GRPC_completion_queue_pluck_internal(cq, &set);
  return reader;
}

bool GRPC_server_streaming_blocking_read(GRPC_client_reader *reader,
                                         void *response) {
  GRPC_call_op_set set_meta = {
      {grpc_op_recv_metadata, grpc_op_recv_object},
      .context = GRPC_client_context_to_base(reader->context),
      .user_tag = &set_meta};
  GRPC_call_op_set set_no_meta = {
      {grpc_op_recv_object},
      .context = GRPC_client_context_to_base(reader->context),
      .user_tag = &set_no_meta};
  GRPC_call_op_set *pSet = NULL;
  if (reader->context->initial_metadata_received == false) {
    pSet = &set_meta;
  } else {
    pSet = &set_no_meta;
  }

  GRPC_start_batch_from_op_set(reader->call, pSet,
                               GRPC_client_context_to_base(reader->context),
                               (GRPC_message){0, 0}, response);
  return GRPC_completion_queue_pluck_internal(reader->cq, pSet) &&
         pSet->message_received;
}

GRPC_status GRPC_client_reader_terminate(GRPC_client_reader *reader) {
  GRPC_call_op_set set = {
      {grpc_op_client_recv_status},
      .context = GRPC_client_context_to_base(reader->context),
      .user_tag = &set};
  GRPC_start_batch_from_op_set(reader->call, &set,
                               GRPC_client_context_to_base(reader->context),
                               (GRPC_message){0, 0}, NULL);
  GRPC_completion_queue_pluck_internal(reader->cq, &set);
  GRPC_completion_queue_shutdown(reader->cq);
  GRPC_completion_queue_shutdown_wait(reader->cq);
  GRPC_completion_queue_destroy(reader->cq);
  grpc_call_destroy(reader->call);
  reader->context->call = NULL;
  GRPC_client_context *context = reader->context;
  gpr_free(reader);
  return context->status;
}
