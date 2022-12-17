//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/lib/gprpp/fork.h"

#include <stdint.h>

#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

TEST(ForkTest, Init) {
  ASSERT_FALSE(grpc_core::Fork::Enabled());

  // Default fork support (disabled)
  grpc_core::Fork::GlobalInit();
  ASSERT_FALSE(grpc_core::Fork::Enabled());

  // Explicitly disabled fork support
  grpc_core::Fork::Enable(false);
  grpc_core::Fork::GlobalInit();
  ASSERT_FALSE(grpc_core::Fork::Enabled());

  // Explicitly enabled fork support
  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();
  ASSERT_TRUE(grpc_core::Fork::Enabled());
}

// This spawns CONCURRENT_TEST_THREADS that last up to
// THREAD_DELAY_MS, and checks that the Fork::AwaitThreads()
// returns roughly after THREAD_DELAY_MS.  The epsilon is high
// because tsan threads can take a while to spawn/join.
#define THREAD_DELAY_MS 6000
#define THREAD_DELAY_EPSILON 1500
#define CONCURRENT_TEST_THREADS 10

static void sleeping_thd(void* arg) {
  int64_t sleep_ms = reinterpret_cast<int64_t>(arg);
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(sleep_ms, GPR_TIMESPAN)));
}

TEST(ForkTest, ThdCount) {
  // Test no active threads
  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();
  grpc_core::Fork::AwaitThreads();

  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();
  grpc_core::Thread thds[CONCURRENT_TEST_THREADS];
  gpr_timespec est_end_time =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_millis(THREAD_DELAY_MS, GPR_TIMESPAN));
  gpr_timespec tolerance = gpr_time_from_millis(
      THREAD_DELAY_EPSILON * grpc_test_slowdown_factor(), GPR_TIMESPAN);
  for (int i = 0; i < CONCURRENT_TEST_THREADS; i++) {
    intptr_t sleep_time_ms =
        (i * THREAD_DELAY_MS) / (CONCURRENT_TEST_THREADS - 1);
    thds[i] = grpc_core::Thread("grpc_fork_test", sleeping_thd,
                                reinterpret_cast<void*>(sleep_time_ms));
    thds[i].Start();
  }
  grpc_core::Fork::AwaitThreads();
  gpr_timespec end_time = gpr_now(GPR_CLOCK_REALTIME);
  for (auto& thd : thds) {
    thd.Join();
  }
  ASSERT_TRUE(gpr_time_similar(end_time, est_end_time, tolerance));
}

static void exec_ctx_thread(void* arg) {
  bool* exec_ctx_created = static_cast<bool*>(arg);
  grpc_core::Fork::IncExecCtxCount();
  *exec_ctx_created = true;
}

TEST(ForkTest, ExecCount) {
  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();

  grpc_core::Fork::IncExecCtxCount();
  ASSERT_TRUE(grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  grpc_core::Fork::AllowExecCtx();

  grpc_core::Fork::IncExecCtxCount();
  grpc_core::Fork::IncExecCtxCount();
  ASSERT_FALSE(grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  grpc_core::Fork::DecExecCtxCount();

  grpc_core::Fork::IncExecCtxCount();
  ASSERT_TRUE(grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  grpc_core::Fork::AllowExecCtx();

  // Test that block_exec_ctx() blocks grpc_core::Fork::IncExecCtxCount
  bool exec_ctx_created = false;
  grpc_core::Thread thd =
      grpc_core::Thread("grpc_fork_test", exec_ctx_thread, &exec_ctx_created);
  grpc_core::Fork::IncExecCtxCount();
  ASSERT_TRUE(grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  thd.Start();
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(1, GPR_TIMESPAN)));
  ASSERT_FALSE(exec_ctx_created);
  grpc_core::Fork::AllowExecCtx();
  thd.Join();  // This ensure that the call got un-blocked
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
