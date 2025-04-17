// Copyright 2022 The gRPC Authors
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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "test/core/event_engine/mock_event_engine.h"
#include "test/core/event_engine/util/aborting_event_engine.h"
#include "test/core/test_util/test_config.h"

namespace {
using ::grpc_event_engine::experimental::AbortingEventEngine;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::EventEngineFactoryReset;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::MockEventEngine;
using ::grpc_event_engine::experimental::SetEventEngineFactory;

class EventEngineFactoryTest : public testing::Test {
 public:
  EventEngineFactoryTest() = default;
  ~EventEngineFactoryTest() override { EventEngineFactoryReset(); }
};

TEST_F(EventEngineFactoryTest, CustomFactoryIsUsed) {
  int counter{0};
  SetEventEngineFactory([&counter] {
    ++counter;
    return std::make_unique<AbortingEventEngine>();
  });
  auto ee1 = GetDefaultEventEngine();
  ASSERT_EQ(counter, 1);
  auto ee2 = GetDefaultEventEngine();
  ASSERT_EQ(counter, 1);
  ASSERT_EQ(ee1, ee2);
}

TEST_F(EventEngineFactoryTest, FactoryResetWorks) {
  int counter{0};
  SetEventEngineFactory([&counter]() {
    // this factory should only be used twice;
    EXPECT_LE(++counter, 2);
    return std::make_shared<AbortingEventEngine>();
  });
  auto custom_ee = GetDefaultEventEngine();
  ASSERT_EQ(counter, 1);
  auto same_ee = GetDefaultEventEngine();
  ASSERT_EQ(custom_ee, same_ee);
  ASSERT_EQ(counter, 1);
  EventEngineFactoryReset();
  auto default_ee = GetDefaultEventEngine();
  ASSERT_NE(custom_ee, default_ee);
}

TEST_F(EventEngineFactoryTest, SharedPtrGlobalEventEngineLifetimesAreValid) {
  int create_count = 0;
  grpc_event_engine::experimental::SetEventEngineFactory([&create_count] {
    ++create_count;
    return std::make_shared<MockEventEngine>();
  });
  std::shared_ptr<EventEngine> ee2;
  ASSERT_EQ(0, create_count);
  {
    std::shared_ptr<EventEngine> ee1 = GetDefaultEventEngine();
    ASSERT_EQ(1, create_count);
    ee2 = GetDefaultEventEngine();
    ASSERT_EQ(1, create_count);
    // 3 instances: ee1, ee2, and the ee owned by the factory
    ASSERT_EQ(ee2.use_count(), 2);
  }
  // Ensure the first shared_ptr did not delete the global
  ASSERT_EQ(ee2.use_count(), 1);
  // destroy the global engine via the last shared_ptr, and create a new one.
  ee2.reset();
  ee2 = GetDefaultEventEngine();
  ASSERT_EQ(2, create_count);
  ASSERT_EQ(ee2.use_count(), 1);
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
