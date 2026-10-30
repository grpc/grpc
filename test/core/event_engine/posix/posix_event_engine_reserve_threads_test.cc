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

#include <cstddef>
#include <memory>
#include <vector>

#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_event_engine::experimental {
namespace {

constexpr int kMaxReserveThreads =
    PosixEventEngine::Options::kMaxReserveThreads;

TEST(PosixEventEngineReserveThreadsTest,
     MakePosixEventEngine_StartsMaxReserveThreads_WhenSetAboveMax) {
  // Engines stay alive until the end of the test, so no pool thread exits
  // while threads are counted.
  std::vector<std::shared_ptr<PosixEventEngine>> engines;
  // Returns how many threads an engine with the given reserve count starts.
  auto threads_started = [&engines](int reserve_threads) {
    PosixEventEngine::Options options;
    options.reserve_threads = reserve_threads;
    const size_t before = ProcessThreadCount();
    engines.push_back(PosixEventEngine::MakePosixEventEngine(options));
    return ProcessThreadCount() - before;
  };
  EXPECT_EQ(threads_started(kMaxReserveThreads + 13),
            threads_started(kMaxReserveThreads));
}

}  // namespace
}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
