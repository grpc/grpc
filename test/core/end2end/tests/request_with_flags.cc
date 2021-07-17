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

#include "test/core/end2end/end2end_tests.h"

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/transport/byte_stream.h"
#include "test/core/end2end/cq_verifier.h"

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

static gpr_timespec one_second_from_now(void) { return n_seconds_from_now(1); }

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, one_second_from_now(), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         nullptr)
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

static void test_invoke_request_with_flags(
    grpc_end2end_test_config config, uint32_t* flags_for_op,
    grpc_call_error call_start_batch_expected_result) {
  grpc_call* c;
  grpc_slice request_payload_slice =
      grpc_slice_from_copied_string("hello world");
  grpc_byte_buffer* request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_end2end_test_fixture f =
      begin_test(config, "test_invoke_request_with_flags", nullptr, nullptr);
  cq_verifier* cqv = cq_verifier_create(f.cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* request_payload_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  grpc_call_error expectation;

  gpr_timespec deadline = one_second_from_now();
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
  op->flags = flags_for_op[op->op];
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = request_payload;
  op->flags = flags_for_op[op->op];
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = flags_for_op[op->op];
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = flags_for_op[op->op];
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = flags_for_op[op->op];
  op->reserved = nullptr;
  op++;
  expectation = call_start_batch_expected_result;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
  GPR_ASSERT(expectation == error);

  if (expectation == GRPC_CALL_OK) {
    CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
    cq_verify(cqv);
    grpc_slice_unref(details);
  }

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  grpc_byte_buffer_destroy(request_payload);
  grpc_byte_buffer_destroy(request_payload_recv);

  end_test(&f);
  config.tear_down_data(&f);
}

void request_with_flags(grpc_end2end_test_config config) {
  size_t i;
  uint32_t flags_for_op[GRPC_OP_RECV_CLOSE_ON_SERVER + 1];

  {
    /* check that all grpc_op_types fail when their flag value is set to an
     * invalid value */
    int indices[] = {GRPC_OP_SEND_INITIAL_METADATA, GRPC_OP_SEND_MESSAGE,
                     GRPC_OP_SEND_CLOSE_FROM_CLIENT,
                     GRPC_OP_RECV_INITIAL_METADATA,
                     GRPC_OP_RECV_STATUS_ON_CLIENT};
    for (i = 0; i < GPR_ARRAY_SIZE(indices); ++i) {
      memset(flags_for_op, 0, sizeof(flags_for_op));
      flags_for_op[indices[i]] = 0xDEADBEEF;
      test_invoke_request_with_flags(config, flags_for_op,
                                     GRPC_CALL_ERROR_INVALID_FLAGS);
    }
  }
  {
    /* check valid operation with allowed flags for GRPC_OP_SEND_BUFFER */
    uint32_t flags[] = {GRPC_WRITE_BUFFER_HINT, GRPC_WRITE_NO_COMPRESS,
                        GRPC_WRITE_INTERNAL_COMPRESS};
    for (i = 0; i < GPR_ARRAY_SIZE(flags); ++i) {
      memset(flags_for_op, 0, sizeof(flags_for_op));
      flags_for_op[GRPC_OP_SEND_MESSAGE] = flags[i];
      test_invoke_request_with_flags(config, flags_for_op, GRPC_CALL_OK);
    }
  }
}

void request_with_flags_pre_init(void) {}
