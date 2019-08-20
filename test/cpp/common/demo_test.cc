/*
 *
 * Copyright 2019 gRPC authors.
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


#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

TEST(DemoTest, FailsOnMacOS) {
  grpc_init();
  grpc_core::ExecCtx exec_ctx;
  grpc_shutdown_blocking();
}

TEST(DemoTest, PassesOnMacOS1) {
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
  }
  grpc_shutdown_blocking();
}

TEST(DemoTest, PassesOnMacOS2) {
  grpc_init();
  grpc_core::ExecCtx exec_ctx;
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
