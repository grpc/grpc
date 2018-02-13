/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/impl/codegen/port_platform.h>

#include <limits.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/host_port.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static void* tag(intptr_t i) { return (void*)i; }

struct test_state {
  int is_client;
  grpc_channel* chan;
  grpc_call* call;
  gpr_timespec deadline;
  grpc_completion_queue* cq;
  cq_verifier* cqv;
  grpc_op ops[6];
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_slice details;
  grpc_call* server_call;
  grpc_server* server;
  grpc_metadata_array server_initial_metadata_recv;
  grpc_call_details call_details;
};

static struct test_state g_state;

static void prepare_test(int is_client) {
  int port = grpc_pick_unused_port_or_die();
  char* server_hostport;
  grpc_op* op;
  g_state.is_client = is_client;
  grpc_metadata_array_init(&g_state.initial_metadata_recv);
  grpc_metadata_array_init(&g_state.trailing_metadata_recv);
  g_state.deadline = grpc_timeout_seconds_to_deadline(5);
  g_state.cq = grpc_completion_queue_create_for_next(nullptr);
  g_state.cqv = cq_verifier_create(g_state.cq);
  g_state.details = grpc_empty_slice();
  memset(g_state.ops, 0, sizeof(g_state.ops));

  if (is_client) {
    /* create a call, channel to a non existant server */
    g_state.chan =
        grpc_insecure_channel_create("nonexistant:54321", nullptr, nullptr);
    grpc_slice host = grpc_slice_from_static_string("nonexistant");
    g_state.call = grpc_channel_create_call(
        g_state.chan, nullptr, GRPC_PROPAGATE_DEFAULTS, g_state.cq,
        grpc_slice_from_static_string("/Foo"), &host, g_state.deadline,
        nullptr);
  } else {
    g_state.server = grpc_server_create(nullptr, nullptr);
    grpc_server_register_completion_queue(g_state.server, g_state.cq, nullptr);
    gpr_join_host_port(&server_hostport, "0.0.0.0", port);
    grpc_server_add_insecure_http2_port(g_state.server, server_hostport);
    grpc_server_start(g_state.server);
    gpr_free(server_hostport);
    gpr_join_host_port(&server_hostport, "localhost", port);
    g_state.chan =
        grpc_insecure_channel_create(server_hostport, nullptr, nullptr);
    gpr_free(server_hostport);
    grpc_slice host = grpc_slice_from_static_string("bar");
    g_state.call = grpc_channel_create_call(
        g_state.chan, nullptr, GRPC_PROPAGATE_DEFAULTS, g_state.cq,
        grpc_slice_from_static_string("/Foo"), &host, g_state.deadline,
        nullptr);
    grpc_metadata_array_init(&g_state.server_initial_metadata_recv);
    grpc_call_details_init(&g_state.call_details);
    op = g_state.ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
    op->reserved = nullptr;
    op++;
    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(g_state.call, g_state.ops,
                                                     (size_t)(op - g_state.ops),
                                                     tag(1), nullptr));
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_server_request_call(g_state.server, &g_state.server_call,
                                        &g_state.call_details,
                                        &g_state.server_initial_metadata_recv,
                                        g_state.cq, g_state.cq, tag(101)));
    CQ_EXPECT_COMPLETION(g_state.cqv, tag(101), 1);
    CQ_EXPECT_COMPLETION(g_state.cqv, tag(1), 1);
    cq_verify(g_state.cqv);
  }
}

