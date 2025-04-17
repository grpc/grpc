//
//
// Copyright 2019 gRPC authors.
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

#include "src/core/lib/iomgr/timer.h"

#include <grpc/grpc.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/util/crash.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"

#ifdef GRPC_POSIX_SOCKET_EV
#include "src/core/lib/iomgr/ev_posix.h"
#endif

// MAYBE_SKIP_TEST is a macro to determine if this particular test configuration
// should be skipped based on a decision made at SetUp time.
#define MAYBE_SKIP_TEST \
  do {                  \
    if (do_not_test_) { \
      return;           \
    }                   \
  } while (0)

class TimerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    // Skip test if slowdown factor > 1, or we are
    // using event manager.
#ifdef GRPC_POSIX_SOCKET_EV
    if (grpc_test_slowdown_factor() != 1 ||
        grpc_event_engine_run_in_background()) {
#else
    if (grpc_test_slowdown_factor() != 1) {
#endif
      do_not_test_ = true;
    }
  }

  void TearDown() override { grpc_shutdown(); }

  bool do_not_test_{false};
};

#ifndef GPR_WINDOWS
// the test fails with too many wakeups on windows opt build
// the mechanism by which that happens is described in
// https://github.com/grpc/grpc/issues/20436
TEST_F(TimerTest, NoTimers) {
  MAYBE_SKIP_TEST;
  grpc_core::ExecCtx exec_ctx;
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1500));

  // We expect to get 1 wakeup per second. Sometimes we also get a wakeup
  // during initialization, so in 1.5 seconds we expect to get 1 or 2 wakeups.
  int64_t wakeups = grpc_timer_manager_get_wakeups_testonly();
  CHECK(wakeups == 1 || wakeups == 2);
}
#endif

TEST_F(TimerTest, OneTimerExpires) {
  MAYBE_SKIP_TEST;
  grpc_core::ExecCtx exec_ctx;
  grpc_timer timer;
  int timer_fired = 0;
  grpc_timer_init(
      &timer,
      grpc_core::Timestamp::Now() + grpc_core::Duration::Milliseconds(500),
      GRPC_CLOSURE_CREATE(
          [](void* arg, grpc_error_handle) {
            int* timer_fired = static_cast<int*>(arg);
            ++*timer_fired;
          },
          &timer_fired, grpc_schedule_on_exec_ctx));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1500));
  CHECK_EQ(timer_fired, 1);

  // We expect to get 1 wakeup/second + 1 wakeup for the expired timer + maybe 1
  // wakeup during initialization. i.e. in 1.5 seconds we expect 2 or 3 wakeups.
  // Actual number of wakeups is more due to bug
  // https://github.com/grpc/grpc/issues/19947
  int64_t wakeups = grpc_timer_manager_get_wakeups_testonly();
  VLOG(2) << "wakeups: " << wakeups;
}

TEST_F(TimerTest, MultipleTimersExpire) {
  MAYBE_SKIP_TEST;
  grpc_core::ExecCtx exec_ctx;
  const int kNumTimers = 10;
  grpc_timer timers[kNumTimers];
  int timer_fired = 0;
  for (int i = 0; i < kNumTimers; ++i) {
    grpc_timer_init(&timers[i],
                    grpc_core::Timestamp::Now() +
                        grpc_core::Duration::Milliseconds(500) +
                        grpc_core::Duration::Milliseconds(i),
                    GRPC_CLOSURE_CREATE(
                        [](void* arg, grpc_error_handle) {
                          int* timer_fired = static_cast<int*>(arg);
                          ++*timer_fired;
                        },
                        &timer_fired, grpc_schedule_on_exec_ctx));
  }

  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1500));
  CHECK(kNumTimers == timer_fired);

  // We expect to get 1 wakeup/second + 1 wakeup for per timer fired + maybe 1
  // wakeup during initialization. i.e. in 1.5 seconds we expect 11 or 12
  // wakeups. Actual number of wakeups is more due to bug
  // https://github.com/grpc/grpc/issues/19947
  int64_t wakeups = grpc_timer_manager_get_wakeups_testonly();
  VLOG(2) << "wakeups: " << wakeups;
}

