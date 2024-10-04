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

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <stddef.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "src/core/util/env.h"
#include "test/core/test_util/fake_udp_and_tcp_server.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace {

void TryConnectAndDestroy(const char* fake_metadata_server_address) {
  grpc::ChannelArguments args;
  std::string target = "google-c2p:///servername_not_used";
  args.SetInt("grpc.testing.google_c2p_resolver_pretend_running_on_gcp", 1);
  args.SetString("grpc.testing.google_c2p_resolver_metadata_server_override",
                 fake_metadata_server_address);
  auto channel = grpc::CreateCustomChannel(
      target, grpc::InsecureChannelCredentials(), args);
  // Start connecting, and give some time for the google-c2p resolver to begin
  // resolution and start trying to contact the metadata server.
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  channel.reset();
};

// Exercise the machinery involved with shutting down the C2P resolver while
// it's waiting for its initial metadata server queries to finish.
TEST(DestroyGoogleC2pChannelWithActiveConnectStressTest,
     LoopTryConnectAndDestroyWithHangingMetadataServer) {
  // Create a fake metadata server which hangs.
  grpc_core::testing::FakeUdpAndTcpServer fake_metadata_server(
      grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
          kWaitForClientToSendFirstBytes,
      grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  std::vector<std::unique_ptr<std::thread>> threads;
  const int kNumThreads = 10;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        new std::thread(TryConnectAndDestroy, fake_metadata_server.address()));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
}

// Exercise the machinery involved with shutting down the C2P resolver while
// it's waiting for its initial metadata server queries to finish.
TEST(DestroyGoogleC2pChannelWithActiveConnectStressTest,
     LoopTryConnectAndDestroyWithFastFailingMetadataServer) {
  // Create a fake metadata server address which rejects connections
  int port = grpc_pick_unused_port_or_die();
  std::string address = absl::StrFormat("[::1]:%d", port);
  std::vector<std::unique_ptr<std::thread>> threads;
  const int kNumThreads = 10;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        new std::thread(TryConnectAndDestroy, address.c_str()));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int result;
  {
    grpc_core::testing::FakeUdpAndTcpServer fake_xds_server(
        grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
            kWaitForClientToSendFirstBytes,
        grpc_core::testing::FakeUdpAndTcpServer::
            CloseSocketUponReceivingBytesFromPeer);
    grpc_core::SetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI",
                      fake_xds_server.address());
    result = RUN_ALL_TESTS();
  }
  grpc_shutdown();
  return result;
}
