/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <thread>
#include <vector>
#include <algorithm>

#include <gmock/gmock.h>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace {

TEST(PortPickerTest, TestPortPickingIsThreadSafe) {
  // Test that port-picking is thread safe, so that we don't
  // get suprising behavior in tests that assume it.
  // 32 threads is small enough so as to not overload the port server
  // (used in some environments), but large enough to hit concurrency issues
  // if they exist.
  const int kNumConcurrentPicks = 64;
  std::vector<std::unique_ptr<std::thread>> port_picking_threads;
  port_picking_threads.reserve(kNumConcurrentPicks);
  for (int i = 0; i < kNumConcurrentPicks; i++) {
    port_picking_threads.push_back(absl::make_unique<std::thread>([]() {
      const int kNumPicksPerThread = 20;
      for (int k = 0; k < kNumPicksPerThread; k++) {
        int selected_port = grpc_pick_unused_port_or_die();
        GPR_ASSERT(selected_port != 0);
        grpc_recycle_unused_port(selected_port);
      }
    }));
  }
  for (auto &t : port_picking_threads) {
    t->join();
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
