// Copyright 2026 gRPC authors.
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

#include <grpc/grpc.h>

#include "src/core/server/server.h"
#include "src/core/util/grpc_check.h"
#include "test/core/bad_client/bad_client.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"

// Client preface, empty SETTINGS frame, then a WINDOW_UPDATE with the maximum
// increment for the connection: the connection send window would become
// 65535 + 0x7fffffff, exceeding the RFC 9113 maximum of 2^31-1.
static const char kPayload[] =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    "\x00\x00\x00\x04\x00\x00\x00\x00\x00"  // empty SETTINGS
    "\x00\x00\x04\x08\x00\x00\x00\x00\x00"  // WINDOW_UPDATE header, stream 0
    "\x7f\xff\xff\xff";                     // increment 0x7fffffff

// The server must close the connection itself on the flow-control violation
// (the parse fails with FLOW_CONTROL_ERROR). The client stays connected, so if
// the violation is silently accepted the connection stays open and this
// verifier fails.
static void verifier(grpc_server* server, grpc_completion_queue* cq,
                     void* /*registered_method*/) {
  const gpr_timespec deadline = gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(2, GPR_TIMESPAN));
  while (grpc_core::Server::FromC(server)->HasOpenConnections()) {
    GRPC_CHECK(gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) < 0)
        << "server did not close the connection on a WINDOW_UPDATE that "
           "overflows the flow-control window";
    GRPC_CHECK(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

TEST(WindowUpdateOverflowTest, TransportWindowOverflow) {
  grpc_bad_client_arg arg = {nullptr, nullptr, kPayload, sizeof(kPayload) - 1};
  grpc_run_bad_client_test(verifier, &arg, 1, 0);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
