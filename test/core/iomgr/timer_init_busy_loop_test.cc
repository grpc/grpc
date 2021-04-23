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

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "src/core/lib/iomgr/timer.h"

#include "test/core/util/test_config.h"


namespace {

class WorkSerializedTimerLoop {
 public:
  WorkSerializedTimerLoop() {
    GRPC_CLOSURE_INIT(&on_schedule_timer_, OnScheduleTimer, this, grpc_schedule_on_exec_ctx);
  }

  void Start() {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, &on_schedule_timer_, GRPC_ERROR_NONE);
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
    self->work_serializer_->Run([self, error] {
      grpc_core::MutexLock lock(&self->mu_);
      if (error != GRPC_ERROR_NONE || self->shutdown_) {
        GRPC_ERROR_UNREF(error);
        gpr_log(GPR_INFO, "test timer loop quitting, shutdown_:%d, error: %s", self->shutdown_, grpc_error_string(error));
        delete self;
        return;
      }
      // Note that we explicitly do not call InvalidateNow(). This test is meant to
      // display and example of real scenarios in the code base that do not call InvalidateNow
      // when scheduling periodic timer events.
      grpc_millis timeout = grpc_core::ExecCtx::Get()->Now() + 500;
      grpc_timer_init(&self->schedule_timer_alarm_, timeout, &self->on_schedule_timer_);
      gpr_log(GPR_INFO, "just scheduled a timer to go off in 500 ms. ExecCtx::Get()->Now(): %ld", grpc_core::ExecCtx::Get()->Now());
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_millis(100, GPR_TIMESPAN)));
      gpr_log(GPR_INFO, "just woke up from 1 ms sleep");
      GRPC_ERROR_UNREF(error);
    }, DEBUG_LOCATION);
    gpr_log(GPR_INFO, "just appended timer_init to work serializer");
  }

  std::shared_ptr<grpc_core::WorkSerializer> work_serializer_ = std::make_shared<grpc_core::WorkSerializer>();
  grpc_closure on_schedule_timer_;
  grpc_timer schedule_timer_alarm_;
  grpc_core::Mutex mu_;
  bool shutdown_ = false;
};

TEST(TimerInitBusyLoopTest, TestBusyLoopDoesNotHappenWithPeriodicTimerEventsOnAWorkSerializer) {
  WorkSerializedTimerLoop* work_serialized_timer_loop = new WorkSerializedTimerLoop();
  {
    grpc_core::ExecCtx exec_ctx;
    gpr_log(GPR_INFO, "begin test after sleeping for 1 second, ExecCtd::Get()->Now(): %ld", grpc_core::ExecCtx::Get()->Now());
    // Add some initial delay to try cause skew between this thread's view of the
    // time and the timer manager thread's view of the time (which can happen with
    // if the time is cached in the ExecCtx).
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_seconds(1, GPR_TIMESPAN)));
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
