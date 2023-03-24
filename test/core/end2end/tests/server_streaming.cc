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

#include <string.h>

#include <functional>
#include <memory>
#include <string>

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
    int num_messages) {
  gpr_log(GPR_INFO, "%s\nRunning test: %s/%s/%d", std::string(100, '*').c_str(),
          test_name, config.name, num_messages);
  auto f = config.create_fixture(grpc_core::ChannelArgs::FromC(client_args),
                                 grpc_core::ChannelArgs::FromC(server_args));
  f->InitServer(grpc_core::ChannelArgs::FromC(server_args));
  f->InitClient(grpc_core::ChannelArgs::FromC(client_args));
  return f;
}

// Client requests status along with the initial metadata. Server streams
// messages and ends with a non-OK status. Client reads after server is done
// writing, and expects to get the status after the messages.
static void test_server_streaming(const CoreTestConfiguration& config,
                                  int num_messages) {
  auto f = begin_test(config, "test_server_streaming", nullptr, nullptr,
                      num_messages);
  grpc_call* c;
  grpc_call* s;
  auto cqv = std::make_unique<grpc_core::CqVerifier>(f->cq());
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
  grpc_byte_buffer* request_payload_recv;
  grpc_byte_buffer* response_payload;
  grpc_slice response_payload_slice =
      grpc_slice_from_copied_string("hello world");

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
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
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Client requests status early but should not receive status till all the
  // messages are received.
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  // Client sends close early
  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(3), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv->Expect(grpc_core::CqVerifier::tag(3), true);
  cqv->Verify();

  error = grpc_server_request_call(f->server(), &s, &call_details,
                                   &request_metadata_recv, f->cq(), f->cq(),
                                   grpc_core::CqVerifier::tag(100));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv->Expect(grpc_core::CqVerifier::tag(100), true);
  cqv->Verify();

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(101), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  cqv->Expect(grpc_core::CqVerifier::tag(101), true);
  cqv->Verify();

  // Server writes bunch of messages
  for (int i = 0; i < num_messages; i++) {
    response_payload = grpc_raw_byte_buffer_create(&response_payload_slice, 1);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = response_payload;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  grpc_core::CqVerifier::tag(103), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    cqv->Expect(grpc_core::CqVerifier::tag(103), true);
    cqv->Verify();

    grpc_byte_buffer_destroy(response_payload);
  }

  // Server sends status
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
  op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
  op->data.recv_close_on_server.cancelled = &was_cancelled;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(104), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);
  bool seen_status = false;
  cqv->Expect(grpc_core::CqVerifier::tag(1),
              grpc_core::CqVerifier::Maybe{&seen_status});
  cqv->Expect(grpc_core::CqVerifier::tag(104), true);
  cqv->Verify();

  // Client keeps reading messages till it gets the status
  int num_messages_received = 0;
  while (true) {
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &request_payload_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                  grpc_core::CqVerifier::tag(102), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    cqv->Expect(grpc_core::CqVerifier::tag(1),
                grpc_core::CqVerifier::Maybe{&seen_status});
    cqv->Expect(grpc_core::CqVerifier::tag(102), true);
    cqv->Verify();
    if (request_payload_recv == nullptr) {
      // The transport has received the trailing metadata.
      break;
    }
    GPR_ASSERT(byte_buffer_eq_string(request_payload_recv, "hello world"));
    grpc_byte_buffer_destroy(request_payload_recv);
    num_messages_received++;
  }
  GPR_ASSERT(num_messages_received == num_messages);
  if (!seen_status) {
    cqv->Expect(grpc_core::CqVerifier::tag(1), true);
    cqv->Verify();
  }
  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));

  grpc_slice_unref(response_payload_slice);

  grpc_call_unref(c);
  grpc_call_unref(s);

  cqv.reset();
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_slice_unref(details);
}

void server_streaming(const CoreTestConfiguration& config) {
  test_server_streaming(config, 0);
  test_server_streaming(config, 1);
  test_server_streaming(config, 10);
}

void server_streaming_pre_init(void) {}
