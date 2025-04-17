// Copyright 2021 gRPC authors.
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

#include "src/core/lib/resource_quota/periodic_update.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stddef.h>

#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {
namespace testing {

TEST(PeriodicUpdateTest, SimpleTest) {
  std::unique_ptr<PeriodicUpdate> upd;
  Timestamp start;
  Timestamp reset_start;
  // Create a periodic update that updates every second.
  {
    ExecCtx exec_ctx;
    upd = std::make_unique<PeriodicUpdate>(Duration::Seconds(1));
    start = Timestamp::Now();
  }
  // Wait until the first period has elapsed.
  bool done = false;
  while (!done) {
    ExecCtx exec_ctx;
    upd->Tick([&](Duration elapsed) {
      reset_start = Timestamp::Now();
      EXPECT_GE(elapsed, Duration::Seconds(1));
      done = true;
    });
  }
  // Ensure that took at least 1 second.
  {
    ExecCtx exec_ctx;
    EXPECT_GE(Timestamp::Now() - start, Duration::Seconds(1));
    start = reset_start;
  }
  // Do ten more update cycles
  for (int i = 0; i < 10; i++) {
    done = false;
    while (!done) {
      ExecCtx exec_ctx;
      upd->Tick([&](Duration) {
        reset_start = Timestamp::Now();
        EXPECT_GE(Timestamp::Now() - start, Duration::Seconds(1));
        done = true;
      });
    }
    // Ensure the time taken was between 1 and 3 seconds - we make a little
    // allowance for the presumed inaccuracy of this type.
    {
      ExecCtx exec_ctx;
      EXPECT_GE(Timestamp::Now() - start, Duration::Seconds(1));
      EXPECT_LE(Timestamp::Now() - start, Duration::Seconds(3));
      start = reset_start;
    }
  }
}

TEST(PeriodicUpdate, ThreadTest) {
  std::unique_ptr<PeriodicUpdate> upd;
  std::atomic<int> count(0);
  Timestamp start;
  // Create a periodic update that updates every second.
  {
    ExecCtx exec_ctx;
    upd = std::make_unique<PeriodicUpdate>(Duration::Seconds(1));
    start = Timestamp::Now();
  }
  // Run ten threads all updating the counter continuously, for a total of ten
  // update cycles.
  // This allows TSAN to catch threading issues.
  std::vector<std::thread> threads;
  for (size_t i = 0; i < 10; i++) {
    threads.push_back(std::thread([&]() {
      while (count.load() < 10) {
        ExecCtx exec_ctx;
        upd->Tick([&](Duration d) {
          EXPECT_GE(d, Duration::Seconds(1));
          count.fetch_add(1);
        });
      }
    }));
  }

  // Finish all threads.
  for (auto& th : threads) {
    th.join();
  }
  // Ensure our ten cycles took at least 10 seconds, and no more than 30.
  {
    ExecCtx exec_ctx;
    EXPECT_GE(Timestamp::Now() - start, Duration::Seconds(10));
    EXPECT_LE(Timestamp::Now() - start, Duration::Seconds(30));
  }
}

}  // namespace testing
}  // namespace grpc_core

// Hook needed to run ExecCtx outside of iomgr.
void grpc_set_default_iomgr_platform() {}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  gpr_log_verbosity_init();
  gpr_time_init();
  return RUN_ALL_TESTS();
}
