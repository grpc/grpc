//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy/weighted_round_robin/static_stride_scheduler.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <benchmark/benchmark.h>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/no_destruct.h"

namespace grpc_core {
namespace {

using ::testing::AnyOf;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Le;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

TEST(StaticStrideSchedulerTest, EmptyWeightsIsNullopt) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {};
  ASSERT_FALSE(StaticStrideScheduler::Make(absl::MakeSpan(weights), [&] {
                 return sequence++;
               }).has_value());
}

TEST(StaticStrideSchedulerTest, OneZeroWeightIsNullopt) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {0};
  ASSERT_FALSE(StaticStrideScheduler::Make(absl::MakeSpan(weights), [&] {
                 return sequence++;
               }).has_value());
}

TEST(StaticStrideSchedulerTest, AllZeroWeightsIsNullopt) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {0, 0, 0, 0};
  ASSERT_FALSE(StaticStrideScheduler::Make(absl::MakeSpan(weights), [&] {
                 return sequence++;
               }).has_value());
}

TEST(StaticStrideSchedulerTest, OneWeightsIsNullopt) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {1};
  ASSERT_FALSE(StaticStrideScheduler::Make(absl::MakeSpan(weights), [&] {
                 return sequence++;
               }).has_value());
}

TEST(StaticStrideSchedulerTest, PicksAreWeightedExactly) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {1, 2, 3};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  std::vector<int> picks(weights.size());
  for (int i = 0; i < 6; ++i) {
    ++picks[scheduler->Pick()];
  }
  EXPECT_THAT(picks, ElementsAre(1, 2, 3));
}

TEST(StaticStrideSchedulerTest, ZeroWeightUsesMean) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {3, 0, 1};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  std::vector<int> picks(weights.size());
  for (int i = 0; i < 6; ++i) {
    ++picks[scheduler->Pick()];
  }
  EXPECT_THAT(picks, ElementsAre(3, 2, 1));
}

TEST(StaticStrideSchedulerTest, AllWeightsEqualIsRoundRobin) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {300, 300, 0};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  std::vector<size_t> picks(weights.size());
  for (int i = 0; i < 3; ++i) {
    picks[i] = scheduler->Pick();
  }

  // Each backend is selected exactly once.
  EXPECT_THAT(picks, UnorderedElementsAre(0, 1, 2));

  // And continues to be picked in the original order, whatever it may be.
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(scheduler->Pick(), picks[i % 3]);
  }
}

TEST(StaticStrideSchedulerTest, PicksAreDeterministic) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {1, 2, 3};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  const int n = 100;
  std::vector<size_t> picks;
  picks.reserve(n);
  for (int i = 0; i < n; ++i) {
    picks.push_back(scheduler->Pick());
  }
  for (int i = 0; i < 5; ++i) {
    sequence = 0;
    for (int j = 0; j < n; ++j) {
      EXPECT_EQ(scheduler->Pick(), picks[j]);
    }
  }
}

TEST(StaticStrideSchedulerTest, RebuildGiveSamePicks) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {1, 2, 3};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  const int n = 100;
  std::vector<size_t> picks;
  picks.reserve(n);
  for (int i = 0; i < n; ++i) {
    picks.push_back(scheduler->Pick());
  }

  // Rewind and make each pick with a new scheduler instance. This should give
  // identical picks.
  sequence = 0;
  for (int i = 0; i < n; ++i) {
    const absl::optional<StaticStrideScheduler> rebuild =
        StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                    [&] { return sequence++; });
    ASSERT_TRUE(rebuild.has_value());

    EXPECT_EQ(rebuild->Pick(), picks[i]);
  }
}

// This tests an internal implementation detail of StaticStrideScheduler --
// the highest weighted element will be picked on all `kMaxWeight` generations.
// The number of picks required to run through all values of the sequence is
// mean(weights) * kMaxWeight. It is worth testing this property because it can
// catch rounding and off-by-one errors.
TEST(StaticStrideSchedulerTest, LargestIsPickedEveryGeneration) {
  uint32_t sequence = 0;
  const std::vector<float> weights = {1, 2, 3};
  const int mean = 2;
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  const int kMaxWeight = std::numeric_limits<uint16_t>::max();
  int largest_weight_pick_count = 0;
  for (int i = 0; i < kMaxWeight * mean; ++i) {
    if (scheduler->Pick() == 2) {
      ++largest_weight_pick_count;
    }
  }
  EXPECT_EQ(largest_weight_pick_count, kMaxWeight);
}

const int kNumWeightsLow = 10;
const int kNumWeightsHigh = 10000;
const int kRangeMultiplier = 10;

// Returns a randomly ordered list of weights equally distributed between 0.6
// and 1.0.
const std::vector<float>& Weights() {
  static const NoDestruct<std::vector<float>> kWeights([] {
    static NoDestruct<absl::BitGen> bit_gen;
    std::vector<float> weights;
    weights.reserve(kNumWeightsHigh);
    for (int i = 0; i < 40; ++i) {
      for (int j = 0; j < kNumWeightsHigh / 40; ++j) {
        weights.push_back(0.6 + (0.01 * i));
      }
    }
    absl::c_shuffle(weights, *bit_gen);
    return weights;
  }());
  return *kWeights;
}

void BM_StaticStrideSchedulerPickNonAtomic(benchmark::State& state) {
  uint32_t sequence = 0;
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(
          absl::MakeSpan(Weights()).subspan(0, state.range(0)),
          [&] { return sequence++; });
  GPR_ASSERT(scheduler.has_value());
  for (auto s : state) {
    benchmark::DoNotOptimize(scheduler->Pick());
  }
}
BENCHMARK(BM_StaticStrideSchedulerPickNonAtomic)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kNumWeightsLow, kNumWeightsHigh);

void BM_StaticStrideSchedulerPickAtomic(benchmark::State& state) {
  std::atomic<uint32_t> sequence{0};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(
          absl::MakeSpan(Weights()).subspan(0, state.range(0)),
          [&] { return sequence.fetch_add(1, std::memory_order_relaxed); });
  GPR_ASSERT(scheduler.has_value());
  for (auto s : state) {
    benchmark::DoNotOptimize(scheduler->Pick());
  }
}
BENCHMARK(BM_StaticStrideSchedulerPickAtomic)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kNumWeightsLow, kNumWeightsHigh);

void BM_StaticStrideSchedulerMake(benchmark::State& state) {
  uint32_t sequence = 0;
  for (auto s : state) {
    const absl::optional<StaticStrideScheduler> scheduler =
        StaticStrideScheduler::Make(
            absl::MakeSpan(Weights()).subspan(0, state.range(0)),
            [&] { return sequence++; });
    GPR_ASSERT(scheduler.has_value());
  }
}
BENCHMARK(BM_StaticStrideSchedulerMake)
    ->RangeMultiplier(kRangeMultiplier)
    ->Range(kNumWeightsLow, kNumWeightsHigh);

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
