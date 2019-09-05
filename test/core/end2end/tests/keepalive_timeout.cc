/*
 *
 * Copyright 2017 gRPC authors.
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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/frame_ping.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/end2end/cq_verifier.h"

#ifdef GRPC_POSIX_SOCKET
#include "src/core/lib/iomgr/ev_posix.h"
#endif  // GRPC_POSIX_SOCKET

static void* tag(intptr_t t) { return (void*)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_from_now(int n) {
  return grpc_timeout_seconds_to_deadline(n);
}

static gpr_timespec five_seconds_from_now(void) {
  return n_seconds_from_now(5);
}

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_from_now(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;

  grpc_server_shutdown_and_notify(f->server, f->shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(1000),
                                         five_seconds_from_now(), nullptr)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = nullptr;
}

static void shutdown_client(grpc_end2end_test_fixture* f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = nullptr;
}

static void end_test(grpc_end2end_test_fixture* f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  grpc_completion_queue_destroy(f->shutdown_cq);
}

/* Client sends a request, server replies with a payload, then waits for the
   keepalive watchdog timeouts before returning status. */
static void test_keepalive_timeout(grpc_end2end_test_config config) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);

  grpc_arg keepalive_arg_elems[3];
  keepalive_arg_elems[0].type = GRPC_ARG_INTEGER;
  keepalive_arg_elems[0].key = const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS);
  keepalive_arg_elems[0].value.integer = 3500;
  keepalive_arg_elems[1].type = GRPC_ARG_INTEGER;
  keepalive_arg_elems[1].key = const_cast<char*>(GRPC_ARG_KEEPALIVE_TIMEOUT_MS);
  keepalive_arg_elems[1].value.integer = 0;
  keepalive_arg_elems[2].type = GRPC_ARG_INTEGER;
  keepalive_arg_elems[2].key = const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE);
  keepalive_arg_elems[2].value.integer = 0;
  grpc_channel_args keepalive_args = {GPR_ARRAY_SIZE(keepalive_arg_elems),
                                      keepalive_arg_elems};

  grpc_end2end_test_fixture f =
      begin_test(config, "keepalive_timeout", &keepalive_args, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;

  /* Disable ping ack to trigger the keepalive timeout */
  grpc_set_disable_ping_ack(true);

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                 f.server, &s, &call_details,
                                 &request_metadata_recv, f.cq, f.cq, tag(101)));
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(3),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
  cq_verify(cqv);

  char* details_str = grpc_slice_to_c_string(details);
  char* method_str = grpc_slice_to_c_string(call_details.method);
  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "keepalive watchdog timeout"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));

  gpr_free(details_str);
  gpr_free(method_str);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(response_payload);
  grpc_byte_buffer_destroy(response_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);
}

/* Verify that reads reset the keepalive ping timer. The client sends 30 pings
 * with a sleep of 10ms in between. It has a configured keepalive timer of
 * 200ms. In the success case, each ping ack should reset the keepalive timer so
 * that the keepalive ping is never sent. */
static void test_read_delays_keepalive(grpc_end2end_test_config config) {
#ifdef GRPC_POSIX_SOCKET
  grpc_core::UniquePtr<char> poller = GPR_GLOBAL_CONFIG_GET(grpc_poll_strategy);
  /* It is hard to get the timing right for the polling engine poll. */
  if ((0 == strcmp(poller.get(), "poll"))) {
    return;
  }
#endif  // GRPC_POSIX_SOCKET
  const int kPingIntervalMS = 100;
  grpc_arg keepalive_arg_elems[3];
  keepalive_arg_elems[0].type = GRPC_ARG_INTEGER;
  keepalive_arg_elems[0].key = const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS);
  keepalive_arg_elems[0].value.integer = 20 * kPingIntervalMS;
  keepalive_arg_elems[1].type = GRPC_ARG_INTEGER;
  keepalive_arg_elems[1].key = const_cast<char*>(GRPC_ARG_KEEPALIVE_TIMEOUT_MS);
  keepalive_arg_elems[1].value.integer = 0;
  keepalive_arg_elems[2].type = GRPC_ARG_INTEGER;
  keepalive_arg_elems[2].key = const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE);
  keepalive_arg_elems[2].value.integer = 0;
  grpc_channel_args keepalive_args = {GPR_ARRAY_SIZE(keepalive_arg_elems),
                                      keepalive_arg_elems};
  grpc_end2end_test_fixture f = begin_test(config, "test_read_delays_keepalive",
                                           &keepalive_args, nullptr);
  /* Disable ping ack to trigger the keepalive timeout */
  grpc_set_disable_ping_ack(true);
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  grpc_byte_buffer* request_payload;
  grpc_byte_buffer* request_payload_recv;
  grpc_byte_buffer* response_payload;
  grpc_byte_buffer* response_payload_recv;
  int i;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello you");

  gpr_timespec deadline = five_seconds_from_now();
  c = grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(100));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(100), 1);
  cq_verify(cqv);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(101),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  for (i = 0; i < 30; i++) {
    request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
    response_payload = grpc_raw_byte_buffer_create(&response_payload_slice, 1);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(2),
                                  nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &request_payload_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  tag(102), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
    cq_verify(cqv);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = response_payload;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  tag(103), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    CQ_EXPECT_COMPLETION(cqv, tag(103), 1);
    CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
    cq_verify(cqv);

    grpc_byte_buffer_destroy(request_payload);
    grpc_byte_buffer_destroy(response_payload);
    grpc_byte_buffer_destroy(request_payload_recv);
    grpc_byte_buffer_destroy(response_payload_recv);
    /* Sleep for a short interval to check if the client sends any pings */
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(kPingIntervalMS));
  }

  grpc_slice_unref(request_payload_slice);
  grpc_slice_unref(response_payload_slice);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(3),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(104),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(104), 1);
  cq_verify(cqv);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_slice_unref(details);

  end_test(&f);
  config.tear_down_data(&f);
}

void keepalive_timeout(grpc_end2end_test_config config) {
  test_keepalive_timeout(config);
  test_read_delays_keepalive(config);
}

void keepalive_timeout_pre_init(void) {}
