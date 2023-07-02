// Copyright 2023 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE

#include <thread>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/util/port.h"

using namespace std::chrono_literals;

namespace grpc_event_engine {
namespace experimental {

TEST(CFEventEngineTest, TestConnectionTimeout) {
  // use a non-routable IP so connection will timeout
  auto resolved_addr = URIToResolvedAddress("ipv4:10.255.255.255:1234");
  GPR_ASSERT(resolved_addr.ok());

  grpc_core::MemoryQuota memory_quota("cf_engine_test");
  grpc_core::Notification client_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();

  ChannelArgsEndpointConfig config(grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default()));
  cf_engine->Connect(
      [&client_signal](auto endpoint) {
        EXPECT_EQ(endpoint.status().code(),
                  absl::StatusCode::kDeadlineExceeded);
        client_signal.Notify();
      },
      *resolved_addr, config, memory_quota.CreateMemoryAllocator("conn1"), 1ms);

  client_signal.WaitForNotification();
  GPR_ASSERT(false);
}

TEST(CFEventEngineTest, TestConnectionCancelled) {
  // use a non-routable IP so to cancel connection before timeout
  auto resolved_addr = URIToResolvedAddress("ipv4:10.255.255.255:1234");
  GPR_ASSERT(resolved_addr.ok());

  grpc_core::MemoryQuota memory_quota("cf_engine_test");
  grpc_core::Notification client_signal;
  auto cf_engine = std::make_shared<CFEventEngine>();

  ChannelArgsEndpointConfig config(grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default()));
  auto conn_handle = cf_engine->Connect(
      [&client_signal](auto endpoint) {
        EXPECT_EQ(endpoint.status().code(), absl::StatusCode::kCancelled);
        client_signal.Notify();
      },
      *resolved_addr, config, memory_quota.CreateMemoryAllocator("conn1"), 1h);

  cf_engine->CancelConnect(conn_handle);
  client_signal.WaitForNotification();
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int status = RUN_ALL_TESTS();
  grpc_shutdown();
  return status;
}

#else  // not GPR_APPLE
int main(int /* argc */, char** /* argv */) { return 0; }
#endif
