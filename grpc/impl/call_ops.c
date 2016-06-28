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
#include "call_ops.h"
#include "context.h"
#include "../message_public.h"
#include "tag.h"
#include <grpc/support/log.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>

static bool op_send_metadata_fill(grpc_op *op, const grpc_method *method, grpc_context *context, grpc_call_op_set *set, const grpc_message message, grpc_message *response) {
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_send_metadata_finish(grpc_context *context, grpc_call_op_set *set, bool *status, int max_message_size) {

}

const grpc_op_manager grpc_op_send_metadata = {
  op_send_metadata_fill,
  op_send_metadata_finish
};

static bool op_send_object_fill(grpc_op *op, const grpc_method *method, grpc_context *context, grpc_call_op_set *set, const grpc_message message, grpc_message *response) {
  op->op = GRPC_OP_SEND_MESSAGE;

  grpc_message serialized;
  context->serialize(message, &serialized);

  gpr_slice slice = gpr_slice_from_copied_buffer(serialized.data, serialized.length);
  op->data.send_message = grpc_raw_byte_buffer_create(&slice, 1);
  GPR_ASSERT(op->data.send_message != NULL);

  GRPC_message_destroy(&serialized);

  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_send_object_finish(grpc_context *context, grpc_call_op_set *set, bool *status, int max_message_size) {

}

const grpc_op_manager grpc_op_send_object = {
  op_send_object_fill,
  op_send_object_finish
};

static bool op_recv_metadata_fill(grpc_op *op, const grpc_method *method, grpc_context *context, grpc_call_op_set *set, const grpc_message message, grpc_message *response) {
  if (context->initial_metadata_received) return false;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  grpc_metadata_array_init(&context->recv_metadata_array);
  op->data.recv_initial_metadata = &context->recv_metadata_array;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_recv_metadata_finish(grpc_context *context, grpc_call_op_set *set, bool *status, int max_message_size) {
  context->initial_metadata_received = true;
}

const grpc_op_manager grpc_op_recv_metadata = {
  op_recv_metadata_fill,
  op_recv_metadata_finish
};

static bool op_recv_object_fill(grpc_op *op, const grpc_method *method, grpc_context *context, grpc_call_op_set *set, const grpc_message message, grpc_message *response) {
  set->message_received = false;
  set->response = response;
  op->op = GRPC_OP_RECV_MESSAGE;
  set->recv_buffer = NULL;
  op->data.recv_message = &set->recv_buffer;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_recv_object_finish(grpc_context *context, grpc_call_op_set *set, bool *status, int max_message_size) {
  if (set->recv_buffer) {
    GPR_ASSERT(set->message_received == false);
    // deserialize
    set->message_received = true;

    grpc_byte_buffer_reader reader;
    grpc_byte_buffer_reader_init(&reader, set->recv_buffer);
    gpr_slice slice_recv = grpc_byte_buffer_reader_readall(&reader);
    uint8_t *resp = GPR_SLICE_START_PTR(slice_recv);
    size_t len = GPR_SLICE_LENGTH(slice_recv);

    context->deserialize((grpc_message) { resp, len }, set->response);

    gpr_slice_unref(slice_recv);
    grpc_byte_buffer_reader_destroy(&reader);
    grpc_byte_buffer_destroy(set->recv_buffer);
    set->recv_buffer = NULL;
  }
}

const grpc_op_manager grpc_op_recv_object = {
  op_recv_object_fill,
  op_recv_object_finish
};

static bool op_send_close_fill(grpc_op *op, const grpc_method *method, grpc_context *context, grpc_call_op_set *set, const grpc_message message, grpc_message *response) {
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_send_close_finish(grpc_context *context, grpc_call_op_set *set, bool *status, int max_message_size) {
}

const grpc_op_manager grpc_op_send_close = {
  op_send_close_fill,
  op_send_close_finish
};

static bool op_recv_status_fill(grpc_op *op, const grpc_method *method, grpc_context *context, grpc_call_op_set *set, const grpc_message message, grpc_message *response) {
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  grpc_metadata_array_init(&context->trailing_metadata_array);
  context->status.details = NULL;
  context->status.details_length = 0;

  op->data.recv_status_on_client.trailing_metadata = &context->trailing_metadata_array;
  op->data.recv_status_on_client.status = &context->status.code;
  op->data.recv_status_on_client.status_details = &context->status.details;
  op->data.recv_status_on_client.status_details_capacity = &context->status.details_length;
  op->flags = 0;
  op->reserved = NULL;
  return true;
}

static void op_recv_status_finish(grpc_context *context, grpc_call_op_set *set, bool *status, int max_message_size) {
}

const grpc_op_manager grpc_op_recv_status = {
  op_recv_status_fill,
  op_recv_status_finish
};

void grpc_fill_op_from_call_set(grpc_call_op_set *set, const grpc_method *rpc_method, grpc_context *context,
                                const grpc_message message, grpc_message *response, grpc_op ops[], size_t *nops) {
  size_t manager = 0;
  size_t filled = 0;
  while (manager < GRPC_MAX_OP_COUNT) {
    if (set->op_managers[manager].fill == NULL && set->op_managers[manager].finish == NULL) break;   // end of call set
    if (set->op_managers[manager].fill == NULL) continue;
    bool result = set->op_managers[manager].fill(&ops[filled], rpc_method, context, set, message, response);
    manager++;
    if (result)
      filled++;
  }
  *nops = filled;
}

void grpc_finish_op_from_call_set(grpc_call_op_set *set, grpc_context *context) {
  size_t count = 0;
  while (count < GRPC_MAX_OP_COUNT) {
    if (set->op_managers[count].fill == NULL && set->op_managers[count].finish == NULL) break;   // end of call set
    if (set->op_managers[count].finish == NULL) continue;
    size_t size = 100;  // todo(yifeit): hook up this value
    bool status;
    set->op_managers[count].finish(context, set, &status, size);
    count++;
  }
}

void grpc_start_batch_from_op_set(grpc_call *call, grpc_call_op_set *set, grpc_context *context,
                                             const grpc_message request, grpc_message *response) {
  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(set, &context->rpc_method, context, request, response, ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops, nops, TAG(set), NULL));
}