static void cleanup_test() {
  grpc_completion_queue* shutdown_cq;
  grpc_call_unref(g_state.call);
  cq_verifier_destroy(g_state.cqv);
  grpc_channel_destroy(g_state.chan);
  grpc_slice_unref(g_state.details);
  grpc_metadata_array_destroy(&g_state.initial_metadata_recv);
  grpc_metadata_array_destroy(&g_state.trailing_metadata_recv);

  if (!g_state.is_client) {
    shutdown_cq = grpc_completion_queue_create_for_pluck(nullptr);
    grpc_call_unref(g_state.server_call);
    grpc_server_shutdown_and_notify(g_state.server, shutdown_cq, tag(1000));
    GPR_ASSERT(grpc_completion_queue_pluck(shutdown_cq, tag(1000),
                                           grpc_timeout_seconds_to_deadline(5),
                                           nullptr)
                   .type == GRPC_OP_COMPLETE);
    grpc_completion_queue_destroy(shutdown_cq);
    grpc_server_destroy(g_state.server);
    grpc_call_details_destroy(&g_state.call_details);
    grpc_metadata_array_destroy(&g_state.server_initial_metadata_recv);
  }
  grpc_completion_queue_shutdown(g_state.cq);
  while (grpc_completion_queue_next(g_state.cq,
                                    gpr_inf_future(GPR_CLOCK_REALTIME), nullptr)
             .type != GRPC_QUEUE_SHUTDOWN)
    ;
  grpc_completion_queue_destroy(g_state.cq);
}

static void test_non_null_reserved_on_start_batch() {
  gpr_log(GPR_INFO, "test_non_null_reserved_on_start_batch");

  prepare_test(1);
  GPR_ASSERT(GRPC_CALL_ERROR ==
             grpc_call_start_batch(g_state.call, nullptr, 0, nullptr, tag(1)));
  cleanup_test();
}

static void test_non_null_reserved_on_op() {
  gpr_log(GPR_INFO, "test_non_null_reserved_on_op");

  grpc_op* op;
  prepare_test(1);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = tag(2);
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_send_initial_metadata_more_than_once() {
  gpr_log(GPR_INFO, "test_send_initial_metadata_more_than_once");

  grpc_op* op;
  prepare_test(1);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(g_state.call, g_state.ops,
                                                   (size_t)(op - g_state.ops),
                                                   tag(1), nullptr));
  CQ_EXPECT_COMPLETION(g_state.cqv, tag(1), 0);
  cq_verify(g_state.cqv);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_too_many_metadata() {
  gpr_log(GPR_INFO, "test_too_many_metadata");

  grpc_op* op;
  prepare_test(1);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = static_cast<size_t>(INT_MAX) + 1;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_METADATA ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_send_null_message() {
  gpr_log(GPR_INFO, "test_send_null_message");

  grpc_op* op;
  prepare_test(1);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_MESSAGE ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_send_messages_at_the_same_time() {
  gpr_log(GPR_INFO, "test_send_messages_at_the_same_time");

  grpc_op* op;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  prepare_test(1);
  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = static_cast<grpc_byte_buffer*>(tag(2));
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  grpc_byte_buffer_destroy(request_payload);
  cleanup_test();
}

static void test_send_server_status_from_client() {
  gpr_log(GPR_INFO, "test_send_server_status_from_client");

  grpc_op* op;
  prepare_test(1);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_ON_CLIENT ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_receive_initial_metadata_twice_at_client() {
  gpr_log(GPR_INFO, "test_receive_initial_metadata_twice_at_client");

  grpc_op* op;
  prepare_test(1);
  op = g_state.ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &g_state.initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(g_state.call, g_state.ops,
                                                   (size_t)(op - g_state.ops),
                                                   tag(1), nullptr));
  CQ_EXPECT_COMPLETION(g_state.cqv, tag(1), 0);
  cq_verify(g_state.cqv);
  op = g_state.ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &g_state.initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_receive_message_with_invalid_flags() {
  gpr_log(GPR_INFO, "test_receive_message_with_invalid_flags");

  grpc_op* op;
  grpc_byte_buffer* payload = nullptr;
  prepare_test(1);
  op = g_state.ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &payload;
  op->flags = 1;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_FLAGS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_receive_two_messages_at_the_same_time() {
  gpr_log(GPR_INFO, "test_receive_two_messages_at_the_same_time");

  grpc_op* op;
  grpc_byte_buffer* payload = nullptr;
  prepare_test(1);
  op = g_state.ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_recv_close_on_server_from_client() {
  gpr_log(GPR_INFO, "test_recv_close_on_server_from_client");

  grpc_op* op;
  prepare_test(1);

  op = g_state.ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_ON_CLIENT ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_recv_status_on_client_twice() {
  gpr_log(GPR_INFO, "test_recv_status_on_client_twice");

  grpc_op* op;
  prepare_test(1);

  op = g_state.ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &g_state.trailing_metadata_recv;
  op->data.recv_status_on_client.status = &g_state.status;
  op->data.recv_status_on_client.status_details = &g_state.details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(g_state.call, g_state.ops,
                                                   (size_t)(op - g_state.ops),
                                                   tag(1), nullptr));
  CQ_EXPECT_COMPLETION(g_state.cqv, tag(1), 1);
  cq_verify(g_state.cqv);

  op = g_state.ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = nullptr;
  op->data.recv_status_on_client.status = nullptr;
  op->data.recv_status_on_client.status_details = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

static void test_send_close_from_client_on_server() {
  gpr_log(GPR_INFO, "test_send_close_from_client_on_server");

  grpc_op* op;
  prepare_test(0);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_ON_SERVER ==
             grpc_call_start_batch(g_state.server_call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(2),
                                   nullptr));
  cleanup_test();
}

static void test_recv_status_on_client_from_server() {
  gpr_log(GPR_INFO, "test_recv_status_on_client_from_server");

  grpc_op* op;
  prepare_test(0);

  op = g_state.ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &g_state.trailing_metadata_recv;
  op->data.recv_status_on_client.status = &g_state.status;
  op->data.recv_status_on_client.status_details = &g_state.details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_ON_SERVER ==
             grpc_call_start_batch(g_state.server_call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(2),
                                   nullptr));
  cleanup_test();
}

static void test_send_status_from_server_with_invalid_flags() {
  gpr_log(GPR_INFO, "test_send_status_from_server_with_invalid_flags");

  grpc_op* op;
  prepare_test(0);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 1;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_FLAGS ==
             grpc_call_start_batch(g_state.server_call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(2),
                                   nullptr));
  cleanup_test();
}

static void test_too_many_trailing_metadata() {
  gpr_log(GPR_INFO, "test_too_many_trailing_metadata");

  grpc_op* op;
  prepare_test(0);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count =
      static_cast<size_t>(INT_MAX) + 1;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_METADATA ==
             grpc_call_start_batch(g_state.server_call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(2),
                                   nullptr));
  cleanup_test();
}

static void test_send_server_status_twice() {
  gpr_log(GPR_INFO, "test_send_server_status_twice");

  grpc_op* op;
  prepare_test(0);

  op = g_state.ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.server_call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(2),
                                   nullptr));
  cleanup_test();
}

static void test_recv_close_on_server_with_invalid_flags() {
  gpr_log(GPR_INFO, "test_recv_close_on_server_with_invalid_flags");

  grpc_op* op;
  prepare_test(0);

  op = g_state.ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = nullptr;
  op->flags = 1;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_FLAGS ==
             grpc_call_start_batch(g_state.server_call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(2),
                                   nullptr));
  cleanup_test();
}

