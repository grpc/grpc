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

#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/timer.h"
#include "test/core/util/test_config.h"

static gpr_mu g_mu;
extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);
gpr_timespec (*gpr_now_impl_orig)(gpr_clock_type clock_type) = gpr_now_impl;
static int g_time_shift_sec = 0;
static int g_time_shift_nsec = 0;
static gpr_timespec now_impl(gpr_clock_type clock) {
  auto ts = gpr_now_impl_orig(clock);
  // We only manipulate the realtime clock to simulate changes in wall-clock
  // time
  if (clock != GPR_CLOCK_REALTIME) {
    return ts;
  }
  GPR_ASSERT(ts.tv_nsec >= 0);
  GPR_ASSERT(ts.tv_nsec < GPR_NS_PER_SEC);
  gpr_mu_lock(&g_mu);
  ts.tv_sec += g_time_shift_sec;
  ts.tv_nsec += g_time_shift_nsec;
  gpr_mu_unlock(&g_mu);
  if (ts.tv_nsec >= GPR_NS_PER_SEC) {
    ts.tv_nsec -= GPR_NS_PER_SEC;
    ++ts.tv_sec;
  } else if (ts.tv_nsec < 0) {
    --ts.tv_sec;
    ts.tv_nsec = GPR_NS_PER_SEC + ts.tv_nsec;
  }
  return ts;
}

// offset the value returned by gpr_now(GPR_CLOCK_REALTIME) by msecs
// milliseconds
static void set_now_offset(int msecs) {
  gpr_mu_lock(&g_mu);
  g_time_shift_sec = msecs / 1000;
  g_time_shift_nsec = (msecs % 1000) * 1e6;
  gpr_mu_unlock(&g_mu);
}

// restore the original implementation of gpr_now()
static void reset_now_offset() {
  gpr_mu_lock(&g_mu);
  g_time_shift_sec = 0;
  g_time_shift_nsec = 0;
  gpr_mu_unlock(&g_mu);
}


namespace grpc {
namespace {

TEST(TimerTest, TimerExpiry) {
  grpc_init();
  grpc_core::ExecCtx exec_ctx;
  grpc_timer timer;
  grpc_timer_init(&timer, 1500,
                  GRPC_CLOSURE_CREATE([](void*, grpc_error*) { gpr_log(GPR_ERROR, "Timer fired");}, nullptr, grpc_schedule_on_exec_ctx));
  gpr_log(GPR_ERROR, "sleeping for 5 seconds");
  sleep(5);
  gpr_log(GPR_ERROR, "sleep done");
  grpc_shutdown();
}

TEST(TimerTest, TimeJumpsBackwards) {
  grpc_init();
  grpc_core::ExecCtx exec_ctx;
  gpr_log(GPR_ERROR, "sleeping for 5 seconds");
  set_now_offset(-3000);
  sleep(5);
  gpr_log(GPR_ERROR, "sleep done");
  reset_now_offset();
  grpc_shutdown();
}

TEST(TimerTest, TimeJumpsForward) {
  grpc_init();
  grpc_core::ExecCtx exec_ctx;
  gpr_log(GPR_ERROR, "sleeping for 5 seconds");
  set_now_offset(3000);
  sleep(5);
  gpr_log(GPR_ERROR, "sleep done");
  reset_now_offset();
  grpc_shutdown();
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  gpr_mu_init(&g_mu);
  gpr_now_impl = now_impl;
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
