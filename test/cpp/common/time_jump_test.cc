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

#include <grpc/grpc.h>
#include <spawn.h>

#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/util/crash.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/test_config.h"

extern char** environ;

#ifdef GPR_ANDROID
// Android doesn't have posix_spawn. Use std::system instead
void run_cmd(const char* cmd) { std::system(cmd); }
#else
void run_cmd(const char* cmd) {
  pid_t pid;
  const char* argv[] = {const_cast<const char*>("sh"),
                        const_cast<const char*>("-c"), cmd, nullptr};
  int status;

  status = posix_spawn(&pid, const_cast<const char*>("/bin/sh"), nullptr,
                       nullptr, const_cast<char**>(argv), environ);
  if (status == 0) {
    if (waitpid(pid, &status, 0) == -1) {
      perror("waitpid");
    }
  }
}
#endif

class TimeJumpTest : public ::testing::TestWithParam<std::string> {
 protected:
  void SetUp() override {
    // Skip test if slowdown factor > 1
    if (grpc_test_slowdown_factor() != 1) {
      GTEST_SKIP();
    } else {
      grpc_init();
    }
  }
  void TearDown() override {
    // Skip test if slowdown factor > 1
    if (grpc_test_slowdown_factor() == 1) {
      run_cmd("sudo sntp -sS pool.ntp.org");
      grpc_shutdown();
    }
  }

  const int kWaitTimeMs = 1500;
};

std::vector<std::string> CreateTestScenarios() {
  return {"-1M", "+1M", "-1H", "+1H", "-1d", "+1d", "-1y", "+1y"};
}
INSTANTIATE_TEST_SUITE_P(TimeJump, TimeJumpTest,
                         ::testing::ValuesIn(CreateTestScenarios()));

TEST_P(TimeJumpTest, TimerRunning) {
  grpc_core::ExecCtx exec_ctx;
  grpc_timer timer;
  grpc_timer_init(&timer,
                  grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(3),
                  GRPC_CLOSURE_CREATE(
                      [](void*, grpc_error_handle error) {
                        CHECK(error == absl::CancelledError());
                      },
                      nullptr, grpc_schedule_on_exec_ctx));
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  std::ostringstream cmd;
  cmd << "sudo date `date -v" << GetParam() << " \"+%m%d%H%M%y\"`";
  run_cmd(cmd.str().c_str());
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(kWaitTimeMs));
  // We expect 1 wakeup/sec when there are not timer expiries
  int64_t wakeups = grpc_timer_manager_get_wakeups_testonly();
  VLOG(2) << "wakeups: " << wakeups;
  CHECK_LE(wakeups, 3);
  grpc_timer_cancel(&timer);
}

TEST_P(TimeJumpTest, TimedWait) {
  grpc_core::CondVar cond;
  grpc_core::Mutex mu;
  {
    grpc_core::MutexLock lock(&mu);
    std::thread thd = std::thread([]() {
      gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
      std::ostringstream cmd;
      cmd << "sudo date `date -v" << GetParam() << " \"+%m%d%H%M%y\"`";
      run_cmd(cmd.str().c_str());
    });
    gpr_timespec before = gpr_now(GPR_CLOCK_MONOTONIC);
    bool timedout = cond.WaitWithTimeout(&mu, absl::Milliseconds(kWaitTimeMs));
    gpr_timespec after = gpr_now(GPR_CLOCK_MONOTONIC);
    int32_t elapsed_ms = gpr_time_to_millis(gpr_time_sub(after, before));
    VLOG(2) << "After wait, timedout = " << timedout
            << " elapsed_ms = " << elapsed_ms;
    CHECK_EQ(timedout, 1);
    CHECK(1 == gpr_time_similar(gpr_time_sub(after, before),
                                gpr_time_from_millis(kWaitTimeMs, GPR_TIMESPAN),
                                gpr_time_from_millis(50, GPR_TIMESPAN)));

    thd.join();
  }
  // We expect 1 wakeup/sec when there are not timer expiries
  int64_t wakeups = grpc_timer_manager_get_wakeups_testonly();
  VLOG(2) << "wakeups: " << wakeups;
  CHECK_LE(wakeups, 3);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
