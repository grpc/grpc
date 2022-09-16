/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/debug/stats.h"

#include <mutex>
#include <queue>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {

class Snapshot {
 public:
  Snapshot() { grpc_stats_collect(&begin_); }

  grpc_stats_data delta() {
    grpc_stats_data now;
    grpc_stats_collect(&now);
    grpc_stats_data delta;
    grpc_stats_diff(&now, &begin_, &delta);
    return delta;
  }

 private:
  grpc_stats_data begin_;
};

TEST(StatsTest, IncCounters) {
  for (int i = 0; i < GRPC_STATS_COUNTER_COUNT; i++) {
    std::unique_ptr<Snapshot> snapshot(new Snapshot);

    grpc_core::ExecCtx exec_ctx;
    GRPC_STATS_INC_COUNTER((grpc_stats_counters)i);

    EXPECT_EQ(snapshot->delta().counters[i], 1);
  }
}

TEST(StatsTest, IncSpecificCounter) {
  std::unique_ptr<Snapshot> snapshot(new Snapshot);

  grpc_core::ExecCtx exec_ctx;
  GRPC_STATS_INC_CLIENT_CALLS_CREATED();

  EXPECT_EQ(snapshot->delta().counters[GRPC_STATS_COUNTER_CLIENT_CALLS_CREATED],
            1);
}

static int FindExpectedBucket(int i, int j) {
  if (j < 0) {
    return 0;
  }
  if (j >= grpc_stats_histo_bucket_boundaries[i][grpc_stats_histo_buckets[i]]) {
    return grpc_stats_histo_buckets[i] - 1;
  }
  return std::upper_bound(grpc_stats_histo_bucket_boundaries[i],
                          grpc_stats_histo_bucket_boundaries[i] +
                              grpc_stats_histo_buckets[i],
                          j) -
         grpc_stats_histo_bucket_boundaries[i] - 1;
}

class HistogramTest : public ::testing::TestWithParam<int> {};

TEST_P(HistogramTest, CheckBucket) {
  const int kHistogram = GetParam();
  int max_bucket_boundary =
      grpc_stats_histo_bucket_boundaries[kHistogram]
                                        [grpc_stats_histo_buckets[kHistogram] -
                                         1];
  for (int i = -1000; i < max_bucket_boundary + 1000; i++) {
    ASSERT_EQ(FindExpectedBucket(kHistogram, i),
              grpc_stats_get_bucket[kHistogram](i))
        << "i=" << i << " expect_bucket="
        << grpc_stats_histo_bucket_boundaries[kHistogram]
                                             [FindExpectedBucket(kHistogram, i)]
        << " actual_bucket="
        << grpc_stats_histo_bucket_boundaries[kHistogram]
                                             [grpc_stats_get_bucket[kHistogram](
                                                 i)];
  }
}

TEST_P(HistogramTest, IncHistogram) {
  const int kHistogram = GetParam();
  std::queue<std::thread> threads;
  auto run = [kHistogram](const std::vector<int>& test_values,
                          int expected_bucket) {
    grpc_core::ExecCtx exec_ctx;
    for (auto j : test_values) {
      std::unique_ptr<Snapshot> snapshot(new Snapshot);

      grpc_stats_inc_histogram_value(kHistogram, j);

      auto delta = snapshot->delta();

      EXPECT_EQ(
          delta
              .histograms[grpc_stats_histo_start[kHistogram] + expected_bucket],
          1)
          << "\nhistogram:" << kHistogram
          << "\nexpected_bucket:" << expected_bucket << "\nj:" << j;
    }
  };
  // largest bucket boundary for current histogram type.
  int max_bucket_boundary =
      grpc_stats_histo_bucket_boundaries[kHistogram]
                                        [grpc_stats_histo_buckets[kHistogram] -
                                         1];
  std::map<int /* expected_bucket */, std::vector<int> /* test_values */>
      test_values_by_expected_bucket;
  std::random_device rd;
  std::uniform_int_distribution<int> dist(-1000, max_bucket_boundary + 1000);
  for (int i = 0; i < 100; i++) {
    int j = dist(rd);
    int expected_bucket = FindExpectedBucket(kHistogram, j);
    test_values_by_expected_bucket[expected_bucket].push_back(j);
  }
  for (auto& p : test_values_by_expected_bucket) {
    while (threads.size() >= 10) {
      threads.front().join();
      threads.pop();
    }
    threads.emplace(
        [test_values = std::move(p.second), run,
         cur_bucket = p.first]() mutable { run(test_values, cur_bucket); });
  }
  while (!threads.empty()) {
    threads.front().join();
    threads.pop();
  }
}

INSTANTIATE_TEST_SUITE_P(HistogramTestCases, HistogramTest,
                         ::testing::Range<int>(0, GRPC_STATS_HISTOGRAM_COUNT));

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
