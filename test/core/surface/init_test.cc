//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "src/core/lib/surface/init.h"

#include <chrono>
#include <memory>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

static void test(int rounds) {
  int i;
  for (i = 0; i < rounds; i++) {
    grpc_init();
  }
  for (i = 0; i < rounds; i++) {
    grpc_shutdown();
  }
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, test) {
  test(1);
  test(2);
  test(3);
}

static void test_blocking(int rounds) {
  int i;
  for (i = 0; i < rounds; i++) {
    grpc_init();
  }
  for (i = 0; i < rounds; i++) {
    grpc_shutdown_blocking();
  }
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, blocking) {
  test_blocking(1);
  test_blocking(2);
  test_blocking(3);
}

TEST(Init, ShutdownWithThread) {
  grpc_init();
  {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx(
        GRPC_APP_CALLBACK_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);
    grpc_shutdown();
  }
  grpc_maybe_wait_for_async_shutdown();
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, mixed) {
  grpc_init();
  grpc_init();
  grpc_shutdown();
  grpc_init();
  grpc_shutdown();
  grpc_shutdown();
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, MixedWithThread) {
  grpc_init();
  {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx(
        GRPC_APP_CALLBACK_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);
    grpc_init();
    grpc_shutdown();
    grpc_init();
    grpc_shutdown();
    grpc_shutdown();
  }
  grpc_maybe_wait_for_async_shutdown();
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, Repeatedly) {
  for (int i = 0; i < 10; i++) {
    grpc_init();
    {
      grpc_core::ApplicationCallbackExecCtx callback_exec_ctx(
          GRPC_APP_CALLBACK_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);
      grpc_shutdown();
    }
  }
  grpc_maybe_wait_for_async_shutdown();
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, RepeatedlyBlocking) {
  for (int i = 0; i < 10; i++) {
    grpc_init();
    {
      grpc_core::ApplicationCallbackExecCtx callback_exec_ctx(
          GRPC_APP_CALLBACK_EXEC_CTX_FLAG_IS_INTERNAL_THREAD);
      grpc_shutdown_blocking();
    }
  }
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, TimerManagerHoldsLastInit) {
  grpc_init();
  // the temporary engine is deleted immediately, and the callback owns a copy.
  auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  engine->RunAfter(
      std::chrono::seconds(1),
      [engine = grpc_event_engine::experimental::GetDefaultEventEngine()] {
        grpc_core::ApplicationCallbackExecCtx app_exec_ctx;
        grpc_core::ExecCtx exec_ctx;
        grpc_shutdown();
      });
  while (engine.use_count() != 1) {
    absl::SleepFor(absl::Microseconds(15));
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
