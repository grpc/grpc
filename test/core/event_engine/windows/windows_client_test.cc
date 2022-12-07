// Copyright 2022 gRPC authors.
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

#ifdef GPR_WINDOWS
#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "test/core/event_engine/test_utils.h"

namespace grpc_event_engine {
namespace experimental {

using namespace std::chrono_literals;

class WindowsEventEngineClientTest : public testing::Test {};

TEST_F(WindowsEventEngineClientTest, NoOpCommunication) {
  WindowsEventEngine engine;
  grpc_core::Notification signal;
  auto connect_cb =
      [_ = NotifyOnDelete(&signal)](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
        ASSERT_FALSE(endpoint.ok());
        ASSERT_EQ(endpoint.status().code(),
                  absl::StatusCode::kDeadlineExceeded);
      };
  auto addr = URIToResolvedAddress("ipv4:/127.0.0.1:12345");
  ChannelArgsEndpointConfig cfg;
  grpc_core::MemoryQuota quota("test quota");
  auto handle = engine.Connect(std::move(connect_cb), addr, cfg,
                               quota.CreateMemoryAllocator("testing"), 1s);
  signal.WaitForNotification();
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

#else  // not GPR_WINDOWS
int main(int /* argc */, char** /* argv */) { return 0; }
#endif
