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

#include <limits.h>
#include <string.h>

#include <functional>
#include <memory>

#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

#define MAX_CONNECTION_AGE_MS 500
#define MAX_CONNECTION_AGE_GRACE_MS 2000
#define MAX_CONNECTION_IDLE_MS 9999

#define MAX_CONNECTION_AGE_JITTER_MULTIPLIER 1.1
#define CALL_DEADLINE_S 30
// The amount of time we wait for the connection to time out, but after it the
// connection should not use up its grace period. It should be a number between
// MAX_CONNECTION_AGE_MS and MAX_CONNECTION_AGE_MS +
// MAX_CONNECTION_AGE_GRACE_MS
#define CQ_MAX_CONNECTION_AGE_WAIT_TIME_S 1
// The amount of time we wait after the connection reaches its max age, it
// should be shorter than CALL_DEADLINE_S - CQ_MAX_CONNECTION_AGE_WAIT_TIME_S
#define CQ_MAX_CONNECTION_AGE_GRACE_WAIT_TIME_S 2
// The grace period for the test to observe the channel shutdown process
#define IMMEDIATE_SHUTDOWN_GRACE_TIME_MS 3000

static void test_max_age_forcibly_close(const CoreTestConfiguration& config) {
  auto f =
      config.create_fixture(grpc_core::ChannelArgs(), grpc_core::ChannelArgs());
  auto cqv = std::make_unique<grpc_core::CqVerifier>(f->cq());
  auto server_args =
      grpc_core::ChannelArgs()
          .Set(GRPC_ARG_MAX_CONNECTION_AGE_MS, MAX_CONNECTION_AGE_MS)
          .Set(GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS,
               MAX_CONNECTION_AGE_GRACE_MS)
          .Set(GRPC_ARG_MAX_CONNECTION_IDLE_MS, MAX_CONNECTION_IDLE_MS);

  f->InitClient(grpc_core::ChannelArgs());
  f->InitServer(server_args);

  grpc_call* c;
  grpc_call* s = nullptr;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(CALL_DEADLINE_S);
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error = grpc_server_request_call(f->server(), &s, &call_details,
                                   &request_metadata_recv, f->cq(), f->cq(),
                                   grpc_core::CqVerifier::tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);

  grpc_event ev = grpc_completion_queue_next(
      f->cq(), gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == grpc_core::CqVerifier::tag(1) ||
             ev.tag == grpc_core::CqVerifier::tag(101));

  if (ev.tag == grpc_core::CqVerifier::tag(101)) {
    // Request got through to the server before connection timeout

    // Wait for the channel to reach its max age
    cqv->VerifyEmpty(
        grpc_core::Duration::Seconds(CQ_MAX_CONNECTION_AGE_WAIT_TIME_S));

    // After the channel reaches its max age, we still do nothing here. And wait
    // for it to use up its max age grace period.
    cqv->Expect(grpc_core::CqVerifier::tag(1), true);
    cqv->Verify();

    gpr_timespec expect_shutdown_time = grpc_timeout_milliseconds_to_deadline(
        static_cast<int>(MAX_CONNECTION_AGE_MS *
                         MAX_CONNECTION_AGE_JITTER_MULTIPLIER) +
        MAX_CONNECTION_AGE_GRACE_MS + IMMEDIATE_SHUTDOWN_GRACE_TIME_MS);

    gpr_timespec channel_shutdown_time = gpr_now(GPR_CLOCK_MONOTONIC);
    GPR_ASSERT(gpr_time_cmp(channel_shutdown_time, expect_shutdown_time) < 0);

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
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  grpc_core::CqVerifier::tag(102), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);
    cqv->Expect(grpc_core::CqVerifier::tag(102), true);
    cqv->Verify();

    GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
    GPR_ASSERT(was_cancelled == 1);
  } else {
    // Request failed before getting to the server
  }

  grpc_server_shutdown_and_notify(f->server(), f->cq(),
                                  grpc_core::CqVerifier::tag(0xdead));
  cqv->Expect(grpc_core::CqVerifier::tag(0xdead), true);
  if (s == nullptr) {
    cqv->Expect(grpc_core::CqVerifier::tag(101), false);
  }
  cqv->Verify();

  if (s != nullptr) {
    grpc_call_unref(s);
    grpc_metadata_array_destroy(&request_metadata_recv);
  }

  // The connection should be closed immediately after the max age grace period,
  // the in-progress RPC should fail.
  GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  cqv.reset();
}

static void test_max_age_gracefully_close(const CoreTestConfiguration& config) {
  auto f =
      config.create_fixture(grpc_core::ChannelArgs(), grpc_core::ChannelArgs());
  auto cqv = std::make_unique<grpc_core::CqVerifier>(f->cq());
  auto server_args =
      grpc_core::ChannelArgs()
          .Set(GRPC_ARG_MAX_CONNECTION_AGE_MS, MAX_CONNECTION_AGE_MS)
          .Set(GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS, INT_MAX)
          .Set(GRPC_ARG_MAX_CONNECTION_IDLE_MS, MAX_CONNECTION_IDLE_MS);

  f->InitClient(grpc_core::ChannelArgs());
  f->InitServer(server_args);

  grpc_call* c;
  grpc_call* s = nullptr;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(CALL_DEADLINE_S);
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
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops),
                                grpc_core::CqVerifier::tag(1), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == error);

  error = grpc_server_request_call(f->server(), &s, &call_details,
                                   &request_metadata_recv, f->cq(), f->cq(),
                                   grpc_core::CqVerifier::tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);

  grpc_event ev = grpc_completion_queue_next(
      f->cq(), gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == grpc_core::CqVerifier::tag(1) ||
             ev.tag == grpc_core::CqVerifier::tag(101));

  if (ev.tag == grpc_core::CqVerifier::tag(101)) {
    // Request got through to the server before connection timeout

    // Wait for the channel to reach its max age
    cqv->VerifyEmpty(
        grpc_core::Duration::Seconds(CQ_MAX_CONNECTION_AGE_WAIT_TIME_S));

    // The connection is shutting down gracefully. In-progress rpc should not be
    // closed, hence the completion queue should see nothing here.
    cqv->VerifyEmpty(
        grpc_core::Duration::Seconds(CQ_MAX_CONNECTION_AGE_GRACE_WAIT_TIME_S));

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
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  grpc_core::CqVerifier::tag(102), nullptr);
    GPR_ASSERT(GRPC_CALL_OK == error);

    cqv->Expect(grpc_core::CqVerifier::tag(102), true);
    cqv->Expect(grpc_core::CqVerifier::tag(1), true);
    cqv->Verify();

    GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
    GPR_ASSERT(was_cancelled == 0);
  } else {
    // Request failed before getting to the server
  }

  grpc_server_shutdown_and_notify(f->server(), f->cq(),
                                  grpc_core::CqVerifier::tag(0xdead));
  cqv->Expect(grpc_core::CqVerifier::tag(0xdead), true);
  if (s == nullptr) {
    cqv->Expect(grpc_core::CqVerifier::tag(101), false);
  }
  cqv->Verify();

  if (s != nullptr) {
    grpc_call_unref(s);
    grpc_metadata_array_destroy(&request_metadata_recv);
  }

  // The connection is closed gracefully with goaway, the rpc should still be
  // completed.
  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  cqv.reset();
}

void max_connection_age(const CoreTestConfiguration& config) {
  test_max_age_forcibly_close(config);
  test_max_age_gracefully_close(config);
}

void max_connection_age_pre_init(void) {}
