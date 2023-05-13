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

#include "gtest/gtest.h"

#include <grpc/status.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreEnd2endTest, EarlyServerShutdownFinishesInflightCalls) {
  SKIP_IF_FUZZING();

  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102).RecvCloseOnServer(client_close);

  // Make sure we don't shutdown the server while HTTP/2 PING frames are still
  // being exchanged on the newly established connection. It can lead to
  // failures when testing with HTTP proxy. See
  // https://github.com/grpc/grpc/issues/14471
  //
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));

  // shutdown and destroy the server
  ShutdownServerAndNotify(1000);
  CancelAllCallsOnServer();

  Expect(1000, true);
  Expect(102, true);
  Expect(1, true);
  Step();

  DestroyServer();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_TRUE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
