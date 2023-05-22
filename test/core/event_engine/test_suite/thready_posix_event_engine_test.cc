// Copyright 2023 The gRPC Authors
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
#include <memory>

#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/thready_event_engine/thready_event_engine.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/event_engine/test_suite/posix/oracle_event_engine_posix.h"
#include "test/core/event_engine/test_suite/tests/client_test.h"
#include "test/core/event_engine/test_suite/tests/server_test.h"
#include "test/core/event_engine/test_suite/tests/timer_test.h"
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  SetEventEngineFactories(
      []() {
        return std::make_unique<
            grpc_event_engine::experimental::ThreadyEventEngine>(
            std::make_unique<
                grpc_event_engine::experimental::PosixEventEngine>());
      },
      []() {
        return std::make_unique<
            grpc_event_engine::experimental::ThreadyEventEngine>(
            std::make_unique<
                grpc_event_engine::experimental::PosixOracleEventEngine>());
      });
  grpc_event_engine::experimental::InitTimerTests();
  grpc_event_engine::experimental::InitClientTests();
  grpc_event_engine::experimental::InitServerTests();
  // TODO(ctiller): EventEngine temporarily needs grpc to be initialized first
  // until we clear out the iomgr shutdown code.
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
