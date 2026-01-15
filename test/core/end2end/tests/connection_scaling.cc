//
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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <memory>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/scoped_env_var.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(Http2SingleHopTests, SubchannelConnectionScaling) {
  SKIP_IF_MINSTACK();
  if (!IsSubchannelConnectionScalingEnabled()) {
    GTEST_SKIP()
        << "this test requires the subchannel_connection_scaling experiment";
  }
  testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_MAX_CONCURRENT_STREAMS_CONNECTION_SCALING");
  constexpr int kMaxConcurrentStreams = 3;
  InitServer(DefaultServerArgs().Set(GRPC_ARG_MAX_CONCURRENT_STREAMS,
                                     kMaxConcurrentStreams));
  constexpr char kServiceConfig[] =
      "{\n"
      "  \"connectionScaling\": {\n"
      "    \"maxConnectionsPerSubchannel\": 2\n"
      "  }\n"
      "}";
  InitClient(ChannelArgs().Set(GRPC_ARG_SERVICE_CONFIG, kServiceConfig));
  // Start 3 RPCs.  Each one sends initial metadata and is seen by the server.
  // First RPC.
  auto c1 = NewClientCall("/alpha").Timeout(Duration::Seconds(1000)).Create();
  c1.NewBatch(101).SendInitialMetadata({});
  IncomingStatusOnClient server_status1;
  c1.NewBatch(102).RecvStatusOnClient(server_status1);
  Expect(101, true);
  Step();
  auto s1 = RequestCall(201);
  Expect(201, true);
  Step();
  // Second RPC.
  auto c2 = NewClientCall("/beta").Timeout(Duration::Seconds(1000)).Create();
  c2.NewBatch(301).SendInitialMetadata({});
  IncomingStatusOnClient server_status2;
  c2.NewBatch(302).RecvStatusOnClient(server_status2);
  Expect(301, true);
  Step();
  auto s2 = RequestCall(401);
  Expect(401, true);
  Step();
  // Third RPC.
  auto c3 = NewClientCall("/gamma").Timeout(Duration::Seconds(1000)).Create();
  c3.NewBatch(501).SendInitialMetadata({});
  IncomingStatusOnClient server_status3;
  c3.NewBatch(502).RecvStatusOnClient(server_status3);
  Expect(501, true);
  Step();
  auto s3 = RequestCall(601);
  Expect(601, true);
  Step();
  // Those three RPCs should all be on the same connection.
  EXPECT_EQ(s1.GetPeer(), s2.GetPeer());
  EXPECT_EQ(s1.GetPeer(), s3.GetPeer());
  // Start a 4th RPC, which should trigger a new connection.
  auto c4 = NewClientCall("/delta").Timeout(Duration::Seconds(1000)).Create();
  c4.NewBatch(701).SendInitialMetadata({});
  IncomingStatusOnClient server_status4;
  c4.NewBatch(702).RecvStatusOnClient(server_status4);
  Expect(701, true);
  Step();
  auto s4 = RequestCall(801);
  Expect(801, true);
  Step();
  EXPECT_NE(s1.GetPeer(), s4.GetPeer());
  // Clean up.
  c1.Cancel();
  c2.Cancel();
  c3.Cancel();
  c4.Cancel();
  Expect(102, AnyStatus{});
  Expect(302, AnyStatus{});
  Expect(502, AnyStatus{});
  Expect(702, AnyStatus{});
  Step();
}

