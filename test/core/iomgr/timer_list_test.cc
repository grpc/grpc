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

#include "src/core/lib/iomgr/port.h"

// This test only works with the generic timer implementation
#ifndef GRPC_CUSTOM_SOCKET

#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/timer.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "src/core/lib/debug/trace.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tracer_util.h"

#define MAX_CB 30

extern grpc_core::TraceFlag grpc_timer_trace;
extern grpc_core::TraceFlag grpc_timer_check_trace;

static int cb_called[MAX_CB][2];
static const int64_t kMillisIn25Days = 2160000000;
static const int64_t kHoursIn25Days = 600;

static void cb(void* arg, grpc_error_handle error) {
  cb_called[reinterpret_cast<intptr_t>(arg)][error == GRPC_ERROR_NONE]++;
}

static void add_test(void) {
  int i;
  grpc_timer timers[20];
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "add_test");

  grpc_timer_list_init();
  grpc_core::testing::grpc_tracer_enable_flag(&grpc_timer_trace);
  grpc_core::testing::grpc_tracer_enable_flag(&grpc_timer_check_trace);
  memset(cb_called, 0, sizeof(cb_called));

  grpc_millis start = grpc_core::ExecCtx::Get()->Now();

  /* 10 ms timers.  will expire in the current epoch */
  for (i = 0; i < 10; i++) {
    grpc_timer_init(
        &timers[i], start + 10,
        GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)i, grpc_schedule_on_exec_ctx));
  }

  /* 1010 ms timers.  will expire in the next epoch */
  for (i = 10; i < 20; i++) {
    grpc_timer_init(
        &timers[i], start + 1010,
        GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)i, grpc_schedule_on_exec_ctx));
  }

  /* collect timers.  Only the first batch should be ready. */
  grpc_core::ExecCtx::Get()->TestOnlySetNow(start + 500);
  GPR_ASSERT(grpc_timer_check(nullptr) == GRPC_TIMERS_FIRED);
  grpc_core::ExecCtx::Get()->Flush();
  for (i = 0; i < 20; i++) {
    GPR_ASSERT(cb_called[i][1] == (i < 10));
    GPR_ASSERT(cb_called[i][0] == 0);
  }

  grpc_core::ExecCtx::Get()->TestOnlySetNow(start + 600);
  GPR_ASSERT(grpc_timer_check(nullptr) == GRPC_TIMERS_CHECKED_AND_EMPTY);
  grpc_core::ExecCtx::Get()->Flush();
  for (i = 0; i < 30; i++) {
    GPR_ASSERT(cb_called[i][1] == (i < 10));
    GPR_ASSERT(cb_called[i][0] == 0);
  }

  /* collect the rest of the timers */
  grpc_core::ExecCtx::Get()->TestOnlySetNow(start + 1500);
  GPR_ASSERT(grpc_timer_check(nullptr) == GRPC_TIMERS_FIRED);
  grpc_core::ExecCtx::Get()->Flush();
  for (i = 0; i < 30; i++) {
    GPR_ASSERT(cb_called[i][1] == (i < 20));
    GPR_ASSERT(cb_called[i][0] == 0);
  }

  grpc_core::ExecCtx::Get()->TestOnlySetNow(start + 1600);
  GPR_ASSERT(grpc_timer_check(nullptr) == GRPC_TIMERS_CHECKED_AND_EMPTY);
  for (i = 0; i < 30; i++) {
    GPR_ASSERT(cb_called[i][1] == (i < 20));
    GPR_ASSERT(cb_called[i][0] == 0);
  }

  grpc_timer_list_shutdown();
}

/* Cleaning up a list with pending timers. */
void destruction_test(void) {
  grpc_timer timers[5];
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "destruction_test");

  grpc_core::ExecCtx::Get()->TestOnlySetNow(0);
  grpc_timer_list_init();
  grpc_core::testing::grpc_tracer_enable_flag(&grpc_timer_trace);
  grpc_core::testing::grpc_tracer_enable_flag(&grpc_timer_check_trace);
  memset(cb_called, 0, sizeof(cb_called));

  grpc_timer_init(
      &timers[0], 100,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)0, grpc_schedule_on_exec_ctx));
  grpc_timer_init(
      &timers[1], 3,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)1, grpc_schedule_on_exec_ctx));
  grpc_timer_init(
      &timers[2], 100,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)2, grpc_schedule_on_exec_ctx));
  grpc_timer_init(
      &timers[3], 3,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)3, grpc_schedule_on_exec_ctx));
  grpc_timer_init(
      &timers[4], 1,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)4, grpc_schedule_on_exec_ctx));
  grpc_core::ExecCtx::Get()->TestOnlySetNow(2);
  GPR_ASSERT(grpc_timer_check(nullptr) == GRPC_TIMERS_FIRED);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(1 == cb_called[4][1]);
  grpc_timer_cancel(&timers[0]);
  grpc_timer_cancel(&timers[3]);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(1 == cb_called[0][0]);
  GPR_ASSERT(1 == cb_called[3][0]);

  grpc_timer_list_shutdown();
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(1 == cb_called[1][0]);
  GPR_ASSERT(1 == cb_called[2][0]);
}

