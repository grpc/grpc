// Copyright 2025 gRPC authors.
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

#include "test/core/test_util/test_timeout.h"

#include <grpc/grpc.h>

#include <memory>
#include <thread>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/util/time.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(TestTimeoutTest, NoCrashIfDestroyedBeforeTimeout) {
  auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  {
    TestTimeout timeout(Duration::Milliseconds(10), engine);
  }
  // Wait longer than timeout to ensure it doesn't crash
  std::this_thread::sleep_for(std::chrono::seconds(2));
}

TEST(TestTimeoutTest, CrashIfTimeoutExpires) {
  EXPECT_DEATH(
      {
        auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
        TestTimeout timeout(Duration::Milliseconds(10), engine);
        std::this_thread::sleep_for(std::chrono::seconds(20));
      },
      "");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
