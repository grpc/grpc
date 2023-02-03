//
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
//

#include <stdint.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

#define MAX_PING_STRIKES 2

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture* f) {
  if (!f->server) return;
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

// Send more pings than server allows to trigger server's GOAWAY.
static void test_bad_ping(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(nullptr, nullptr);
  grpc_core::CqVerifier cqv(f.cq);
  grpc_arg client_a[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA), 0),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0)};
  grpc_arg server_a[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(
              GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS),
          300000 /* 5 minutes */),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PING_STRIKES), MAX_PING_STRIKES),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0)};
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(server_a), server_a};

  config.init_client(&f, &client_args);
  config.init_server(&f, &server_args);

  grpc_call* c;
  grpc_call* s;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
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
  op->data.send_initial_metadata.metadata = nullptr;
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
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();

  // Send too many pings to the server to trigger the punishment:
  // The first ping will let server mark its last_recv time. Afterwards, each
  // ping will trigger a ping strike, and we need at least MAX_PING_STRIKES
  // strikes to trigger the punishment. So (MAX_PING_STRIKES + 2) pings are
  // needed here.
  int i;
  for (i = 1; i <= MAX_PING_STRIKES + 2; i++) {
    grpc_channel_ping(f.client, f.cq, tag(200 + i), nullptr);
    cqv.Expect(tag(200 + i), true);
    if (i == MAX_PING_STRIKES + 2) {
      cqv.Expect(tag(1), true);
    }
    cqv.Verify();
  }

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

  cqv.Expect(tag(102), true);
  cqv.Verify();

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));
  cqv.Expect(tag(0xdead), true);
  cqv.Verify();

  grpc_call_unref(s);

  // The connection should be closed immediately after the misbehaved pings,
  // the in-progress RPC should fail.
  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(was_cancelled == 1);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  end_test(&f);
  config.tear_down_data(&f);
}

// Try sending more pings than server allows, but server should be fine because
// max_pings_without_data should limit pings sent out on wire.
static void test_pings_without_data(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(nullptr, nullptr);
  grpc_core::CqVerifier cqv(f.cq);
  // Only allow MAX_PING_STRIKES pings without data (DATA/HEADERS/WINDOW_UPDATE)
  // so that the transport will throttle the excess pings.
  grpc_arg client_a[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA),
          MAX_PING_STRIKES),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0)};
  grpc_arg server_a[] = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(
              GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS),
          300000 /* 5 minutes */),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_MAX_PING_STRIKES), MAX_PING_STRIKES),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_HTTP2_BDP_PROBE), 0)};
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(server_a), server_a};

  config.init_client(&f, &client_args);
  config.init_server(&f, &server_args);

  grpc_call* c;
  grpc_call* s;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
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
  op->data.send_initial_metadata.metadata = nullptr;
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
      grpc_server_request_call(f.server, &s, &call_details,
                               &request_metadata_recv, f.cq, f.cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  cqv.Expect(tag(101), true);
  cqv.Verify();

  // Send too many pings to the server similar to the previous test case.
  // However, since we set the MAX_PINGS_WITHOUT_DATA at the client side, only
  // MAX_PING_STRIKES will actually be sent and the rpc will still succeed.
  int i;
  for (i = 1; i <= MAX_PING_STRIKES + 2; i++) {
    grpc_channel_ping(f.client, f.cq, tag(200 + i), nullptr);
    if (i <= MAX_PING_STRIKES) {
      cqv.Expect(tag(200 + i), true);
    }
    cqv.Verify();
  }

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

  cqv.Expect(tag(102), true);
  // Client call should return.
  cqv.Expect(tag(1), true);
  cqv.Verify();

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));
  cqv.Expect(tag(0xdead), true);

  // Also expect the previously blocked pings to complete with an error
  cqv.Expect(tag(200 + MAX_PING_STRIKES + 1), false);
  cqv.Expect(tag(200 + MAX_PING_STRIKES + 2), false);

  cqv.Verify();

  grpc_call_unref(s);

  // The rpc should be successful.
  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  end_test(&f);
  config.tear_down_data(&f);
}

void bad_ping(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION);
  test_bad_ping(config);
  test_pings_without_data(config);
}

void bad_ping_pre_init(void) {}
