/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/gprpp/fork.h"

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

static void test_init() {
  GPR_ASSERT(!grpc_core::Fork::Enabled());

  // Default fork support (disabled)
  grpc_core::Fork::GlobalInit();
  GPR_ASSERT(!grpc_core::Fork::Enabled());
  grpc_core::Fork::GlobalShutdown();

  // Explicitly disabled fork support
  grpc_core::Fork::Enable(false);
  grpc_core::Fork::GlobalInit();
  GPR_ASSERT(!grpc_core::Fork::Enabled());
  grpc_core::Fork::GlobalShutdown();

  // Explicitly enabled fork support
  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();
  GPR_ASSERT(grpc_core::Fork::Enabled());
  grpc_core::Fork::GlobalShutdown();
}

// This spawns CONCURRENT_TEST_THREADS that last up to
// THREAD_DELAY_MS, and checks that the Fork::AwaitThreads()
// returns roughly after THREAD_DELAY_MS.  The epsilon is high
// because tsan threads can take a while to spawn/join.
#define THREAD_DELAY_MS 6000
#define THREAD_DELAY_EPSILON 1500
#define CONCURRENT_TEST_THREADS 100

static void sleeping_thd(void* arg) {
  int64_t sleep_ms = reinterpret_cast<int64_t>(arg);
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(sleep_ms, GPR_TIMESPAN)));
}

static void test_thd_count() {
  // Test no active threads
  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();
  grpc_core::Fork::AwaitThreads();
  grpc_core::Fork::GlobalShutdown();

  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();
  grpc_core::Thread thds[CONCURRENT_TEST_THREADS];
  gpr_timespec est_end_time =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_millis(THREAD_DELAY_MS, GPR_TIMESPAN));
  gpr_timespec tolerance =
      gpr_time_from_millis(THREAD_DELAY_EPSILON, GPR_TIMESPAN);
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
  GPR_ASSERT(gpr_time_similar(end_time, est_end_time, tolerance));
  grpc_core::Fork::GlobalShutdown();
}

static void exec_ctx_thread(void* arg) {
  bool* exec_ctx_created = static_cast<bool*>(arg);
  grpc_core::Fork::IncExecCtxCount();
  *exec_ctx_created = true;
}

static void test_exec_count() {
  grpc_core::Fork::Enable(true);
  grpc_core::Fork::GlobalInit();

  grpc_core::Fork::IncExecCtxCount();
  GPR_ASSERT(grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  grpc_core::Fork::AllowExecCtx();

  grpc_core::Fork::IncExecCtxCount();
  grpc_core::Fork::IncExecCtxCount();
  GPR_ASSERT(!grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  grpc_core::Fork::DecExecCtxCount();

  grpc_core::Fork::IncExecCtxCount();
  GPR_ASSERT(grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  grpc_core::Fork::AllowExecCtx();

  // Test that block_exec_ctx() blocks grpc_core::Fork::IncExecCtxCount
  bool exec_ctx_created = false;
  grpc_core::Thread thd =
      grpc_core::Thread("grpc_fork_test", exec_ctx_thread, &exec_ctx_created);
  grpc_core::Fork::IncExecCtxCount();
  GPR_ASSERT(grpc_core::Fork::BlockExecCtx());
  grpc_core::Fork::DecExecCtxCount();
  thd.Start();
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(1, GPR_TIMESPAN)));
  GPR_ASSERT(!exec_ctx_created);
  grpc_core::Fork::AllowExecCtx();
  thd.Join();  // This ensure that the call got un-blocked
  grpc_core::Fork::GlobalShutdown();
}

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment env(argc, argv);
  test_init();
  test_thd_count();
  test_exec_count();

  return 0;
}
