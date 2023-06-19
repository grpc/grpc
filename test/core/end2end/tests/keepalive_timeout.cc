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

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Client sends a request, then waits for the keepalive watchdog timeouts before
// returning status.
CORE_END2END_TEST(Http2SingleHopTest, KeepaliveTimeout) {
  // Disable ping ack to trigger the keepalive timeout
  InitServer(ChannelArgs().Set("grpc.http2.ack_pings", false));
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_KEEPALIVE_TIME_MS, 10)
                 .Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 0)
                 .Set(GRPC_ARG_HTTP2_BDP_PROBE, false));
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(server_status.message(), "keepalive watchdog timeout");
}

// Verify that reads reset the keepalive ping timer. The client sends 30 pings
// with a sleep of 10ms in between. It has a configured keepalive timer of
// 200ms. In the success case, each ping ack should reset the keepalive timer so
// that the keepalive ping is never sent.
CORE_END2END_TEST(Http2SingleHopTest, ReadDelaysKeepalive) {
#ifdef GRPC_POSIX_SOCKET
  // It is hard to get the timing right for the polling engine poll.
  if (ConfigVars::Get().PollStrategy() == "poll") {
    GTEST_SKIP() << "Skipping test under poll poller";
  }
#endif  // GRPC_POSIX_SOCKET
  const auto kPingInterval = Duration::Milliseconds(100);
  // Disable ping ack to trigger the keepalive timeout
  InitServer(ChannelArgs().Set("grpc.http2.ack_pings", false));
  InitClient(
      ChannelArgs()
          .Set(GRPC_ARG_KEEPALIVE_TIME_MS, (20 * kPingInterval).MillisRoundUp())
          .Set(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 0)
          .Set(GRPC_ARG_HTTP2_BDP_PROBE, false));
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(60)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(100);
  Expect(100, true);
  Step();
  IncomingCloseOnServer client_close;
  s.NewBatch(101).SendInitialMetadata({}).RecvCloseOnServer(client_close);
  for (int i = 0; i < 30; i++) {
    IncomingMessage server_message;
    IncomingMessage client_message;
    c.NewBatch(2).SendMessage("hello world").RecvMessage(server_message);
    s.NewBatch(102).RecvMessage(client_message);
    Expect(102, true);
    Step();
    s.NewBatch(103).SendMessage("hello you");
    Expect(103, true);
    Expect(2, true);
    Step();
    // Sleep for a short interval to check if the client sends any pings
    Step(kPingInterval);
  }
  c.NewBatch(3).SendCloseFromClient();
  s.NewBatch(104).SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {});
  Expect(1, true);
  Expect(3, true);
  Expect(101, true);
  Expect(104, true);
  Step();
}

}  // namespace
}  // namespace grpc_core
