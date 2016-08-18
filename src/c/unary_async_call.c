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

#include "src/c/unary_async_call.h"
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc_c/codegen/unary_async_call.h>
#include "src/c/alloc.h"
#include "src/c/server.h"

//
// Client
//

static void free_client_reader(void *arg) {
  GRPC_client_async_response_reader *reader = arg;
  gpr_free(reader);
}

GRPC_client_async_response_reader *GRPC_unary_async_call(
    GRPC_completion_queue *cq, const GRPC_method rpc_method,
    const GRPC_message request, GRPC_client_context *const context) {
  grpc_call *call = grpc_channel_create_call(
      context->channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq, rpc_method.name, "",
      context->deadline, NULL);
  context->call = call;
  context->rpc_method = rpc_method;
  GRPC_client_async_response_reader *reader = GRPC_ALLOC_STRUCT(
      GRPC_client_async_response_reader,
      {.context = context,
       .call = call,
       .init_buf = {{grpc_op_send_metadata, grpc_op_send_object,
                     grpc_op_client_send_close},
                    .context = GRPC_client_context_to_base(context),
                    .hide_from_user = true},
       .meta_buf = {{grpc_op_recv_metadata},
                    .context = GRPC_client_context_to_base(context)},
       .finish_buf =
           {
               {grpc_op_recv_metadata, grpc_op_recv_object,
                grpc_op_client_recv_status},
               .context = GRPC_client_context_to_base(context),
           }});

  // Different from blocking call, we need to inform completion queue to run
  // cleanup for us
  reader->finish_buf.async_cleanup =
      (GRPC_closure){.arg = reader, .callback = free_client_reader};

  GRPC_start_batch_from_op_set(reader->call, &reader->init_buf,
                               GRPC_client_context_to_base(reader->context),
                               request, NULL);
  return reader;
}

void GRPC_client_async_read_metadata(GRPC_client_async_response_reader *reader,
                                     void *tag) {
  reader->meta_buf.user_tag = tag;
  GRPC_start_batch_from_op_set(reader->call, &reader->meta_buf,
                               GRPC_client_context_to_base(reader->context),
                               (GRPC_message){0, 0}, NULL);
}

void GRPC_client_async_finish(GRPC_client_async_response_reader *reader,
                              void *response, void *tag) {
  reader->finish_buf.user_tag = tag;
  GRPC_start_batch_from_op_set(reader->call, &reader->finish_buf,
                               GRPC_client_context_to_base(reader->context),
                               (GRPC_message){0, 0}, response);
}

//
// Server
//

static void free_server_writer(void *arg) {
  GRPC_server_async_response_writer *writer = arg;
  gpr_free(writer);
}

GRPC_server_async_response_writer *GRPC_unary_async_server_request(
    GRPC_registered_service *service, size_t method_index,
    GRPC_server_context *const context, void *request,
    GRPC_incoming_notification_queue *incoming_queue,
    GRPC_completion_queue *processing_queue, void *tag) {
  GRPC_server_async_response_writer *writer = GRPC_ALLOC_STRUCT(
      GRPC_server_async_response_writer,
      {.context = context,
       .receive_set =
           {// deserialize from the payload read by core after the request comes
            // in
            .operations = {grpc_op_server_decode_context_payload},
            .context = GRPC_server_context_to_base(context),
            .user_tag = tag},
       .finish_set = {.operations = {grpc_op_send_metadata, grpc_op_send_object,
                                     grpc_op_server_recv_close,
                                     grpc_op_server_send_status},
                      .context = GRPC_server_context_to_base(context)}});

  writer->finish_set.async_cleanup =
      (GRPC_closure){.arg = writer, .callback = free_server_writer};

  GPR_ASSERT(GRPC_server_request_call(service, method_index, context,
                                      incoming_queue, processing_queue,
                                      &writer->receive_set) == GRPC_CALL_OK);
  GRPC_start_batch_from_op_set(NULL, &writer->receive_set,
                               GRPC_server_context_to_base(context),
                               (GRPC_message){0, 0}, request);
  return writer;
}

void GRPC_unary_async_server_finish(GRPC_server_async_response_writer *writer,
                                    const GRPC_message response,
                                    const grpc_status_code server_status,
                                    void *tag) {
  writer->finish_set.user_tag = tag;
  writer->context->server_return_status = server_status;
  GRPC_start_batch_from_op_set(writer->context->call, &writer->finish_set,
                               GRPC_server_context_to_base(writer->context),
                               response, NULL);
}
