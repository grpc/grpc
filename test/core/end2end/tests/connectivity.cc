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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(RetryHttp2Test, ConnectivityWatch) {
  gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
  grpc_tracer_set_enabled("client_channel", true);
  grpc_tracer_set_enabled("client_channel_lb_call", true);
  grpc_tracer_set_enabled("subchannel", true);
  grpc_tracer_set_enabled("pick_first", true);
  InitClient(ChannelArgs()
                 .Set(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 1000)
                 .Set(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000)
                 .Set(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 5000));
  // channels should start life in IDLE, and stay there
  EXPECT_EQ(CheckConnectivityState(false), GRPC_CHANNEL_IDLE);
  Step(Duration::Milliseconds(100));
  EXPECT_EQ(CheckConnectivityState(false), GRPC_CHANNEL_IDLE);
  // start watching for a change
  WatchConnectivityState(GRPC_CHANNEL_IDLE, Duration::Milliseconds(500), 1);
  Expect(1, false);
  Step(Duration::Minutes(1));
  // check that we're still in idle, and start connecting
  EXPECT_EQ(CheckConnectivityState(true), GRPC_CHANNEL_IDLE);
  // start watching for a change
  WatchConnectivityState(GRPC_CHANNEL_IDLE, Duration::Seconds(10), 2);
  // and now the watch should trigger
  // (we might miss the notification for CONNECTING, so we might see
  // TRANSIENT_FAILURE instead)
  Expect(2, true);
  Step();
  grpc_connectivity_state state = CheckConnectivityState(false);
  EXPECT_THAT(state, ::testing::AnyOf(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                      GRPC_CHANNEL_CONNECTING));
  // quickly followed by a transition to TRANSIENT_FAILURE
  WatchConnectivityState(GRPC_CHANNEL_CONNECTING, Duration::Seconds(10), 3);
  Expect(3, true);
  Step();
  state = CheckConnectivityState(false);
  EXPECT_EQ(state, GRPC_CHANNEL_TRANSIENT_FAILURE);
  // now let's bring up a server to connect to
  InitServer(ChannelArgs());
  // when the channel gets connected, it will report READY
  WatchConnectivityState(state, Duration::Seconds(20), 4);
  Expect(4, true);
  Step(Duration::Seconds(30));
  state = CheckConnectivityState(false);
  EXPECT_EQ(state, GRPC_CHANNEL_READY);
  // bring down the server again
  // we should go immediately to IDLE
  WatchConnectivityState(GRPC_CHANNEL_READY, Duration::Seconds(10), 5);
  ShutdownServerAndNotify(1000);
  Expect(5, true);
  Expect(1000, true);
  Step();
  state = CheckConnectivityState(false);
  EXPECT_EQ(state, GRPC_CHANNEL_IDLE);
}

}  // namespace
}  // namespace grpc_core