static void test_recv_close_on_server_twice() {
  gpr_log(GPR_INFO, "test_recv_close_on_server_twice");

  grpc_op* op;
  prepare_test(0);

  op = g_state.ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = nullptr;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS ==
             grpc_call_start_batch(g_state.server_call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(2),
                                   nullptr));
  cleanup_test();
}

static void test_invalid_initial_metadata_reserved_key() {
  gpr_log(GPR_INFO, "test_invalid_initial_metadata_reserved_key");

  grpc_metadata metadata;
  metadata.key = grpc_slice_from_static_string(":start_with_colon");
  metadata.value = grpc_slice_from_static_string("value");

  grpc_op* op;
  prepare_test(1);
  op = g_state.ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 1;
  op->data.send_initial_metadata.metadata = &metadata;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_ERROR_INVALID_METADATA ==
             grpc_call_start_batch(g_state.call, g_state.ops,
                                   (size_t)(op - g_state.ops), tag(1),
                                   nullptr));
  cleanup_test();
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_invalid_initial_metadata_reserved_key();
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
  test_recv_close_on_server_from_client();
  test_recv_status_on_client_twice();
  test_send_close_from_client_on_server();
  test_recv_status_on_client_from_server();
  test_send_status_from_server_with_invalid_flags();
  test_too_many_trailing_metadata();
  test_send_server_status_twice();
  test_recv_close_on_server_with_invalid_flags();
  test_recv_close_on_server_twice();
  grpc_shutdown();

  return 0;
}
