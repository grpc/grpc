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

#include <grpc/event_engine/event_engine.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "src/core/config/config_vars.h"
#include "src/core/util/no_destruct.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_event_engine::experimental {
namespace {

// The engine default reserve thread count is the core count clamped to
// [kMinDefaultReserveThreads, kMaxDefaultReserveThreads].
constexpr int kMinDefaultReserveThreads = 4;
constexpr int kMaxDefaultReserveThreads = 16;
// Two reserve thread counts inside the default range.
constexpr int kFewerReserveThreads = 6;
constexpr int kMoreReserveThreads = 10;

class DefaultEventEngineFactoryTest : public testing::Test {
 protected:
  // Destroys the engines only after every test has counted threads, so no
  // pool thread exits while a test is counting.
  static void TearDownTestSuite() { engines_->clear(); }

  void TearDown() override { grpc_core::ConfigVars::Reset(); }

  // Returns how many threads DefaultEventEngineFactory starts with the given
  // reserve thread override. The engine is kept until the suite ends.
  static size_t ThreadsStartedByFactory(int reserve_threads) {
    grpc_core::ConfigVars::Overrides overrides;
    overrides.event_engine_reserve_threads = reserve_threads;
    grpc_core::ConfigVars::SetOverrides(overrides);
    const size_t before = ProcessThreadCount();
    engines_->push_back(DefaultEventEngineFactory());
    return ProcessThreadCount() - before;
  }

 private:
  static grpc_core::NoDestruct<std::vector<std::shared_ptr<EventEngine>>>
      engines_;
};

grpc_core::NoDestruct<std::vector<std::shared_ptr<EventEngine>>>
    DefaultEventEngineFactoryTest::engines_;

TEST_F(DefaultEventEngineFactoryTest,
       DefaultEventEngineFactory_StartsConfiguredReserveThreads_WhenSet) {
  EXPECT_EQ(ThreadsStartedByFactory(kMoreReserveThreads) -
                ThreadsStartedByFactory(kFewerReserveThreads),
            kMoreReserveThreads - kFewerReserveThreads);
}

TEST_F(DefaultEventEngineFactoryTest,
       DefaultEventEngineFactory_StartsFewerThreads_WhenSetBelowDefault) {
  EXPECT_LT(ThreadsStartedByFactory(2), ThreadsStartedByFactory(0));
}

TEST_F(DefaultEventEngineFactoryTest,
       DefaultEventEngineFactory_StartsDefaultRangeThreads_WhenZero) {
  const size_t started = ThreadsStartedByFactory(0);
  EXPECT_GE(started, ThreadsStartedByFactory(kMinDefaultReserveThreads));
  EXPECT_LE(started, ThreadsStartedByFactory(kMaxDefaultReserveThreads));
}

}  // namespace
}  // namespace grpc_event_engine::experimental

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
