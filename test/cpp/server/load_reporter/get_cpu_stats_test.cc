//
//
// Copyright 2018 gRPC authors.
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

#include "src/cpp/server/load_reporter/get_cpu_stats.h"

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {
namespace {

TEST(GetCpuStatsTest, ReadOnce) { grpc::load_reporter::GetCpuStatsImpl(); }

TEST(GetCpuStatsTest, BusyNoLargerThanTotal) {
  auto p = grpc::load_reporter::GetCpuStatsImpl();
  uint64_t busy = p.first;
  uint64_t total = p.second;
  ASSERT_LE(busy, total) << "Busy time " << busy
                         << " is larger than total time " << total;
}

TEST(GetCpuStatsTest, Ascending) {
  /*
      May 2026 Notes : This test flaked. Since the flake is extremely rare, not
      fixing it right now. The fix may create more problems than it solves.
      Failed 2 times in 30 days.
      Adding notes here for future reference.
      Platform: Test flakes on Windows only.
      File : src/cpp/server/load_reporter/get_cpu_stats_windows.cc
      Failure : Busy time decreased at run 82: 48600468750 -> 48600312500
      Geminis Notes :
      GetSystemTimes does not capture kernel and idle times atomically.
      In Docker containers, virtualized CPU scheduling causes timer skew.
      Updated before means idle receives the new clock tick first.
      During an idle tick, both idle and kernel must increase.
      When idle increments first, its value in memory advances immediately.
      At that exact moment, kernel has not yet been incremented.
      Therefore, idle is temporarily larger (inflated) relative to kernel.
      Busy time in GetCpuStatsImpl is (kernel + user - idle).
      Subtracting the advanced idle from the lagging kernel reduces busy time.
      If kernel updated first, idle would be lagging (deflated).
      A deflated idle would cause busy time to increase, not decrease.
      The test log proves busy time decreased by exactly one tick.
      Thus, idle was updated first and was temporarily inflated.
  */
  const size_t kRuns = 100;
  auto prev = grpc::load_reporter::GetCpuStatsImpl();
  for (size_t i = 0; i < kRuns; ++i) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                 gpr_time_from_millis(1, GPR_TIMESPAN)));
    auto cur = grpc::load_reporter::GetCpuStatsImpl();
    ASSERT_LE(prev.first, cur.first)
        << "Busy time decreased at run " << i << ": " << prev.first << " -> "
        << cur.first;
    ASSERT_LE(prev.second, cur.second)
        << "Total time decreased at run " << i << ": " << prev.second << " -> "
        << cur.second;
    prev = cur;
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
