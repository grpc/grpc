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

#include "src/core/lib/event_engine/default_event_engine_factory.h"

#include <dirent.h>
#include <grpc/event_engine/event_engine.h>

#include <cstddef>
#include <memory>

#include "src/core/config/config_vars.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_event_engine::experimental {
namespace {

// Above the largest engine default, which is 16.
constexpr int kReserveThreads = 32;

// Returns the number of threads in this process.
size_t ProcessThreadCount() {
  DIR* dir = opendir("/proc/self/task");
  EXPECT_NE(dir, nullptr);
  if (dir == nullptr) return 0;
  size_t count = 0;
  // Each entry other than "." and ".." is one thread.
  while (dirent* entry = readdir(dir)) {
    if (entry->d_name[0] != '.') ++count;
  }
  closedir(dir);
  return count;
}

// Returns how many threads DefaultEventEngineFactory starts with the given
// reserve thread override.
size_t ThreadsStartedByFactory(int reserve_threads) {
  grpc_core::ConfigVars::Overrides overrides;
  overrides.event_engine_reserve_threads = reserve_threads;
  grpc_core::ConfigVars::SetOverrides(overrides);
  const size_t before = ProcessThreadCount();
  std::shared_ptr<EventEngine> engine = DefaultEventEngineFactory();
  const size_t after = ProcessThreadCount();
  engine.reset();
  grpc_core::ConfigVars::Reset();
  return after - before;
}

TEST(DefaultEventEngineFactoryTest,
     DefaultEventEngineFactory_StartsConfiguredReserveThreads_WhenSet) {
  EXPECT_GE(ThreadsStartedByFactory(kReserveThreads), kReserveThreads);
}

TEST(DefaultEventEngineFactoryTest,
     DefaultEventEngineFactory_StartsEngineDefaultThreads_WhenZero) {
  EXPECT_LT(ThreadsStartedByFactory(0), kReserveThreads);
}

}  // namespace
}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
