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

#include "gtest/gtest.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {
namespace {

TEST(GetCpuStatsTest, ReadOnce) { grpc::load_reporter::GetCpuStatsImpl(); }

TEST(GetCpuStatsTest, BusyNoLargerThanTotal) {
  auto p = grpc::load_reporter::GetCpuStatsImpl();
  uint64_t busy = p.first;
  uint64_t total = p.second;
  ASSERT_LE(busy, total);
}

TEST(GetCpuStatsTest, Ascending) {
  const size_t kRuns = 100;
  auto prev = grpc::load_reporter::GetCpuStatsImpl();
  for (size_t i = 0; i < kRuns; ++i) {
    auto cur = grpc::load_reporter::GetCpuStatsImpl();
    ASSERT_LE(prev.first, cur.first);
    ASSERT_LE(prev.second, cur.second);
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
