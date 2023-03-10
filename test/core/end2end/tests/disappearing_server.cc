//
//
// Copyright 2015 gRPC authors.
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

static void do_request_and_shutdown_server(
    const CoreTestConfiguration& /*config*/, CoreTestFixture* f,
    grpc_core::CqVerifier& cqv) {
  grpc_call* c;
  grpc_call* s;
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
  cqv.Expect(grpc_core::CqVerifier::tag(101), true);
  cqv.Verify();

  // should be able to shut down the server early
  // - and still complete the request
  grpc_server_shutdown_and_notify(f->server(), f->cq(),
                                  grpc_core::CqVerifier::tag(1000));

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

  cqv.Expect(grpc_core::CqVerifier::tag(102), true);
  cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  cqv.Expect(grpc_core::CqVerifier::tag(1000), true);
  cqv.Verify();
  // Please refer https://github.com/grpc/grpc/issues/21221 for additional
  // details.
  // TODO(yashykt@) - The following line should be removeable after C-Core
  // correctly handles GOAWAY frames. Internal Reference b/135458602. If this
  // test remains flaky even after this, an alternative fix would be to send a
  // request when the server is in the shut down state.
  //
  cqv.VerifyEmpty();

  GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
  GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
  GPR_ASSERT(was_cancelled == 0);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);

  grpc_call_unref(c);
  grpc_call_unref(s);
}

static void disappearing_server_test(const CoreTestConfiguration& config) {
  auto f =
      config.create_fixture(grpc_core::ChannelArgs(), grpc_core::ChannelArgs());
  grpc_core::CqVerifier cqv(f->cq());

  gpr_log(GPR_INFO, "Running test: %s/%s", "disappearing_server_test",
          config.name);

  f->InitClient(grpc_core::ChannelArgs());
  f->InitServer(grpc_core::ChannelArgs());

  do_request_and_shutdown_server(config, f.get(), cqv);

  // now destroy and recreate the server
  f->InitServer(grpc_core::ChannelArgs());

  do_request_and_shutdown_server(config, f.get(), cqv);
}

void disappearing_server(const CoreTestConfiguration& config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION);
#ifndef GPR_WINDOWS  // b/148110727 for more details
  disappearing_server_test(config);
#endif  // GPR_WINDOWS
}

void disappearing_server_pre_init(void) {}
