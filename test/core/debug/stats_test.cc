//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/lib/debug/stats.h"

#include <algorithm>
#include <memory>

#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

class Snapshot {
 public:
  std::unique_ptr<GlobalStats> delta() {
    auto now = global_stats().Collect();
    return now->Diff(*begin_);
  }

 private:
  std::unique_ptr<GlobalStats> begin_ = global_stats().Collect();
};

TEST(StatsTest, IncSpecificCounter) {
  std::unique_ptr<Snapshot> snapshot(new Snapshot);

  ExecCtx exec_ctx;
  global_stats().IncrementClientCallsCreated();

  EXPECT_EQ(snapshot->delta()->client_calls_created, 1);
}

TEST(StatsTest, IncrementHttp2MetadataSize) {
  ExecCtx exec_ctx;
  global_stats().IncrementHttp2MetadataSize(0);
}

static int FindExpectedBucket(const HistogramView& h, int value) {
  if (value < 0) {
    return 0;
  }
  if (value >= h.bucket_boundaries[h.num_buckets]) {
    return h.num_buckets - 1;
  }
  return std::upper_bound(h.bucket_boundaries,
                          h.bucket_boundaries + h.num_buckets, value) -
         h.bucket_boundaries - 1;
}

class HistogramTest : public ::testing::TestWithParam<int> {};

TEST_P(HistogramTest, CheckBucket) {
  const GlobalStats::Histogram kHistogram =
      static_cast<GlobalStats::Histogram>(GetParam());
  auto some_stats = std::make_unique<GlobalStats>();
  auto view = some_stats->histogram(kHistogram);
  const int max_bucket_boundary = view.bucket_boundaries[view.num_buckets];
  for (int i = -1000; i < max_bucket_boundary + 1000; i++) {
    ASSERT_EQ(FindExpectedBucket(view, i), view.bucket_for(i))
        << "i=" << i << " expect_bucket="
        << view.bucket_boundaries[FindExpectedBucket(view, i)]
        << " actual_bucket=" << view.bucket_boundaries[view.bucket_for(i)];
  }
}

INSTANTIATE_TEST_SUITE_P(
    HistogramTestCases, HistogramTest,
    ::testing::Range<int>(0, static_cast<int>(GlobalStats::Histogram::COUNT)));

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
