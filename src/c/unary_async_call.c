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


#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/alloc.h>
#include <grpc_c/codegen/unary_async_call.h>
#include "src/c/unary_async_call.h"
#include "src/c/alloc.h"

static void free_reader_and_call(void *arg) {
  GRPC_client_async_response_reader *reader = arg;
  gpr_free(reader);
}

GRPC_client_async_response_reader *GRPC_unary_async_call(GRPC_completion_queue *cq,
                                                         const GRPC_method rpc_method,
                                                         const GRPC_message request,
                                                         GRPC_client_context *context) {
  grpc_call *call = grpc_channel_create_call(context->channel,
                                             NULL,
                                             GRPC_PROPAGATE_DEFAULTS,
                                             cq,
                                             rpc_method.name,
                                             "",
                                             context->deadline,
                                             NULL);
  context->call = call;
  context->rpc_method = rpc_method;
  GRPC_client_async_response_reader *reader = GRPC_ALLOC_STRUCT(GRPC_client_async_response_reader, {
    .context = context,
    .call = call,
    .init_buf = {
      {
        grpc_op_send_metadata,
        grpc_op_send_object,
        grpc_op_send_close
      },
      .context = context,
      .response = NULL,
      .hide_from_user = true
    },
    .meta_buf = {
      {
        grpc_op_recv_metadata
      },
      .context = context,
      .response = NULL
    },
    .finish_buf = {
      {
        grpc_op_recv_metadata,
        grpc_op_recv_object,
        grpc_op_recv_status
      },
      .context = context,
      .response = NULL,
    }
  });

  // Different from blocking call, we need to inform completion queue to run cleanup for us
  reader->finish_buf.async_cleanup = (grpc_closure) {
    .arg = reader,
    .callback = free_reader_and_call
  };

  grpc_start_batch_from_op_set(reader->call, &reader->init_buf, reader->context, request, NULL);
  return reader;
}

void GRPC_client_async_read_metadata(GRPC_client_async_response_reader *reader, void *tag) {
  reader->meta_buf.user_tag = tag;
  grpc_start_batch_from_op_set(reader->call, &reader->meta_buf, reader->context, (GRPC_message) {0, 0}, NULL);
}

void GRPC_client_async_finish(GRPC_client_async_response_reader *reader, void *response, void *tag) {
  reader->finish_buf.user_tag = tag;
  grpc_start_batch_from_op_set(reader->call, &reader->finish_buf, reader->context, (GRPC_message) {0, 0}, response);
}
