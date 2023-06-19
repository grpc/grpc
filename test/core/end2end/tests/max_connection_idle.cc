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

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

bool SimpleRequestBody(CoreEnd2endTest& test) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata(
          {}, GRPC_INITIAL_METADATA_WAIT_FOR_READY |
                  GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  bool saw_request_at_server = false;
  bool finished_client = false;
  auto s = test.RequestCall(101);
  // Connection timeout may expire before we receive the request at the server,
  // in which case we'll complete the client call but not the incoming call
  // request from the server.
  test.Expect(101, CoreEnd2endTest::Maybe{&saw_request_at_server});
  test.Expect(1, CoreEnd2endTest::Maybe{&finished_client});
  test.Step();
  if (finished_client) {
    EXPECT_FALSE(saw_request_at_server);
    EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
    test.ShutdownServerAndNotify(1000);
    test.Expect(1000, true);
    test.Expect(101, false);
    test.Step();
    return false;
  }
  EXPECT_FALSE(finished_client);
  EXPECT_TRUE(saw_request_at_server);
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  return true;
}

CORE_END2END_TEST(RetryHttp2Test, MaxConnectionIdle) {
  const auto kMaxConnectionIdle = Duration::Seconds(2);
  const auto kMaxConnectionAge = Duration::Seconds(10);
  InitClient(
      ChannelArgs()
          .Set(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS,
               Duration::Seconds(1).millis())
          .Set(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, Duration::Seconds(1).millis())
          .Set(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, Duration::Seconds(5).millis())
          // Avoid transparent retries for this test.
          .Set(GRPC_ARG_ENABLE_RETRIES, false));
  InitServer(
      ChannelArgs()
          .Set(GRPC_ARG_MAX_CONNECTION_IDLE_MS, kMaxConnectionIdle.millis())
          .Set(GRPC_ARG_MAX_CONNECTION_AGE_MS, kMaxConnectionAge.millis()));
  // check that we're still in idle, and start connecting
  grpc_connectivity_state state = CheckConnectivityState(true);
  EXPECT_EQ(state, GRPC_CHANNEL_IDLE);
  // we'll go through some set of transitions (some might be missed), until
  // READY is reached
  while (state != GRPC_CHANNEL_READY) {
    WatchConnectivityState(state, Duration::Seconds(10), 99);
    Expect(99, true);
    Step();
    state = CheckConnectivityState(false);
    EXPECT_THAT(state,
                ::testing::AnyOf(GRPC_CHANNEL_READY, GRPC_CHANNEL_CONNECTING,
                                 GRPC_CHANNEL_TRANSIENT_FAILURE));
  }
  // Use a simple request to cancel and reset the max idle timer
  if (SimpleRequestBody(*this)) {
    // wait for the channel to reach its maximum idle time
    WatchConnectivityState(GRPC_CHANNEL_READY,
                           Duration::Seconds(3) + kMaxConnectionIdle, 99);
    Expect(99, true);
    Step();
    state = CheckConnectivityState(false);
    EXPECT_THAT(state,
                ::testing::AnyOf(GRPC_CHANNEL_IDLE, GRPC_CHANNEL_CONNECTING,
                                 GRPC_CHANNEL_TRANSIENT_FAILURE));
    ShutdownServerAndNotify(1000);
    Expect(1000, true);
    Step();
  }
}

}  // namespace
}  // namespace grpc_core
