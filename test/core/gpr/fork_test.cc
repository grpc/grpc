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

#include "src/core/lib/gpr/fork.h"

#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

static void test_init() {
  GPR_ASSERT(!grpc_fork_support_enabled());

  // Default fork support (disabled)
  grpc_fork_support_init();
  GPR_ASSERT(!grpc_fork_support_enabled());
  grpc_fork_support_destroy();

  // Explicitly disabled fork support
  grpc_enable_fork_support(false);
  grpc_fork_support_init();
  GPR_ASSERT(!grpc_fork_support_enabled());
  grpc_fork_support_destroy();

  // Explicitly enabled fork support
  grpc_enable_fork_support(true);
  grpc_fork_support_init();
  GPR_ASSERT(grpc_fork_support_enabled());
  grpc_fork_support_destroy();
}

#define THREAD_DELAY_MS 3000
#define THREAD_DELAY_EPSILON 500
#define CONCURRENT_TEST_THREADS 100

static void sleeping_thd(void* arg) {
  int64_t sleep_ms = (int64_t)arg;
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(sleep_ms, GPR_TIMESPAN)));
}

static void test_thd_count() {
  // Test no active threads
  grpc_enable_fork_support(true);
  grpc_fork_support_init();
  grpc_fork_await_thds();
  grpc_fork_support_destroy();

  grpc_enable_fork_support(true);
  grpc_fork_support_init();
  grpc_core::Thread thds[CONCURRENT_TEST_THREADS];
  gpr_timespec est_end_time =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                   gpr_time_from_millis(THREAD_DELAY_MS, GPR_TIMESPAN));
  gpr_timespec tolerance =
      gpr_time_from_millis(THREAD_DELAY_EPSILON, GPR_TIMESPAN);
  for (int i = 0; i < CONCURRENT_TEST_THREADS; i++) {
    intptr_t sleep_time_ms =
        (i * THREAD_DELAY_MS) / (CONCURRENT_TEST_THREADS - 1);
    thds[i] =
        grpc_core::Thread("grpc_fork_test", sleeping_thd, (void*)sleep_time_ms);
    thds[i].Start();
  }
  grpc_fork_await_thds();
  gpr_timespec end_time = gpr_now(GPR_CLOCK_REALTIME);
  for (auto& thd : thds) {
    thd.Join();
  }
  GPR_ASSERT(gpr_time_similar(end_time, est_end_time, tolerance));
  grpc_fork_support_destroy();
}

static void exec_ctx_thread(void* arg) {
  bool* exec_ctx_created = (bool*)arg;
  grpc_fork_inc_exec_ctx_count();
  *exec_ctx_created = true;
}

static void test_exec_count() {
  grpc_fork_inc_exec_ctx_count();
  grpc_enable_fork_support(true);
  grpc_fork_support_init();

  grpc_fork_inc_exec_ctx_count();
  GPR_ASSERT(grpc_fork_block_exec_ctx());
  grpc_fork_dec_exec_ctx_count();
  grpc_fork_allow_exec_ctx();

  grpc_fork_inc_exec_ctx_count();
  grpc_fork_inc_exec_ctx_count();
  GPR_ASSERT(!grpc_fork_block_exec_ctx());
  grpc_fork_dec_exec_ctx_count();
  grpc_fork_dec_exec_ctx_count();

  grpc_fork_inc_exec_ctx_count();
  GPR_ASSERT(grpc_fork_block_exec_ctx());
  grpc_fork_dec_exec_ctx_count();
  grpc_fork_allow_exec_ctx();

  // Test that block_exec_ctx() blocks grpc_fork_inc_exec_ctx_count
  bool exec_ctx_created = false;
  grpc_core::Thread thd =
      grpc_core::Thread("grpc_fork_test", exec_ctx_thread, &exec_ctx_created);
  grpc_fork_inc_exec_ctx_count();
  GPR_ASSERT(grpc_fork_block_exec_ctx());
  grpc_fork_dec_exec_ctx_count();
  thd.Start();
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(1, GPR_TIMESPAN)));
  GPR_ASSERT(!exec_ctx_created);
  grpc_fork_allow_exec_ctx();
  thd.Join();  // This ensure that the call got un-blocked
  grpc_fork_support_destroy();
}

int main(int argc, char* argv[]) {
  grpc_test_init(argc, argv);
  test_init();
  test_thd_count();
  test_exec_count();

  return 0;
}
