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

#include <limits>
#include <vector>

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

using ::testing::ElementsAre;
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

TEST(StaticStrideSchedulerTest, MaxIsClampedForHighRatio) {
  uint32_t sequence = 0;
  const std::vector<float> weights{81, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                   1,  1, 1, 1, 1, 1, 1, 1, 1, 1};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  // max gets clamped to mean*maxRatio = 50 for this set of weights. So if we
  // pick 50 + 19 times we should get all possible picks.
  std::vector<int> picks(weights.size());
  for (int i = 0; i < 69; ++i) {
    ++picks[scheduler->Pick()];
  }
  EXPECT_THAT(picks, ElementsAre(50, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 1, 1, 1, 1, 1));
}

TEST(StaticStrideSchedulerTest, MinIsClampedForHighRatio) {
  uint32_t sequence = 0;
  const std::vector<float> weights{100, 1e-10};
  const absl::optional<StaticStrideScheduler> scheduler =
      StaticStrideScheduler::Make(absl::MakeSpan(weights),
                                  [&] { return sequence++; });
  ASSERT_TRUE(scheduler.has_value());

  // We pick 201 elements and ensure that the second channel (with epsilon
  // weight) also gets picked. The math is: mean value of elements is ~50, so
  // the first channel keeps its weight of 100, but the second element's weight
  // gets capped from below to 50*0.01 = 0.5.
  std::vector<int> picks(weights.size());
  for (int i = 0; i < 201; ++i) {
    ++picks[scheduler->Pick()];
  }
  EXPECT_THAT(picks, ElementsAre(200, 1));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
