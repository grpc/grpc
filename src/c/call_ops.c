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

#include <string.h>
#include "src/c/call_ops.h"
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/support/log.h>
#include <grpc/impl/codegen/grpc_types.h>
#include "src/c/client_context.h"
#include "src/c/server_context.h"

static bool op_send_metadata_fill(grpc_op *op, GRPC_context *context,
                                  GRPC_call_op_set *set,
                                  const grpc_message message, void *response) {
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_send_metadata_finish(GRPC_context *context,
                                    GRPC_call_op_set *set, bool *status,
                                    int max_message_size) {}

const GRPC_op_manager grpc_op_send_metadata = {op_send_metadata_fill,
                                               op_send_metadata_finish};

static bool op_send_object_fill(grpc_op *op, GRPC_context *context,
                                GRPC_call_op_set *set,
                                const grpc_message message, void *response) {
  op->op = GRPC_OP_SEND_MESSAGE;

  grpc_message serialized = context->serialization_impl.serialize(message);

  gpr_slice slice =
      gpr_slice_from_copied_buffer(serialized.data, serialized.length);
  op->data.send_message = grpc_raw_byte_buffer_create(&slice, 1);
  set->send_buffer = op->data.send_message;
  GPR_ASSERT(op->data.send_message != NULL);

  GRPC_message_destroy(&serialized);

  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_send_object_finish(GRPC_context *context, GRPC_call_op_set *set,
                                  bool *status, int max_message_size) {
  if (set->send_buffer) grpc_byte_buffer_destroy(set->send_buffer);
}

const GRPC_op_manager grpc_op_send_object = {op_send_object_fill,
                                             op_send_object_finish};

static bool op_recv_metadata_fill(grpc_op *op, GRPC_context *context,
                                  GRPC_call_op_set *set,
                                  const grpc_message message, void *response) {
  if (context->initial_metadata_received) return false;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  grpc_metadata_array_init(&context->recv_metadata_array);
  op->data.recv_initial_metadata = &context->recv_metadata_array;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_recv_metadata_finish(GRPC_context *context,
                                    GRPC_call_op_set *set, bool *status,
                                    int max_message_size) {
  context->initial_metadata_received = true;
}

const GRPC_op_manager grpc_op_recv_metadata = {op_recv_metadata_fill,
                                               op_recv_metadata_finish};

static bool op_recv_object_fill(grpc_op *op, GRPC_context *context,
                                GRPC_call_op_set *set,
                                const grpc_message message, void *response) {
  set->message_received = false;
  set->received_object = response;
  op->op = GRPC_OP_RECV_MESSAGE;
  set->recv_buffer = NULL;
  op->data.recv_message = &set->recv_buffer;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_recv_object_finish(GRPC_context *context, GRPC_call_op_set *set,
                                  bool *status, int max_message_size) {
  if (set->recv_buffer) {
    GPR_ASSERT(set->message_received == false);
    // deserialize
    set->message_received = true;

    grpc_byte_buffer_reader reader;
    grpc_byte_buffer_reader_init(&reader, set->recv_buffer);
    gpr_slice slice_recv = grpc_byte_buffer_reader_readall(&reader);
    uint8_t *resp = GPR_SLICE_START_PTR(slice_recv);
    size_t len = GPR_SLICE_LENGTH(slice_recv);

    context->serialization_impl.deserialize((grpc_message){resp, len},
                                            set->received_object);

    gpr_slice_unref(slice_recv);
    grpc_byte_buffer_reader_destroy(&reader);
    grpc_byte_buffer_destroy(set->recv_buffer);
    set->recv_buffer = NULL;
  }
}

const GRPC_op_manager grpc_op_recv_object = {op_recv_object_fill,
                                             op_recv_object_finish};

static bool op_client_send_close_fill(grpc_op *op, GRPC_context *context,
                                      GRPC_call_op_set *set,
                                      const grpc_message message,
                                      void *response) {
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_client_send_close_finish(GRPC_context *context,
                                        GRPC_call_op_set *set, bool *status,
                                        int max_message_size) {}

const GRPC_op_manager grpc_op_client_send_close = {op_client_send_close_fill,
                                                   op_client_send_close_finish};

static bool op_server_recv_close_fill(grpc_op *op, GRPC_context *context,
                                      GRPC_call_op_set *set,
                                      const grpc_message message,
                                      void *response) {
  GRPC_server_context *server_context = (GRPC_server_context *)context;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &server_context->cancelled;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_server_recv_close_finish(GRPC_context *context,
                                        GRPC_call_op_set *set, bool *status,
                                        int max_message_size) {}

const GRPC_op_manager grpc_op_server_recv_close = {op_server_recv_close_fill,
                                                   op_server_recv_close_finish};

static bool op_client_recv_status_fill(grpc_op *op, GRPC_context *context,
                                       GRPC_call_op_set *set,
                                       const grpc_message message,
                                       void *response) {
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;

  GRPC_client_context *client_context = (GRPC_client_context *)context;
  grpc_metadata_array_init(&client_context->recv_trailing_metadata_array);
  client_context->status.details = NULL;
  client_context->status.details_length = 0;

  op->data.recv_status_on_client.trailing_metadata =
      &client_context->recv_trailing_metadata_array;
  op->data.recv_status_on_client.status = &client_context->status.code;
  op->data.recv_status_on_client.status_details =
      &client_context->status.details;
  op->data.recv_status_on_client.status_details_capacity =
      &client_context->status.details_length;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_client_recv_status_finish(GRPC_context *context,
                                         GRPC_call_op_set *set, bool *status,
                                         int max_message_size) {}

const GRPC_op_manager grpc_op_client_recv_status = {
    op_client_recv_status_fill, op_client_recv_status_finish};

static bool op_server_send_status_fill(grpc_op *op, GRPC_context *context,
                                       GRPC_call_op_set *set,
                                       const grpc_message message,
                                       void *response) {
  GRPC_server_context *server_context = (GRPC_server_context *)context;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count =
      server_context->send_trailing_metadata_array.count;
  op->data.send_status_from_server.trailing_metadata =
      server_context->send_trailing_metadata_array.metadata;
  op->data.send_status_from_server.status =
      server_context->server_return_status;
  op->data.send_status_from_server.status_details = NULL;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_server_send_status_finish(GRPC_context *context,
                                         GRPC_call_op_set *set, bool *status,
                                         int max_message_size) {}

const GRPC_op_manager grpc_op_server_send_status = {
    op_server_send_status_fill, op_server_send_status_finish};

static bool op_server_decode_context_payload_fill(grpc_op *op,
                                                  GRPC_context *context,
                                                  GRPC_call_op_set *set,
                                                  const grpc_message message,
                                                  void *response) {
  set->message_received = false;
  set->received_object = response;
  ((GRPC_server_context *)context)->payload = NULL;
  return false;  // don't fill hence won't trigger grpc_call_start_batch
}

static void op_server_decode_context_payload_finish(GRPC_context *context,
                                                    GRPC_call_op_set *set,
                                                    bool *status,
                                                    int max_message_size) {
  // decode payload in server context
  GRPC_server_context *server_context = (GRPC_server_context *)context;
  grpc_byte_buffer *buffer = server_context->payload;

  if (buffer == NULL) {
    *status = false;
    return;
  }

  if (!set->message_received) {
    set->message_received = true;

    grpc_byte_buffer_reader reader;
    grpc_byte_buffer_reader_init(&reader, buffer);
    gpr_slice slice_recv = grpc_byte_buffer_reader_readall(&reader);
    uint8_t *resp = GPR_SLICE_START_PTR(slice_recv);
    size_t len = GPR_SLICE_LENGTH(slice_recv);

    context->serialization_impl.deserialize((grpc_message){resp, len},
                                            set->received_object);

    gpr_slice_unref(slice_recv);
    grpc_byte_buffer_reader_destroy(&reader);
  }

  grpc_byte_buffer_destroy(buffer);
  server_context->payload = NULL;
}

const GRPC_op_manager grpc_op_server_decode_context_payload = {
    op_server_decode_context_payload_fill,
    op_server_decode_context_payload_finish};

size_t GRPC_fill_op_from_call_set(GRPC_call_op_set *set, GRPC_context *context,
                                  const grpc_message message, void *response,
                                  grpc_op *ops, size_t *nops) {
  size_t manager = 0;
  size_t filled = 0;
  while (manager < GRPC_MAX_OP_COUNT) {
    if (set->operations[manager].fill == NULL &&
        set->operations[manager].finish == NULL)
      break;  // end of call set
    if (set->operations[manager].fill == NULL) continue;
    bool result = set->operations[manager].fill(&ops[filled], context, set,
                                                message, response);
    manager++;
    if (result) filled++;
  }
  *nops = filled;
  return filled;
}

bool GRPC_finish_op_from_call_set(GRPC_call_op_set *set,
                                  GRPC_context *context) {
  size_t count = 0;
  bool allStatus = true;
  while (count < GRPC_MAX_OP_COUNT) {
    if (set->operations[count].fill == NULL &&
        set->operations[count].finish == NULL)
      break;  // end of call set
    if (set->operations[count].finish == NULL) continue;
    int max_message_size = 100;  // todo(yifeit): hook up this value
    bool status = true;
    set->operations[count].finish(context, set, &status, max_message_size);
    allStatus &= status;
    count++;
  }
  return allStatus;
}

void GRPC_start_batch_from_op_set(grpc_call *call, GRPC_call_op_set *set,
                                  GRPC_context *context,
                                  const grpc_message request, void *response) {
  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  memset(ops, 0, sizeof(ops));
  GRPC_fill_op_from_call_set(set, context, request, response, ops, &nops);
  // Server will sometimes use a GRPC_call_op_set to perform post processing,
  // in which case there will be zero filled operations but some
  // finish operations.
  if (nops > 0 && call != NULL) {
    grpc_call_error error = grpc_call_start_batch(call, ops, nops, set, NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);
  }
}
