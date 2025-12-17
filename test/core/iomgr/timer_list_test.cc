/*
 *
 * Copyright 2025 gRPC authors.
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

#include "src/core/lib/iomgr/timer.h"

#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

TEST(TimerListTest, DoubleShutdownIsSafe) {
  grpc_init();
  // grpc_init calls iomgr_init, which calls grpc_timer_list_init.

  // First shutdown.
  grpc_timer_list_shutdown();

  // Second shutdown - should not crash.
  grpc_timer_list_shutdown();

  grpc_shutdown();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