CORE_END2END_TEST(Http2SingleHopTests, HonorsMaxConnectionsPerSubchannel) {
  SKIP_IF_MINSTACK();
  if (!IsSubchannelConnectionScalingEnabled()) {
    GTEST_SKIP()
        << "this test requires the subchannel_connection_scaling experiment";
  }
  testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_MAX_CONCURRENT_STREAMS_CONNECTION_SCALING");
  constexpr int kMaxConcurrentStreams = 2;
  InitServer(DefaultServerArgs().Set(GRPC_ARG_MAX_CONCURRENT_STREAMS,
                                     kMaxConcurrentStreams));
  constexpr char kServiceConfig[] =
      "{\n"
      "  \"connectionScaling\": {\n"
      "    \"maxConnectionsPerSubchannel\": 2\n"
      "  }\n"
      "}";
  InitClient(ChannelArgs().Set(GRPC_ARG_SERVICE_CONFIG, kServiceConfig));
  // Start 4 RPCs, which is enough to use all quota for all connections.
  // Each one sends initial metadata and is seen by the server.
  // First RPC.
  auto c1 = NewClientCall("/alpha").Timeout(Duration::Seconds(1000)).Create();
  c1.NewBatch(101).SendInitialMetadata({});
  IncomingStatusOnClient server_status1;
  c1.NewBatch(102).RecvStatusOnClient(server_status1);
  Expect(101, true);
  Step();
  auto s1 = RequestCall(201);
  Expect(201, true);
  Step();
  // Second RPC.
  auto c2 = NewClientCall("/beta").Timeout(Duration::Seconds(1000)).Create();
  c2.NewBatch(301).SendInitialMetadata({});
  IncomingStatusOnClient server_status2;
  c2.NewBatch(302).RecvStatusOnClient(server_status2);
  Expect(301, true);
  Step();
  auto s2 = RequestCall(401);
  Expect(401, true);
  Step();
  // First two RPCs should be on the same connection.
  EXPECT_EQ(s1.GetPeer(), s2.GetPeer());
  // Third RPC.
  auto c3 = NewClientCall("/gamma").Timeout(Duration::Seconds(1000)).Create();
  c3.NewBatch(501).SendInitialMetadata({});
  IncomingStatusOnClient server_status3;
  c3.NewBatch(502).RecvStatusOnClient(server_status3);
  Expect(501, true);
  Step();
  auto s3 = RequestCall(601);
  Expect(601, true);
  Step();
  // Fourth RPC.
  auto c4 = NewClientCall("/delta").Timeout(Duration::Seconds(1000)).Create();
  c4.NewBatch(701).SendInitialMetadata({});
  IncomingStatusOnClient server_status4;
  c4.NewBatch(702).RecvStatusOnClient(server_status4);
  Expect(701, true);
  Step();
  auto s4 = RequestCall(801);
  Expect(801, true);
  Step();
  // Third and fourth RPCs should be on the same connection, which is
  // different from the connection of the first two.
  EXPECT_EQ(s3.GetPeer(), s4.GetPeer());
  EXPECT_NE(s1.GetPeer(), s3.GetPeer());
  // Start a 5th RPC, which will be queued.
  auto c5 = NewClientCall("/epsilon").Timeout(Duration::Seconds(1000)).Create();
  c5.NewBatch(901).SendInitialMetadata({});
  IncomingStatusOnClient server_status5;
  c5.NewBatch(902).RecvStatusOnClient(server_status5);
  auto s5 = RequestCall(1001);
  Step();  // Nothing completes yet.
  // Cancel the first RPC, which will free up quota for the 5th RPC to
  // be sent on the first connection.
  c1.Cancel();
  Expect(102, AnyStatus{});  // First RPC sees status.
  Expect(901, true);         // Client sees 5th RPC start.
  Expect(1001, true);        // Server sees 5th RPC.
  Step();
  // The 5th RPC should be sent on the first connection.
  EXPECT_EQ(s5.GetPeer(), s1.GetPeer());
  // Clean up.
  c2.Cancel();
  c3.Cancel();
  c4.Cancel();
  c5.Cancel();
  Expect(302, AnyStatus{});
  Expect(502, AnyStatus{});
  Expect(702, AnyStatus{});
  Expect(902, AnyStatus{});
  Step();
}

}  // namespace
}  // namespace grpc_core
