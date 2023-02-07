//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdint.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char* test_name,
                                            grpc_channel_args* client_args,
                                            grpc_channel_args* server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "Running test: %s/%s", test_name, config.name);
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
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(f->cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE || ev.tag != tag(1000));
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
}

// Tests transparent retries when the call was never sent out on the wire.
// This is similar to retry_transparent_not_sent_on_wire, except that
// instead of simulating the response with a filter, we actually have
// the transport behave the right way.  We create a server with
// MAX_CONCURRENT_STREAMS set to 1.  We start a call on the server, and
// then start a second call, which will get queued in the transport.
// Then, before the first call finishes, the server is shut down and
// restarted.  The second call will fail in that transport instance and
// will be transparently retried after the server starts up again.
static void test_retry_transparent_max_concurrent_streams(
    grpc_end2end_test_config config) {
  grpc_op ops[6];
  grpc_op* op;
  grpc_slice request_payload_slice = grpc_slice_from_static_string("foo");
  grpc_slice response_payload_slice = grpc_slice_from_static_string("bar");
  grpc_slice status_details = grpc_slice_from_static_string("xyz");
  grpc_call_error error;

  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MAX_CONCURRENT_STREAMS), 1);
  grpc_channel_args server_args = {1, &arg};
  grpc_end2end_test_fixture f =
      begin_test(config, "retry_transparent_max_concurrent_streams", nullptr,
                 &server_args);

  grpc_core::CqVerifier cqv(f.cq);

  gpr_timespec deadline = five_seconds_from_now();

  // Client starts a call.
  grpc_call* c =
      grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c);
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_status_code status;
  grpc_slice details;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Server should get a call.
  grpc_call* s;
  grpc_metadata_array request_metadata_recv;
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details call_details;
  grpc_call_details_init(&call_details);
  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  grpc_call_details_destroy(&call_details);
  grpc_metadata_array_destroy(&request_metadata_recv);

  // Client starts a second call.
  // We set wait_for_ready for this call, so that if it retries before
  // the server comes back up, it stays pending.
  grpc_call* c2 =
      grpc_channel_create_call(f.client, nullptr, GRPC_PROPAGATE_DEFAULTS, f.cq,
                               grpc_slice_from_static_string("/service/method"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c2);
  grpc_byte_buffer* request_payload2 =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_metadata_array initial_metadata_recv2;
  grpc_metadata_array_init(&initial_metadata_recv2);
  grpc_byte_buffer* response_payload_recv2 = nullptr;
  grpc_metadata_array trailing_metadata_recv2;
  grpc_metadata_array_init(&trailing_metadata_recv2);
  grpc_status_code status2;
  grpc_slice details2;
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload2;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv2;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv2;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv2;
  op->data.recv_status_on_client.status = &status2;
  op->data.recv_status_on_client.status_details = &details2;
  op++;
  error = grpc_call_start_batch(c2, ops, static_cast<size_t>(op - ops), tag(2),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Start server shutdown.
  grpc_server_shutdown_and_notify(f.server, f.cq, tag(102));

  // Server handles the first call.
  grpc_byte_buffer* request_payload_recv = nullptr;
  int was_cancelled = 2;
  grpc_byte_buffer* response_payload =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(104),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Server completes first call and shutdown.
  // Client completes first call.
  cqv.Expect(tag(104), true);
  cqv.Expect(tag(103), true);
  cqv.Expect(tag(102), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  // Clean up from first call.
  GPR_ASSERT(byte_buffer_eq_slice(request_payload_recv, request_payload_slice));
  grpc_byte_buffer_destroy(request_payload_recv);
  GPR_ASSERT(was_cancelled == 0);
  grpc_byte_buffer_destroy(response_payload);
  grpc_call_unref(s);
  grpc_byte_buffer_destroy(request_payload);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  GPR_ASSERT(
      byte_buffer_eq_slice(response_payload_recv, response_payload_slice));
  grpc_byte_buffer_destroy(response_payload_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  grpc_slice_unref(details);
  grpc_call_unref(c);

  // Destroy server and then restart it.
  grpc_server_destroy(f.server);
  f.server = nullptr;
  config.init_server(&f, &server_args);

  // Server should get the second call.
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);
  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(201));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(201), true);
  cqv.Verify();
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/service/method"));
  grpc_call_details_destroy(&call_details);
  // Make sure the "grpc-previous-rpc-attempts" header was NOT sent, since
  // we don't do that for transparent retries.
  for (size_t i = 0; i < request_metadata_recv.count; ++i) {
    GPR_ASSERT(!grpc_slice_eq(
        request_metadata_recv.metadata[i].key,
        grpc_slice_from_static_string("grpc-previous-rpc-attempts")));
  }
  grpc_metadata_array_destroy(&request_metadata_recv);

  // Server handles the second call.
  request_payload_recv = nullptr;
  was_cancelled = 2;
  grpc_byte_buffer* response_payload2 =
      grpc_raw_byte_buffer_create(&response_payload_slice, 1);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &request_payload_recv;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(202),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload2;
  op++;
  op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
  op->data.send_status_from_server.trailing_metadata_count = 0;
  op->data.send_status_from_server.status = GRPC_STATUS_OK;
  op->data.send_status_from_server.status_details = &status_details;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(203),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Second call completes.
  cqv.Expect(tag(203), true);
  cqv.Expect(tag(202), true);
  cqv.Expect(tag(2), true);
  cqv.Verify();

  // Clean up from second call.
  GPR_ASSERT(byte_buffer_eq_slice(request_payload_recv, request_payload_slice));
  grpc_byte_buffer_destroy(request_payload_recv);
  GPR_ASSERT(was_cancelled == 0);
  grpc_byte_buffer_destroy(response_payload2);
  grpc_call_unref(s);
  grpc_byte_buffer_destroy(request_payload2);
  grpc_metadata_array_destroy(&initial_metadata_recv2);
  GPR_ASSERT(
      byte_buffer_eq_slice(response_payload_recv2, response_payload_slice));
  grpc_byte_buffer_destroy(response_payload_recv2);
  grpc_metadata_array_destroy(&trailing_metadata_recv2);
  GPR_ASSERT(status2 == GRPC_STATUS_OK);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details2, "xyz"));
  grpc_slice_unref(details2);
  grpc_call_unref(c2);

  end_test(&f);
  config.tear_down_data(&f);
}

void retry_transparent_max_concurrent_streams(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL);
  test_retry_transparent_max_concurrent_streams(config);
}

void retry_transparent_max_concurrent_streams_pre_init(void) {}
