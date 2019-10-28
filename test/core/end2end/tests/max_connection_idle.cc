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

#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/useful.h"
#include "test/core/end2end/cq_verifier.h"

#define MAX_CONNECTION_IDLE_MS 500
#define MAX_CONNECTION_AGE_MS 9999

static void* tag(intptr_t t) { return (void*)t; }

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void simple_request_body(grpc_end2end_test_config /*config*/,
                                grpc_end2end_test_fixture* f) {
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(f->cq);
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
  char* peer;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  c = grpc_channel_create_call(f->client, nullptr, GRPC_PROPAGATE_DEFAULTS,
                               f->cq, grpc_slice_from_static_string("/foo"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c);

  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "client_peer_before_call=%s", peer);
  gpr_free(peer);

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
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
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
      grpc_server_request_call(f->server, &s, &call_details,
                               &request_metadata_recv, f->cq, f->cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  peer = grpc_call_get_peer(s);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "server_peer=%s", peer);
  gpr_free(peer);
  peer = grpc_call_get_peer(c);
  GPR_ASSERT(peer != nullptr);
  gpr_log(GPR_DEBUG, "client_peer=%s", peer);
  gpr_free(peer);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  op->data.send_status_from_server.status_details = &status_details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(102),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(0 == call_details.flags);
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cq_verifier_destroy(cqv);
}

static void test_max_connection_idle(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(nullptr, nullptr);
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  cq_verifier* cqv = cq_verifier_create(f.cq);

  grpc_arg client_a[1];
  client_a[0].type = GRPC_ARG_INTEGER;
  client_a[0].key =
      const_cast<char*>("grpc.testing.fixed_reconnect_backoff_ms");
  client_a[0].value.integer = 1000;
  grpc_arg server_a[2];
  server_a[0].type = GRPC_ARG_INTEGER;
  server_a[0].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_IDLE_MS);
  server_a[0].value.integer = MAX_CONNECTION_IDLE_MS;
  server_a[1].type = GRPC_ARG_INTEGER;
  server_a[1].key = const_cast<char*>(GRPC_ARG_MAX_CONNECTION_AGE_MS);
  server_a[1].value.integer = MAX_CONNECTION_AGE_MS;
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(server_a), server_a};

  config.init_client(&f, &client_args);
  config.init_server(&f, &server_args);

  /* check that we're still in idle, and start connecting */
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 1) ==
             GRPC_CHANNEL_IDLE);
  /* we'll go through some set of transitions (some might be missed), until
     READY is reached */
  while (state != GRPC_CHANNEL_READY) {
    grpc_channel_watch_connectivity_state(
        f.client, state, grpc_timeout_seconds_to_deadline(3), f.cq, tag(99));
    CQ_EXPECT_COMPLETION(cqv, tag(99), 1);
    cq_verify(cqv);
    state = grpc_channel_check_connectivity_state(f.client, 0);
    GPR_ASSERT(state == GRPC_CHANNEL_READY ||
               state == GRPC_CHANNEL_CONNECTING ||
               state == GRPC_CHANNEL_TRANSIENT_FAILURE);
  }

  /* Use a simple request to cancel and reset the max idle timer */
  simple_request_body(config, &f);

  /* wait for the channel to reach its maximum idle time */
  grpc_channel_watch_connectivity_state(
      f.client, GRPC_CHANNEL_READY,
      grpc_timeout_milliseconds_to_deadline(MAX_CONNECTION_IDLE_MS + 3000),
      f.cq, tag(99));
  CQ_EXPECT_COMPLETION(cqv, tag(99), 1);
  cq_verify(cqv);
  state = grpc_channel_check_connectivity_state(f.client, 0);
  GPR_ASSERT(state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
             state == GRPC_CHANNEL_CONNECTING || state == GRPC_CHANNEL_IDLE);

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead), 1);
  cq_verify(cqv);

  grpc_server_destroy(f.server);
  grpc_channel_destroy(f.client);
  grpc_completion_queue_shutdown(f.cq);
  drain_cq(f.cq);
  grpc_completion_queue_destroy(f.cq);
  grpc_completion_queue_destroy(f.shutdown_cq);
  config.tear_down_data(&f);

  cq_verifier_destroy(cqv);
}

void max_connection_idle(grpc_end2end_test_config config) {
  test_max_connection_idle(config);
}

void max_connection_idle_pre_init(void) {}
