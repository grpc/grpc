//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/lib/gprpp/time_averaged_stats.h"

#include <math.h>

#include <gtest/gtest.h>

namespace grpc_core {
namespace {

TEST(TimeAveragedStatsTest, NoRegressNoPersistTest1) {
  TimeAveragedStats tas(1000, 0, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight());

  // Should have no effect
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight());

  // Should replace old average
  tas.AddSample(2000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight());
}

TEST(TimeAveragedStatsTest, NoRegressNoPersistTest2) {
  TimeAveragedStats tas(1000, 0, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg());
  // Should replace init value
  tas.AddSample(2000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight());

  tas.AddSample(3000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(3000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight());
}

TEST(TimeAveragedStatsTest, NoRegressNoPersistTest3) {
  TimeAveragedStats tas(1000, 0, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg());
  // Should replace init value
  tas.AddSample(2500);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(2500, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight());

  tas.AddSample(3500);
  tas.AddSample(4500);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(4000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight());
}

TEST(TimeAveragedStatsTest, SomeRegressNoPersistTest) {
  TimeAveragedStats tas(1000, 0.5, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight());
  tas.AddSample(2000);
  tas.AddSample(2000);
  tas.UpdateAverage();
  // (2 * 2000 + 0.5 * 1000) / 2.5
  EXPECT_DOUBLE_EQ(1800, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2.5, tas.aggregate_total_weight());
}

TEST(TimeAveragedStatsTest, SomeDecayTest) {
  TimeAveragedStats tas(1000, 1, 0.0);
  EXPECT_EQ(1000, tas.aggregate_weighted_avg());
  // Should avg with init value
  tas.AddSample(2000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(1500, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight());

  tas.AddSample(2000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(1500, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight());

  tas.AddSample(2000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(1500, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight());
}

TEST(TimeAveragedStatsTest, NoRegressFullPersistTest) {
  TimeAveragedStats tas(1000, 0, 1.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight());

  // Should replace init value
  tas.AddSample(2000);
  tas.UpdateAverage();
  EXPECT_EQ(2000, tas.aggregate_weighted_avg());
  EXPECT_EQ(1, tas.aggregate_total_weight());

  // Will result in average of the 3 samples.
  tas.AddSample(2300);
  tas.AddSample(2300);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(2200, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(3, tas.aggregate_total_weight());
}

TEST(TimeAveragedStatsTest, NoRegressSomePersistTest) {
  TimeAveragedStats tas(1000, 0, 0.5);
  // Should replace init value
  tas.AddSample(2000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight());

  tas.AddSample(2500);
  tas.AddSample(4000);
  tas.UpdateAverage();
  EXPECT_DOUBLE_EQ(3000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2.5, tas.aggregate_total_weight());
}

TEST(TimeAveragedStatsTest, SomeRegressSomePersistTest) {
  TimeAveragedStats tas(1000, 0.4, 0.6);
  // Sample weight = 0
  EXPECT_EQ(1000, tas.aggregate_weighted_avg());
  EXPECT_EQ(0, tas.aggregate_total_weight());

  tas.UpdateAverage();
  // (0.6 * 0 * 1000 + 0.4 * 1000 / 0.4)
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(0.4, tas.aggregate_total_weight());

  tas.AddSample(2640);
  tas.UpdateAverage();
  // (1 * 2640 + 0.6 * 0.4 * 1000 + 0.4 * 1000 / (1 + 0.6 * 0.4 + 0.4)
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(1.64, tas.aggregate_total_weight());

  tas.AddSample(2876.8);
  tas.UpdateAverage();
  // (1 * 2876.8 + 0.6 * 1.64 * 2000 + 0.4 * 1000 / (1 + 0.6 * 1.64 + 0.4)
  EXPECT_DOUBLE_EQ(2200, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2.384, tas.aggregate_total_weight());

  tas.AddSample(4944.32);
  tas.UpdateAverage();
  // (1 * 4944.32 + 0.6 * 2.384 * 2200 + 0.4 * 1000) /
  // (1 + 0.6 * 2.384 + 0.4)
  EXPECT_DOUBLE_EQ(3000, tas.aggregate_weighted_avg());
  EXPECT_DOUBLE_EQ(2.8304, tas.aggregate_total_weight());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
