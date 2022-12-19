//
//
// Copyright 2020 gRPC authors.
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

// Client streaming test where the client sends a bunch of messages and the
// server reads them. After reading some messages, the server sends the status.
// Client writes fail after that due to the end of stream and the client
// subsequently requests and receives the status.
static void test_client_streaming(grpc_end2end_test_config config,
                                  int messages) {
  grpc_end2end_test_fixture f =
      begin_test(config, "test_client_streaming", nullptr, nullptr);
  grpc_call* c;
  grpc_call* s;
  grpc_core::CqVerifier cqv(f.cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_byte_buffer* request_payload = nullptr;
  int i;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");

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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error =
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(100));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(100), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops), tag(101),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(tag(101), true);
  cqv.Expect(tag(1), true);
  cqv.Verify();

  // Client writes bunch of messages and server reads them
  for (i = 0; i < messages; i++) {
    request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                  tag(103), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    grpc_byte_buffer_destroy(request_payload);

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
    cqv.Expect(tag(102), true);
    cqv.Expect(tag(103), true);
    cqv.Verify();
    GPR_ASSERT(byte_buffer_eq_string(request_payload_recv, "hello world"));
    grpc_byte_buffer_destroy(request_payload_recv);
  }

  // Server sends status denoting end of stream
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
  cqv.Expect(tag(104), true);
  cqv.Verify();
  // Do an empty verify to make sure that the client receives the status
  cqv.VerifyEmpty();

  // Client tries sending another message which should fail
  request_payload = grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(103),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_byte_buffer_destroy(request_payload);
  cqv.Expect(tag(103), false);
  cqv.Verify();

  // Client sends close and requests status
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(3),
                                nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(3), true);
  cqv.Verify();
  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));

  grpc_slice_unref(request_payload_slice);

  grpc_call_unref(c);
  grpc_call_unref(s);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_slice_unref(details);

  end_test(&f);
  config.tear_down_data(&f);
}

void client_streaming(grpc_end2end_test_config config) {
  for (int i = 0; i < 10; i++) {
    test_client_streaming(config, i);
  }
}

void client_streaming_pre_init(void) {}
