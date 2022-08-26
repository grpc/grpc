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

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "absl/synchronization/notification.h"

#include <grpc/support/log.h>

namespace grpc_event_engine {
namespace experimental {

TEST(ThreadPoolTest, CanRunClosure) {
  ThreadPool p(1);
  absl::Notification n;
  p.Add([&n] { n.Notify(); });
  n.WaitForNotification();
}

TEST(ThreadPoolTest, CanDestroyInsideClosure) {
  auto p = std::make_shared<ThreadPool>(1);
  p->Add([p]() { std::this_thread::sleep_for(std::chrono::seconds(1)); });
}

TEST(ThreadPoolTest, CanSurviveFork) {
  ThreadPool p(1);
  absl::Notification n;
  gpr_log(GPR_INFO, "add callback 1");
  p.Add([&n, &p] {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    gpr_log(GPR_INFO, "add callback 2");
    p.Add([&n] {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      gpr_log(GPR_INFO, "notify");
      n.Notify();
    });
  });
  gpr_log(GPR_INFO, "prepare fork");
  p.PrepareFork();
  gpr_log(GPR_INFO, "wait for notification");
  n.WaitForNotification();
  gpr_log(GPR_INFO, "postfork child");
  p.PostforkChild();
  absl::Notification n2;
  gpr_log(GPR_INFO, "add callback 3");
  p.Add([&n2] {
    gpr_log(GPR_INFO, "notify");
    n2.Notify();
  });
  gpr_log(GPR_INFO, "wait for notification");
  n2.WaitForNotification();
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  gpr_log_verbosity_init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
