// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/event_engine/thread_pool.h"

#include <stdlib.h>

#include <chrono>
#include <thread>

#include "gtest/gtest.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/notification.h"

namespace grpc_event_engine {
namespace experimental {

TEST(ThreadPoolTest, CanRunClosure) {
  ThreadPool p;
  grpc_core::Notification n;
  p.Run([&n] { n.Notify(); });
  n.WaitForNotification();
  p.Quiesce();
}

TEST(ThreadPoolTest, CanDestroyInsideClosure) {
  auto p = std::make_shared<ThreadPool>();
  grpc_core::Notification n;
  p->Run([p, &n]() mutable {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // This should delete the thread pool and not deadlock
    p->Quiesce();
    p.reset();
    n.Notify();
  });
  // Make sure we're not keeping the thread pool alive from outside the loop
  p.reset();
  n.WaitForNotification();
}

TEST(ThreadPoolTest, CanSurviveFork) {
  ThreadPool p;
  grpc_core::Notification n;
  gpr_log(GPR_INFO, "run callback 1");
  p.Run([&n, &p] {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    gpr_log(GPR_INFO, "run callback 2");
    p.Run([&n] {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      gpr_log(GPR_INFO, "notify");
      n.Notify();
    });
  });
  gpr_log(GPR_INFO, "prepare fork");
  p.PrepareFork();
  gpr_log(GPR_INFO, "postfork child");
  p.PostforkChild();
  n.WaitForNotification();
  grpc_core::Notification n2;
  gpr_log(GPR_INFO, "run callback 3");
  p.Run([&n2] {
    gpr_log(GPR_INFO, "notify");
    n2.Notify();
  });
  gpr_log(GPR_INFO, "wait for notification");
  n2.WaitForNotification();
  p.Quiesce();
}

void ScheduleSelf(ThreadPool* p) {
  p->Run([p] { ScheduleSelf(p); });
}

// This can be re-enabled if/when the thread pool is changed to quiesce
// pre-fork. For now, it cannot get stuck because callback execution is
// effectively paused until after the post-fork reboot.
TEST(ThreadPoolDeathTest, DISABLED_CanDetectStucknessAtFork) {
  ASSERT_DEATH_IF_SUPPORTED(
      [] {
        gpr_set_log_verbosity(GPR_LOG_SEVERITY_ERROR);
        ThreadPool p;
        ScheduleSelf(&p);
        std::thread terminator([] {
          std::this_thread::sleep_for(std::chrono::seconds(10));
          abort();
        });
        p.PrepareFork();
      }(),
      "Waiting for thread pool to idle before forking");
}

void ScheduleTwiceUntilZero(ThreadPool* p, int n) {
  if (n == 0) return;
  p->Run([p, n] {
    ScheduleTwiceUntilZero(p, n - 1);
    ScheduleTwiceUntilZero(p, n - 1);
  });
}

TEST(ThreadPoolTest, CanStartLotsOfClosures) {
  ThreadPool p;
  // Our first thread pool implementation tried to create ~1M threads for this
  // test.
  ScheduleTwiceUntilZero(&p, 20);
  p.Quiesce();
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  gpr_log_verbosity_init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
