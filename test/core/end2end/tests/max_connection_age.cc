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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <limits.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/test_config.h"

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

namespace grpc_core {
namespace {

CORE_END2END_TEST(Http2Tests, MaxAgeForciblyClose) {
  SKIP_IF_MINSTACK();
  InitClient(ChannelArgs());
  InitServer(ChannelArgs()
                 .Set(GRPC_ARG_MAX_CONNECTION_AGE_MS, MAX_CONNECTION_AGE_MS)
                 .Set(GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS,
                      MAX_CONNECTION_AGE_GRACE_MS)
                 .Set(GRPC_ARG_MAX_CONNECTION_IDLE_MS, MAX_CONNECTION_IDLE_MS));
  auto c = NewClientCall("/foo")
               .Timeout(Duration::Seconds(CALL_DEADLINE_S))
               .Create();
  const auto expect_shutdown_time =
      Timestamp::FromTimespecRoundUp(grpc_timeout_milliseconds_to_deadline(
          static_cast<int>(MAX_CONNECTION_AGE_MS *
                           MAX_CONNECTION_AGE_JITTER_MULTIPLIER) +
          MAX_CONNECTION_AGE_GRACE_MS + IMMEDIATE_SHUTDOWN_GRACE_TIME_MS));
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  bool got_client = false;
  bool got_server = false;
  Expect(1, Maybe{&got_client});
  Expect(101, Maybe{&got_server});
  Step();
  if (got_server) {
    // Request got through to the server before connection timeout
    // Wait for the channel to reach its max age
    Step(Duration::Seconds(CQ_MAX_CONNECTION_AGE_WAIT_TIME_S));
    // After the channel reaches its max age, we still do nothing here. And wait
    // for it to use up its max age grace period.
    Expect(1, true);
    Step();
    EXPECT_LT(Timestamp::Now(), expect_shutdown_time);
    IncomingCloseOnServer client_close;
    s.NewBatch(102)
        .SendInitialMetadata({})
        .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
        .RecvCloseOnServer(client_close);
    Expect(102, true);
    Step();
    EXPECT_EQ(s.method(), "/foo");
    EXPECT_TRUE(client_close.was_cancelled());
  } else {
    // Request failed before getting to the server
  }
  ShutdownServerAndNotify(1000);
  Expect(1000, true);
  if (got_client) {
    Expect(101, false);
  }
  Step();
  // The connection should be closed immediately after the max age grace period,
  // the in-progress RPC should fail.
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(server_status.message(), "max connection age");
}

CORE_END2END_TEST(Http2Tests, MaxAgeGracefullyClose) {
  SKIP_IF_MINSTACK();
  SKIP_IF_FUZZING();

  InitClient(ChannelArgs());
  InitServer(ChannelArgs()
                 .Set(GRPC_ARG_MAX_CONNECTION_AGE_MS, MAX_CONNECTION_AGE_MS)
                 .Set(GRPC_ARG_MAX_CONNECTION_AGE_GRACE_MS, INT_MAX)
                 .Set(GRPC_ARG_MAX_CONNECTION_IDLE_MS, MAX_CONNECTION_IDLE_MS));
  auto c = NewClientCall("/foo")
               .Timeout(Duration::Seconds(CALL_DEADLINE_S))
               .Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  bool got_client = false;
  bool got_server = false;
  Expect(1, Maybe{&got_client});
  Expect(101, Maybe{&got_server});
  Step();
  if (got_server) {
    // Request got through to the server before connection timeout
    // Wait for the channel to reach its max age
    Step(Duration::Seconds(CQ_MAX_CONNECTION_AGE_WAIT_TIME_S));
    // The connection is shutting down gracefully. In-progress rpc should not be
    // closed, hence the completion queue should see nothing here.
    Step(Duration::Seconds(CQ_MAX_CONNECTION_AGE_GRACE_WAIT_TIME_S));
    IncomingCloseOnServer client_close;
    s.NewBatch(102)
        .SendInitialMetadata({})
        .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
        .RecvCloseOnServer(client_close);
    Expect(102, true);
    Expect(1, true);
    Step();
    EXPECT_EQ(s.method(), "/foo");
    EXPECT_FALSE(client_close.was_cancelled());
  } else {
    // Request failed before getting to the server
  }
  ShutdownServerAndNotify(1000);
  Expect(1000, true);
  if (got_client) {
    Expect(101, false);
  }
  Step();
  if (got_server) {
    // The connection is closed gracefully with goaway, the rpc should still be
    // completed.
    EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
    EXPECT_EQ(server_status.message(), "xyz");
  }
}

}  // namespace
}  // namespace grpc_core
