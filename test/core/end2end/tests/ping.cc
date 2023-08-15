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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(RetryHttp2Test, Ping) {
  const int kPingCount = 5;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0)
                 .Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1));
  InitServer(ChannelArgs()
                 .Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 0)
                 .Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1));
  PingServerFromClient(0);
  Expect(0, false);
  Step();
  // check that we're still in idle, and start connecting
  EXPECT_EQ(CheckConnectivityState(true), GRPC_CHANNEL_IDLE);
  // we'll go through some set of transitions (some might be missed), until
  // READY is reached
  while (state != GRPC_CHANNEL_READY) {
    WatchConnectivityState(state, Duration::Seconds(3), 99);
    Expect(99, true);
    Step();
    state = CheckConnectivityState(false);
    EXPECT_THAT(state,
                ::testing::AnyOf(GRPC_CHANNEL_READY, GRPC_CHANNEL_CONNECTING,
                                 GRPC_CHANNEL_TRANSIENT_FAILURE));
  }
  for (int i = 1; i <= kPingCount; i++) {
    PingServerFromClient(i);
    Expect(i, true);
    Step();
  }
  ShutdownServerAndNotify(1000);
  Expect(1000, true);
  Step();
}

}  // namespace
}  // namespace grpc_core
