/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/surface/init.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

static int g_plugin_state;

static void plugin_init(void) { g_plugin_state = 1; }
static void plugin_destroy(void) { g_plugin_state = 2; }
static bool plugin_is_intialized(void) { return g_plugin_state == 1; }
static bool plugin_is_destroyed(void) { return g_plugin_state == 2; }

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

TEST(Init, shutdown_with_thread) {
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

TEST(Init, mixed_with_thread) {
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

TEST(Init, plugin) {
  grpc_init();
  EXPECT_TRUE(plugin_is_intialized());
  grpc_shutdown_blocking();
  EXPECT_TRUE(plugin_is_destroyed());
  EXPECT_FALSE(grpc_is_initialized());
}

TEST(Init, repeatedly) {
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

TEST(Init, repeatedly_blocking) {
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

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_register_plugin(plugin_init, plugin_destroy);
  return RUN_ALL_TESTS();
}
