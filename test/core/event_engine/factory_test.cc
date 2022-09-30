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

#include <grpc/support/port_platform.h>

#include <memory>

#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "test/core/event_engine/util/aborting_event_engine.h"
#include "test/core/util/test_config.h"

namespace {
using ::grpc_event_engine::experimental::AbortingEventEngine;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::EventEngineFactoryReset;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
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
  SetEventEngineFactory([&counter]() -> std::unique_ptr<EventEngine> {
    // this factory should only be used twice;
    EXPECT_LE(++counter, 2);
    return std::make_unique<AbortingEventEngine>();
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
}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