TEST_F(TimerTest, CancelSomeTimers) {
  MAYBE_SKIP_TEST;
  grpc_core::ExecCtx exec_ctx;
  const int kNumTimers = 10;
  grpc_timer timers[kNumTimers];
  std::atomic<int> timer_fired{0};
  grpc_core::ExecCtx::Get()->InvalidateNow();
  for (int i = 0; i < kNumTimers; ++i) {
    // Set a large firing time for timers which are bound to be cancelled
    // and set a small firing time for timers which need to execute.
    grpc_timer_init(
        &timers[i],
        grpc_core::Timestamp::Now() +
            ((i < kNumTimers / 2) ? grpc_core ::Duration::Milliseconds(60000)
                                  : grpc_core ::Duration::Milliseconds(100) +
                                        grpc_core::Duration::Milliseconds(i)),
        GRPC_CLOSURE_CREATE(
            [](void* arg, grpc_error_handle error) {
              if (error == absl::CancelledError()) {
                return;
              }
              std::atomic<int>* timer_fired =
                  static_cast<std::atomic<int>*>(arg);
              ++*timer_fired;
            },
            &timer_fired, grpc_schedule_on_exec_ctx));
  }
  for (int i = 0; i < kNumTimers / 2; ++i) {
    grpc_timer_cancel(&timers[i]);
  }

  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1500));
  CHECK(kNumTimers / 2 == timer_fired);

  // We expect to get 1 wakeup/second + 1 wakeup per timer fired + maybe 1
  // wakeup during initialization. i.e. in 1.5 seconds we expect 6 or 7 wakeups.
  // Actual number of wakeups is more due to bug
  // https://github.com/grpc/grpc/issues/19947
  int64_t wakeups = grpc_timer_manager_get_wakeups_testonly();
  VLOG(2) << "wakeups: " << wakeups;
}

// Enable the following test after
// https://github.com/grpc/grpc/issues/20049 has been fixed.
TEST_F(TimerTest, DISABLED_TimerNotCanceled) {
  grpc_core::ExecCtx exec_ctx;
  grpc_timer timer;
  grpc_timer_init(
      &timer, grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(10),
      GRPC_CLOSURE_CREATE([](void*, grpc_error_handle) {}, nullptr,
                          grpc_schedule_on_exec_ctx));
}

// Enable the following test after
// https://github.com/grpc/grpc/issues/20064 has been fixed.
TEST_F(TimerTest, DISABLED_CancelRace) {
  MAYBE_SKIP_TEST;
  grpc_core::ExecCtx exec_ctx;
  const int kNumTimers = 10;
  grpc_timer timers[kNumTimers];
  for (int i = 0; i < kNumTimers; ++i) {
    grpc_timer* arg = (i != 0) ? &timers[i - 1] : nullptr;
    grpc_timer_init(
        &timers[i],
        grpc_core::Timestamp::Now() + grpc_core::Duration::Milliseconds(100),
        GRPC_CLOSURE_CREATE(
            [](void* arg, grpc_error_handle /*error*/) {
              grpc_timer* timer = static_cast<grpc_timer*>(arg);
              if (timer) {
                grpc_timer_cancel(timer);
              }
            },
            arg, grpc_schedule_on_exec_ctx));
  }
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
}

// Enable the following test after
// https://github.com/grpc/grpc/issues/20066 has been fixed.
TEST_F(TimerTest, DISABLED_CancelNextTimer) {
  MAYBE_SKIP_TEST;
  grpc_core::ExecCtx exec_ctx;
  const int kNumTimers = 10;
  grpc_timer timers[kNumTimers];

  for (int i = 0; i < kNumTimers; ++i) {
    grpc_timer_init_unset(&timers[i]);
  }

  for (int i = 0; i < kNumTimers; ++i) {
    grpc_timer* arg = nullptr;
    if (i < kNumTimers - 1) {
      arg = &timers[i + 1];
    }
    grpc_timer_init(
        &timers[i],
        grpc_core::Timestamp::Now() + grpc_core::Duration::Milliseconds(100),
        GRPC_CLOSURE_CREATE(
            [](void* arg, grpc_error_handle /*error*/) {
              grpc_timer* timer = static_cast<grpc_timer*>(arg);
              if (timer) {
                grpc_timer_cancel(timer);
              }
            },
            arg, grpc_schedule_on_exec_ctx));
  }
  grpc_timer_cancel(&timers[0]);
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
