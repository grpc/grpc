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
#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/util/notification.h"
#include "test/core/event_engine/mock_event_engine.h"
#include "test/core/test_util/test_config.h"

namespace {

using ::grpc_event_engine::experimental::GetDefaultEventEngine;

class DefaultEngineTest : public testing::Test {
 protected:
  class CountingEngine
      : public grpc_event_engine::experimental::MockEventEngine {
   public:
    struct EngineOpCounts {
      EngineOpCounts() : constructed(0), destroyed(0), ran(0), ran_after(0) {};
      int constructed;
      int destroyed;
      int ran;
      int ran_after;
    };
    explicit CountingEngine(EngineOpCounts& counter) : counter_(counter) {
      ++counter_.constructed;
    }
    ~CountingEngine() override { ++counter_.destroyed; }
    void Run(Closure* /* closure */) override { ++counter_.ran; };
    void Run(absl::AnyInvocable<void()> /* closure */) override {
      ++counter_.ran;
    };
    TaskHandle RunAfter(Duration /* when */, Closure* /* closure */) override {
      ++counter_.ran_after;
      return {-1, -1};
    }
    TaskHandle RunAfter(Duration /* when */,
                        absl::AnyInvocable<void()> /* closure */) override {
      ++counter_.ran_after;
      return {-1, -1};
    }

   private:
    EngineOpCounts& counter_;
  };
};

TEST_F(DefaultEngineTest, ScopedEngineLifetime) {
  DefaultEngineTest::CountingEngine::EngineOpCounts op_counts;
  {
    auto engine =
        std::make_shared<DefaultEngineTest::CountingEngine>(op_counts);
    grpc_event_engine::experimental::DefaultEventEngineScope engine_scope(
        std::move(engine));
    ASSERT_EQ(op_counts.constructed, 1);
    ASSERT_EQ(op_counts.ran, 0);
    {
      auto ee2 = GetDefaultEventEngine();
      ASSERT_EQ(op_counts.constructed, 1);
      ee2->Run([]() {});
      // Ensure that ee2 is the CountingEngine
      ASSERT_EQ(op_counts.ran, 1);
    }
    // Destroying the ee2 should not destroy the shared engine.
    ASSERT_EQ(op_counts.destroyed, 0);
  }
  // When the DefaultEventEngineScope goes out of scope, the engine is
  // destroyed.
  ASSERT_EQ(op_counts.destroyed, 1);
  // Getting a new EE will not return the destroyed CountingEngine. It should
  // create a default internal engine, and Run should work.
  auto ee3 = GetDefaultEventEngine();
  grpc_core::Notification notification;
  ee3->Run([&notification]() { notification.Notify(); });
  notification.WaitForNotification();
  ASSERT_EQ(op_counts.constructed, 1);
  ASSERT_EQ(op_counts.destroyed, 1);
  ASSERT_EQ(op_counts.ran, 1);
}

TEST_F(DefaultEngineTest, ProvidedDefaultEngineHasPrecedenceOverFactory) {
  DefaultEngineTest::CountingEngine::EngineOpCounts ee1_op_counts;
  DefaultEngineTest::CountingEngine::EngineOpCounts ee2_op_counts;
  grpc_event_engine::experimental::SetEventEngineFactory([&ee2_op_counts]() {
    return std::make_shared<DefaultEngineTest::CountingEngine>(ee2_op_counts);
  });
  ASSERT_EQ(ee2_op_counts.constructed, 0);
  // Ensure the factory is used
  {
    auto tmp_engine = GetDefaultEventEngine();
    ASSERT_EQ(ee2_op_counts.constructed, 1);
  }
  ASSERT_EQ(ee2_op_counts.destroyed, 1);
  // Set a custom engine, and ensure it takes precedent over the factory
  {
    grpc_event_engine::experimental::DefaultEventEngineScope engine_scope(
        std::make_shared<DefaultEngineTest::CountingEngine>(ee1_op_counts));
    auto tmp_engine = GetDefaultEventEngine();
    ASSERT_EQ(ee2_op_counts.constructed, 1)
        << "The factory should not have been used to create a default engine";
    ASSERT_EQ(ee1_op_counts.constructed, 1);
  }
  // The default engine will have been unset.
  auto tmp_engine = GetDefaultEventEngine();
  ASSERT_EQ(ee2_op_counts.constructed, 2);
  grpc_event_engine::experimental::EventEngineFactoryReset();
}

TEST_F(DefaultEngineTest, ProvidedDefaultEngineResetsExistingInternalEngine) {
  auto internal_engine = GetDefaultEventEngine();
  DefaultEngineTest::CountingEngine::EngineOpCounts op_counts;
  {
    grpc_event_engine::experimental::DefaultEventEngineScope engine_scope(
        std::make_shared<DefaultEngineTest::CountingEngine>(op_counts));
    auto user_engine = GetDefaultEventEngine();
    ASSERT_GE(internal_engine.use_count(), 1);
    ASSERT_EQ(op_counts.constructed, 1);
  }
  ASSERT_EQ(op_counts.destroyed, 1);
  // The next default engine should not match either previous engine
  auto third_engine = GetDefaultEventEngine();
  ASSERT_NE(third_engine.get(), internal_engine.get());
  // Sanity check that both engines work
  grpc_core::Notification ran1, ran2;
  internal_engine->Run([&ran1]() { ran1.Notify(); });
  third_engine->Run([&ran2]() { ran2.Notify(); });
  ran1.WaitForNotification();
  ran2.WaitForNotification();
  ASSERT_EQ(op_counts.constructed, 1);
  ASSERT_EQ(op_counts.destroyed, 1);
}

TEST_F(DefaultEngineTest, StressTestSharedPtr) {
  constexpr int thread_count = 13;
  constexpr absl::Duration spin_time = absl::Seconds(3);
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int i = 0; i < thread_count; i++) {
    threads.emplace_back([&spin_time] {
      auto timeout = absl::Now() + spin_time;
      do {
        GetDefaultEventEngine().reset();
      } while (timeout > absl::Now());
    });
  }
  for (auto& thd : threads) {
    thd.join();
  }
}
}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
