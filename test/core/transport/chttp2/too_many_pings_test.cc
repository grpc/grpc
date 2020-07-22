/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <gmock/gmock.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <set>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <grpcpp/impl/codegen/service_type.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/alts/alts_security_connector.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include "test/core/util/memory_counters.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/end2end/cq_verifier.h"

namespace {

void* tag(int i) { return (void*)static_cast<intptr_t>(i); }

// Perform a simple RPC where the server cancels the request with
// grpc_call_cancel_with_status
grpc_status_code PerformCall(grpc_channel* channel, grpc_server* server,
                             grpc_completion_queue* cq) {
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_call_details call_details;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  // Start a call
  c = grpc_channel_create_call(channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), nullptr,
                               deadline, nullptr);
  GPR_ASSERT(c);
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
  // Request a call on the server
  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);
  grpc_call_cancel_with_status(s, GRPC_STATUS_PERMISSION_DENIED, "test status",
                               nullptr);
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);
  // cleanup
  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_unref(c);
  grpc_call_unref(s);
  cq_verifier_destroy(cqv);
  return status;
}

// Test that sending a lot of RPCs that are cancelled by the server doesn't
// result in too many pings due to the pings sent by BDP.
TEST(TooManyPings, TestLotsOfServerCancelledRpcsDoesntGiveTooManyPings) {
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  // create the server
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  std::string server_address =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());
  grpc_server_register_completion_queue(server, cq, nullptr);
  GPR_ASSERT(
      grpc_server_add_insecure_http2_port(server, server_address.c_str()));
  grpc_server_start(server);
  // create the channel (bdp pings are enabled by default)
  grpc_channel* channel = grpc_insecure_channel_create(
      server_address.c_str(), nullptr /* channel args */, nullptr);
  std::map<grpc_status_code, int> statuses_and_counts;
  const int kNumTotalRpcs = 1e5;
  // perform an RPC
  gpr_log(GPR_INFO,
          "Performing %d total RPCs and expecting them all to receive status "
          "PERMISSION_DENIED (%d)",
          kNumTotalRpcs, GRPC_STATUS_PERMISSION_DENIED);
  for (int i = 0; i < kNumTotalRpcs; i++) {
    grpc_status_code status = PerformCall(channel, server, cq);
    statuses_and_counts[status] += 1;
  }
  int num_not_cancelled = 0;
  for (auto itr = statuses_and_counts.begin(); itr != statuses_and_counts.end();
       itr++) {
    if (itr->first != GRPC_STATUS_PERMISSION_DENIED) {
      num_not_cancelled += itr->second;
    }
    gpr_log(GPR_INFO, "%d / %d RPCs received status code: %d", itr->second,
            kNumTotalRpcs, itr->first);
  }
  if (num_not_cancelled > 0) {
    gpr_log(GPR_ERROR,
            "Expected all RPCs to receive status PERMISSION_DENIED (%d) but %d "
            "received other status codes",
            GRPC_STATUS_PERMISSION_DENIED, num_not_cancelled);
    FAIL();
  }
  // shutdown and destroy the client and server
  grpc_channel_destroy(channel);
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN)
    ;
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
