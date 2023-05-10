//
//
// Copyright 2016 gRPC authors.
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

/// \file Verify that status ordering rules are obeyed.
/// \ref doc/status_ordering.md

#include <string.h>

#include <functional>
#include <memory>

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

static std::unique_ptr<CoreTestFixture> begin_test(
    const CoreTestConfiguration& config, const char* test_name,
    grpc_channel_args* client_args, grpc_channel_args* server_args,
    bool request_status_early) {
  gpr_log(GPR_INFO, "Running test: %s/%s/request_status_early=%s", test_name,
          config.name, request_status_early ? "true" : "false");
  auto f = config.create_fixture(grpc_core::ChannelArgs::FromC(client_args),
                                 grpc_core::ChannelArgs::FromC(server_args));
  f->InitServer(grpc_core::ChannelArgs::FromC(server_args));
  f->InitClient(grpc_core::ChannelArgs::FromC(client_args));
  return f;
}

// Client sends a request with payload, potentially requesting status early. The
// server reads and streams responses. The client cancels the RPC to get an
// error status. (Server sending a non-OK status is not considered an error
// status.)
static void test(const CoreTestConfiguration& config, bool request_status_early,
                 bool recv_message_separately) {
  grpc_call* c;
  grpc_call* s;
  grpc_slice response_payload1_slice = grpc_slice_from_copied_string("hello");
  grpc_byte_buffer* response_payload1 =
      grpc_raw_byte_buffer_create(&response_payload1_slice, 1);
  grpc_slice response_payload2_slice = grpc_slice_from_copied_string("world");
  grpc_byte_buffer* response_payload2 =
      grpc_raw_byte_buffer_create(&response_payload2_slice, 1);
  auto f = begin_test(config, "streaming_error_response", nullptr, nullptr,
                      request_status_early);
  grpc_core::CqVerifier cqv(f->cq());
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_byte_buffer* response_payload1_recv = nullptr;
  grpc_byte_buffer* response_payload2_recv = nullptr;
  grpc_call_details call_details;
  grpc_status_code status = GRPC_STATUS_OK;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  GPR_ASSERT(!recv_message_separately || request_status_early);
  c = grpc_channel_create_call(f->client(), nullptr, GRPC_PROPAGATE_DEFAULTS,
                               f->cq(), grpc_slice_from_static_string("/foo"),
                               nullptr, deadline, nullptr);
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
  if (!recv_message_separately) {
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload1_recv;
    op++;
  }
  if (request_status_early) {
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;
  }
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_server_request_call(f->server(), &s, &call_details,
                                      &request_metadata_recv, f->cq(), f->cq(),
                                      grpc_core::CqVerifier::tag(101)));
  cqv.Expect(grpc_core::CqVerifier::tag(101), true);
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload1;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(102), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  if (recv_message_separately) {
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload1_recv;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                  grpc_core::CqVerifier::tag(4), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
  }

  cqv.Expect(grpc_core::CqVerifier::tag(102), true);
  if (!request_status_early) {
    cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  }
  if (recv_message_separately) {
    cqv.Expect(grpc_core::CqVerifier::tag(4), true);
  }
  cqv.Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = response_payload2;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(103), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  // The success of the op depends on whether the payload is written before the
  // transport sees the end of stream. If the stream has been write closed
  // before the write completes, it would fail, otherwise it would succeed.
  // Since this behavior is dependent on the transport implementation, we allow
  // any success status with this op.
  cqv.Expect(grpc_core::CqVerifier::tag(103),
             grpc_core::CqVerifier::AnyStatus());

  if (!request_status_early) {
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload2_recv;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                  grpc_core::CqVerifier::tag(2), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    cqv.Expect(grpc_core::CqVerifier::tag(2), true);
    cqv.Verify();

    GPR_ASSERT(response_payload2_recv != nullptr);
  }

  // Cancel the call so that the client sets up an error status.
  grpc_call_cancel(c, nullptr);
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(104), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv.Expect(grpc_core::CqVerifier::tag(104), true);
  if (request_status_early) {
    cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  }
  cqv.Verify();

  if (!request_status_early) {
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                  grpc_core::CqVerifier::tag(3), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    cqv.Expect(grpc_core::CqVerifier::tag(3), true);
    cqv.Verify();

    GPR_ASSERT(response_payload1_recv != nullptr);
    GPR_ASSERT(response_payload2_recv != nullptr);
  }

  GPR_ASSERT(status == GRPC_STATUS_CANCELLED);
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);

  grpc_byte_buffer_destroy(response_payload1);
  grpc_byte_buffer_destroy(response_payload2);
  grpc_byte_buffer_destroy(response_payload1_recv);
  grpc_byte_buffer_destroy(response_payload2_recv);
}

void streaming_error_response(const CoreTestConfiguration& config) {
  test(config, false, false);
  test(config, true, false);
  test(config, true, true);
}

void streaming_error_response_pre_init(void) {}
