//
//
// Copyright 2025 gRPC authors.
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

#include <grpc/status.h>
#include <grpc/support/time.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

// Regression test for https://github.com/grpc/grpc/issues/41785
//
// When a client call arrives at the server but no RequestCall() has been
// issued to match it, the call sits in pending_filter_stack_. On server
// shutdown, ZombifyPending() kills these calls via KillZombie() →
// grpc_call_unref() → FilterStackCall::ExternalUnref(). Since the handler
// never ran (received_final_op_atm_ == 0), the call should be terminated
// with UNAVAILABLE (server went away), not CANCELLED (caller cancelled).
CORE_END2END_TEST(CoreEnd2endTests,
                  EarlyServerShutdownFinishesPendingCalls) {
  SKIP_IF_V3();
  SKIP_IF_FUZZING();

  // Send a client call but do NOT issue RequestCall() on the server.
  // The call will sit in pending_filter_stack_ waiting for a match.
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);

  // Give the call time to reach the server and land in the pending queue.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));

  // Shutdown the server. This calls ZombifyPending() which kills all
  // calls in pending_filter_stack_.
  ShutdownServerAndNotify(1000);
  Expect(1000, true);
  Expect(1, true);
  Step();

  DestroyServer();

  // The client should NOT see CANCELLED — that means "the caller cancelled"
  // per the gRPC status code spec, but here it is the server shutting down.
  // Depending on transport, the client may see UNAVAILABLE or INTERNAL, both
  // of which correctly indicate a server-side issue.
  EXPECT_NE(server_status.status(), GRPC_STATUS_CANCELLED);
}

}  // namespace
}  // namespace grpc_core
