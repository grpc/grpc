/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <gmock/gmock.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/impl/codegen/sync.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gpr/env.h"

#include "test/core/util/port.h"
#include "test/core/util/fake_tcp_server.h"
#include "test/core/util/test_config.h"

namespace {

void TryConnectAndDestroy(const char* fake_metadata_server_address) {
  grpc::ChannelArguments args;
  std::string target = "google-c2p:///servername_not_used";
  args.SetInt("grpc.testing.force_running_on_gcp", 1);
  args.SetString("grpc.testing.google_c2p_resolver_metadata_server_override", fake_metadata_server_address);
  auto channel = ::grpc::CreateCustomChannel(
      target, grpc::InsecureChannelCredentials(), args);
  // Start connecting, and give some time for the google-c2p resolver to begin
  // resolution and start trying to contact the metadata server.
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  channel.reset();
};

TEST(DestroyGoogleC2pChannelWithActiveConnectStressTest,
     LoopTryConnectAndDestroy) {
  grpc_init();
  // Create a fake metadata server which hangs.
  FakeTcpServer fake_metadata_server(
      FakeTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
      FakeTcpServer::CloseSocketUponCloseFromPeer);
  std::vector<std::unique_ptr<std::thread>> threads;
  // 100 is picked for number of threads just
  // because it's enough to reproduce a certain crash almost 100%
  // at this time of writing.
  const int kNumThreads = 1;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(new std::thread(TryConnectAndDestroy, fake_metadata_server.address()));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
  grpc_shutdown();
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  gpr_setenv("GRPC_EXPERIMENTAL_GOOGLE_C2P_RESOLVER", "true");
  gpr_setenv("GRPC_ABORT_ON_LEAKS", "true");
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
