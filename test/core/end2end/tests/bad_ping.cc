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

#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

#define MAX_PING_STRIKES 2

namespace grpc_core {
namespace {

// Send more pings than server allows to trigger server's GOAWAY.
CORE_END2END_TEST(RetryHttp2Test, BadPing) {
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0)
                 .Set(GRPC_ARG_HTTP2_BDP_PROBE, 0));
  InitServer(ChannelArgs()
                 .Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS,
                      Duration::Minutes(5).millis())
                 .Set(GRPC_ARG_HTTP2_MAX_PING_STRIKES, MAX_PING_STRIKES)
                 .Set(GRPC_ARG_HTTP2_BDP_PROBE, 0));
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(10)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  // Send too many pings to the server to trigger the punishment:
  // The first ping will let server mark its last_recv time. Afterwards, each
  // ping will trigger a ping strike, and we need at least MAX_PING_STRIKES
  // strikes to trigger the punishment. So (MAX_PING_STRIKES + 2) pings are
  // needed here.
  int i;
  for (i = 1; i <= MAX_PING_STRIKES + 2; i++) {
    PingServerFromClient(200 + i);
    Expect(200 + i, true);
    if (i == MAX_PING_STRIKES + 2) {
      Expect(1, true);
    }
    Step();
  }
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Step();
  ShutdownServerAndNotify(103);
  Expect(103, true);
  Step();
  // The connection should be closed immediately after the misbehaved pings,
  // the in-progress RPC should fail.
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_TRUE(client_close.was_cancelled());
}

// Try sending more pings than server allows, but server should be fine because
// max_pings_without_data should limit pings sent out on wire.
CORE_END2END_TEST(RetryHttp2Test, PingsWithoutData) {
  // Only allow MAX_PING_STRIKES pings without data (DATA/HEADERS/WINDOW_UPDATE)
  // so that the transport will throttle the excess pings.
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, MAX_PING_STRIKES)
                 .Set(GRPC_ARG_HTTP2_BDP_PROBE, 0));
  InitServer(ChannelArgs()
                 .Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS,
                      Duration::Minutes(5).millis())
                 .Set(GRPC_ARG_HTTP2_MAX_PING_STRIKES, MAX_PING_STRIKES)
                 .Set(GRPC_ARG_HTTP2_BDP_PROBE, 0));
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(10)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  // Send too many pings to the server similar to the previous test case.
  // However, since we set the MAX_PINGS_WITHOUT_DATA at the client side, only
  // MAX_PING_STRIKES will actually be sent and the rpc will still succeed.
  int i;
  for (i = 1; i <= MAX_PING_STRIKES + 2; i++) {
    PingServerFromClient(200 + i);
    if (i <= MAX_PING_STRIKES) {
      Expect(200 + i, true);
    }
    Step();
  }
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  // Client call should return.
  Expect(1, true);
  Step();
  ShutdownServerAndNotify(103);
  Expect(103, true);
  // Also expect the previously blocked pings to complete with an error
  Expect(200 + MAX_PING_STRIKES + 1, false);
  Expect(200 + MAX_PING_STRIKES + 2, false);
  Step();
  // The rpc should be successful.
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(s.method(), "/foo");
}

}  // namespace
}  // namespace grpc_core