/* Cleans up a list with pending timers that simulate long-running-services.
   This test does the following:
    1) Simulates grpc server start time to 25 days in the past (completed in
        `main` using TestOnlyGlobalInit())
    2) Creates 4 timers - one with a deadline 25 days in the future, one just
        3 milliseconds in future, one way out in the future, and one using the
        grpc_timespec_to_millis_round_up function to compute a deadline of 25
        days in the future
    3) Simulates 4 milliseconds of elapsed time by changing `now` (cached at
        step 1) to `now+4`
    4) Shuts down the timer list
   https://github.com/grpc/grpc/issues/15904 */
void long_running_service_cleanup_test(void) {
  grpc_timer timers[4];
  grpc_core::ExecCtx exec_ctx;

  gpr_log(GPR_INFO, "long_running_service_cleanup_test");

  grpc_millis now = grpc_core::ExecCtx::Get()->Now();
  GPR_ASSERT(now >= kMillisIn25Days);
  grpc_timer_list_init();
  grpc_core::testing::grpc_tracer_enable_flag(&grpc_timer_trace);
  grpc_core::testing::grpc_tracer_enable_flag(&grpc_timer_check_trace);
  memset(cb_called, 0, sizeof(cb_called));

  grpc_timer_init(
      &timers[0], now + kMillisIn25Days,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)0, grpc_schedule_on_exec_ctx));
  grpc_timer_init(
      &timers[1], now + 3,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)1, grpc_schedule_on_exec_ctx));
  grpc_timer_init(
      &timers[2], GRPC_MILLIS_INF_FUTURE - 1,
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)2, grpc_schedule_on_exec_ctx));

  gpr_timespec deadline_spec = grpc_millis_to_timespec(
      now + kMillisIn25Days, gpr_clock_type::GPR_CLOCK_MONOTONIC);

  /* grpc_timespec_to_millis_round_up is how users usually compute a millisecond
    input value into grpc_timer_init, so we mimic that behavior here */
  grpc_timer_init(
      &timers[3], grpc_timespec_to_millis_round_up(deadline_spec),
      GRPC_CLOSURE_CREATE(cb, (void*)(intptr_t)3, grpc_schedule_on_exec_ctx));

  grpc_core::ExecCtx::Get()->TestOnlySetNow(now + 4);
  GPR_ASSERT(grpc_timer_check(nullptr) == GRPC_TIMERS_FIRED);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(0 == cb_called[0][0]);  // Timer 0 not called
  GPR_ASSERT(0 == cb_called[0][1]);
  GPR_ASSERT(0 == cb_called[1][0]);
  GPR_ASSERT(1 == cb_called[1][1]);  // Timer 1 fired
  GPR_ASSERT(0 == cb_called[2][0]);  // Timer 2 not called
  GPR_ASSERT(0 == cb_called[2][1]);
  GPR_ASSERT(0 == cb_called[3][0]);  // Timer 3 not called
  GPR_ASSERT(0 == cb_called[3][1]);

  grpc_timer_list_shutdown();
  grpc_core::ExecCtx::Get()->Flush();
  /* Timers 0, 2, and 3 were fired with an error during cleanup */
  GPR_ASSERT(1 == cb_called[0][0]);
  GPR_ASSERT(0 == cb_called[1][0]);
  GPR_ASSERT(1 == cb_called[2][0]);
  GPR_ASSERT(1 == cb_called[3][0]);
}

int main(int argc, char** argv) {
  /* Tests with default g_start_time */
  {
    grpc::testing::TestEnvironment env(argc, argv);
    grpc_core::ExecCtx::GlobalInit();
    grpc_core::ExecCtx exec_ctx;
    grpc_determine_iomgr_platform();
    grpc_iomgr_platform_init();
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
    add_test();
    destruction_test();
    grpc_iomgr_platform_shutdown();
  }
  grpc_core::ExecCtx::GlobalShutdown();

  /* Begin long running service tests */
  {
    grpc::testing::TestEnvironment env(argc, argv);
    /* Set g_start_time back 25 days. */
    /* We set g_start_time here in case there are any initialization
        dependencies that use g_start_time. */
    gpr_timespec new_start =
        gpr_time_sub(gpr_now(gpr_clock_type::GPR_CLOCK_MONOTONIC),
                     gpr_time_from_hours(kHoursIn25Days,
                                         gpr_clock_type::GPR_CLOCK_MONOTONIC));
    grpc_core::ExecCtx::TestOnlyGlobalInit(new_start);
    grpc_core::ExecCtx exec_ctx;
    grpc_determine_iomgr_platform();
    grpc_iomgr_platform_init();
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
    long_running_service_cleanup_test();
    add_test();
    destruction_test();
    grpc_iomgr_platform_shutdown();
  }
  grpc_core::ExecCtx::GlobalShutdown();

  return 0;
}

#else /* GRPC_CUSTOM_SOCKET */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_CUSTOM_SOCKET */
