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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "test/core/util/test_config.h"

namespace {

using ::testing::MockFunction;

class EventEngineSmokeTest : public testing::Test {};

TEST_F(EventEngineSmokeTest, SetEventEngineFactoryLinks) {
  // See https://github.com/grpc/grpc/pull/28707
  testing::MockFunction<
      std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>
      factory;
  EXPECT_CALL(factory, Call()).Times(1);
  grpc_event_engine::experimental::SetEventEngineFactory(
      factory.AsStdFunction());
  EXPECT_EQ(nullptr, grpc_event_engine::experimental::CreateEventEngine());
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
