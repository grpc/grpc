/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <stdlib.h>
#include <string.h>

#include <functional>
#include <memory>

#include <gmock/gmock.h>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"

#include "test/core/util/test_config.h"

namespace {

gpr_timespec g_test_start_time;

class WorkSerializedTimerLoop {
 public:
  WorkSerializedTimerLoop() {
    GRPC_CLOSURE_INIT(&on_schedule_timer_, OnScheduleTimer, this,
                      grpc_schedule_on_exec_ctx);
  }

  void Start() {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &on_schedule_timer_,
                            GRPC_ERROR_NONE);
  }

  void Shutdown() {
    grpc_core::MutexLock lock(&mu_);
    shutdown_ = true;
    grpc_timer_cancel(&schedule_timer_alarm_);
  }

 private:
  static void OnScheduleTimer(void* arg, grpc_error* error) {
    WorkSerializedTimerLoop* self = static_cast<WorkSerializedTimerLoop*>(arg);
    GRPC_ERROR_REF(error);
    self->work_serializer_->Run(
        [self, error] {
          grpc_core::MutexLock lock(&self->mu_);
          // abort if this timer loop has been spinning for 5 minutes
          if (gpr_time_cmp(
                  gpr_now(GPR_CLOCK_MONOTONIC),
                  gpr_time_add(g_test_start_time,
                               gpr_time_from_seconds(30, GPR_TIMESPAN))) > 0) {
            gpr_log(GPR_ERROR,
                    "test timer loop has been spinning for more than 5 "
                    "minutes, this indicates a busy-loop bug");
            GPR_ASSERT(0);
          }
          if (error != GRPC_ERROR_NONE || self->shutdown_) {
            GRPC_ERROR_UNREF(error);
            gpr_log(GPR_INFO,
                    "test timer loop quitting, shutdown_:%d, error: %s",
                    self->shutdown_, grpc_error_string(error));
            delete self;
            return;
          }
          // Schedule a timer to go off in 500 milliseconds. Note that this is
          // explicitly less than the 1 second of delay that was inserted at the
          // beginning of the test. The goal is to try to repro a bug related to
          // time caching where such delay will cause the 500ms timer to fire
          // immediately.
          grpc_millis timeout = grpc_core::ExecCtx::Get()->Now() + 500;
          // Note that we explicitly do not call ExecCtx::InvalidateNow(). This
          // test is meant to display an example of real scenarios in the code
          // base that do not call InvalidateNow when scheduling such periodic
          // timer events.
          gpr_log(GPR_INFO,
                  "scheduled a timer to go off in 500 ms. "
                  "ExecCtx::Get()->Now(): %ld",
                  grpc_core::ExecCtx::Get()->Now());
          grpc_timer_init(&self->schedule_timer_alarm_, timeout,
                          &self->on_schedule_timer_);
          // Sleep for 1 ms to simulate some arbitrary delay that gives time for
          // the timer thread to invoke the timer callback we just scheduled,
          // pushing this lambda back onto the work serializer queue that we're
          // currently holding. If things are correct, then the 500 ms timer
          // just scheduled should very rarely fire before this sleep ends, and
          // thus the timer thread should very rarely have enough time to do
          // that.
          gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                       gpr_time_from_millis(1, GPR_TIMESPAN)));
          gpr_log(
              GPR_INFO,
              "thread calling grpc_timer_init just woke up from 1 ms sleep");
          GRPC_ERROR_UNREF(error);
        },
        DEBUG_LOCATION);
    gpr_log(GPR_INFO,
            "just appended timer-scheduling callback to work serializer");
  }

  std::shared_ptr<grpc_core::WorkSerializer> work_serializer_ =
      std::make_shared<grpc_core::WorkSerializer>();
  grpc_closure on_schedule_timer_;
  grpc_timer schedule_timer_alarm_;
  grpc_core::Mutex mu_;
  bool shutdown_ = false;
};

TEST(TimerInitBusyLoopTest,
     TestBusyLoopDoesNotHappenWithPeriodicTimerEventsOnAWorkSerializer) {
  WorkSerializedTimerLoop* work_serialized_timer_loop =
      new WorkSerializedTimerLoop();
  {
    grpc_core::ExecCtx exec_ctx;
    g_test_start_time = gpr_now(GPR_CLOCK_MONOTONIC);
    // Explicitly invoke ExecCtx::Now(), in order to try to repro a bug related
    // to caching of the ExecCtx::now_ field.
    grpc_millis now = grpc_core::ExecCtx::Get()->Now();
    gpr_log(GPR_INFO,
            "begin timer loop after sleeping for 1 second, "
            "ExecCtx::Get()->Now(): %ld",
            now);
    // Add some initial delay to try cause skew between this thread's view of
    // the time and the timer manager thread's view of the time (which can
    // happen with if the time is cached in the ExecCtx). This simulates
    // arbitrary delay in real code which can cause such skew.
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                 gpr_time_from_seconds(1, GPR_TIMESPAN)));
    work_serialized_timer_loop->Start();
  }
  {
    grpc_core::ExecCtx exec_ctx;
    work_serialized_timer_loop->Shutdown();
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
