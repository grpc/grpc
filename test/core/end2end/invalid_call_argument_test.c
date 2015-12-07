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
#include <limits.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

static void *tag(gpr_intptr i) { return (void *)i; }

struct test_state {
  grpc_channel *chan;
  grpc_call *call;
  gpr_timespec deadline;
  grpc_completion_queue *cq;
  cq_verifier *cqv;
  grpc_op ops[6];
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  char *details;
  size_t details_capacity;
};

static struct test_state g_state;

static void prepare_test() {
  grpc_metadata_array_init(&g_state.initial_metadata_recv);
  grpc_metadata_array_init(&g_state.trailing_metadata_recv);
  g_state.deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(2);
  g_state.cq = grpc_completion_queue_create(NULL);
  g_state.cqv = cq_verifier_create(g_state.cq);
  g_state.details = NULL;
  g_state.details_capacity = 0;

  /* create a call, channel to a non existant server */
  g_state.chan = grpc_insecure_channel_create("nonexistant:54321", NULL, NULL);
  g_state.call = grpc_channel_create_call(g_state.chan, NULL, GRPC_PROPAGATE_DEFAULTS, g_state.cq,
                                    "/Foo", "nonexistant", g_state.deadline, NULL);
}

static void cleanup_test() {
  grpc_completion_queue_shutdown(g_state.cq);
  while (grpc_completion_queue_next(g_state.cq,
                                    gpr_inf_future(GPR_CLOCK_REALTIME), NULL)
             .type != GRPC_QUEUE_SHUTDOWN)
    ;
  grpc_completion_queue_destroy(g_state.cq);
  grpc_call_destroy(g_state.call);
  grpc_channel_destroy(g_state.chan);
  cq_verifier_destroy(g_state.cqv);

  gpr_free(g_state.details);
  grpc_metadata_array_destroy(&g_state.initial_metadata_recv);
  grpc_metadata_array_destroy(&g_state.trailing_metadata_recv);
}

static void test_non_null_reserved_on_start_batch() {
  prepare_test();
  GPR_ASSERT(GRPC_CALL_ERROR ==
             grpc_call_start_batch(g_state.call, NULL, 0, NULL, tag(1)));
  cleanup_test();
}

static void test_non_null_reserved_on_op() {
  grpc_op *op;
  prepare_test();

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = tag(2);
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1), NULL));

  cleanup_test();
}

static void test_send_initial_metadata_more_than_once() {
  grpc_op *op;
  prepare_test();

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(g_state.call, g_state.ops,
                                                   (size_t)(op - g_state.ops),
                                                   tag(1), NULL));
  cq_expect_completion(g_state.cqv, tag(1), 0);
  cq_verify(g_state.cqv);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1), NULL));

  cleanup_test();
}

static void test_too_many_metadata() {
  grpc_op *op;
  prepare_test();

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = (size_t)INT_MAX + 1;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_METADATA ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1), NULL));

  cleanup_test();
}

static void test_send_null_message() {
  grpc_op *op;
  prepare_test();

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = NULL;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_MESSAGE ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1), NULL));
  cleanup_test();
}

static void test_send_messages_at_the_same_time() {
  grpc_op *op;
  gpr_slice request_payload_slice = gpr_slice_from_copied_string("hello world");
  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  prepare_test();
  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = request_payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = tag(2);
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1), NULL));
  grpc_byte_buffer_destroy(request_payload);
  cleanup_test();
}

static void test_send_server_status_from_client() {
  grpc_op *op;
  prepare_test();

  op = g_state.ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  op->data.send_status_from_server.status_details = "xyz";
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_ON_CLIENT ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1), NULL));
  cleanup_test();
}

static void test_receive_initial_metadata_twice_at_client() {
  grpc_op *op;
  prepare_test();
  op = g_state.ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &g_state.initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(g_state.call, g_state.ops,
                                                   (size_t)(op - g_state.ops),
                                                   tag(1), NULL));
  cq_expect_completion(g_state.cqv, tag(1), 0);
  cq_verify(g_state.cqv);
  op = g_state.ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &g_state.initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS == grpc_call_start_batch(g_state.call, g_state.ops,
                                                   (size_t)(op - g_state.ops),
                                                   tag(1), NULL));
  cleanup_test();
}

static void test_receive_message_with_invalid_flags() {
  grpc_op *op;
  grpc_byte_buffer *payload = NULL;
  prepare_test();
  op = g_state.ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &payload;
  op->flags = 1;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_FLAGS == grpc_call_start_batch(g_state.call, g_state.ops,
                                                                    (size_t)(op - g_state.ops),
                                                                    tag(1), NULL));
  cleanup_test();
}

static void test_receive_two_messages_at_the_same_time() {
  grpc_op *op;
  grpc_byte_buffer *payload = NULL;
  prepare_test();
  op = g_state.ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &payload;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS == grpc_call_start_batch(g_state.call, g_state.ops,
                                                                    (size_t)(op - g_state.ops),
                                                                    tag(1), NULL));
  cleanup_test();
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_non_null_reserved_on_start_batch();
  test_non_null_reserved_on_op();
  test_send_initial_metadata_more_than_once();
  test_too_many_metadata();
  test_send_null_message();
  test_send_messages_at_the_same_time();
  test_send_server_status_from_client();
  test_receive_initial_metadata_twice_at_client();
  test_receive_message_with_invalid_flags();
  test_receive_two_messages_at_the_same_time();
  grpc_shutdown();

  return 0;
}
