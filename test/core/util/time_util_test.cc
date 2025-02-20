//
// Copyright 2021 the gRPC authors.
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

#include "src/core/util/time_util.h"

#include <grpc/support/time.h>

#include <algorithm>
#include <vector>

#include "absl/time/time.h"
#include "gtest/gtest.h"

TEST(TimeUtilTest, ToGprTimeSpecFromAbslDurationWithRegularValues) {
  std::vector<int> times = {-10, -1, 0, 1, 10};
  for (int t : times) {
    EXPECT_EQ(0, gpr_time_cmp(gpr_time_from_nanos(t, GPR_TIMESPAN),
                              grpc_core::ToGprTimeSpec(absl::Nanoseconds(t))));
    EXPECT_EQ(0, gpr_time_cmp(gpr_time_from_micros(t, GPR_TIMESPAN),
                              grpc_core::ToGprTimeSpec(absl::Microseconds(t))));
    EXPECT_EQ(0, gpr_time_cmp(gpr_time_from_millis(t, GPR_TIMESPAN),
                              grpc_core::ToGprTimeSpec(absl::Milliseconds(t))));
    EXPECT_EQ(0, gpr_time_cmp(gpr_time_from_seconds(t, GPR_TIMESPAN),
                              grpc_core::ToGprTimeSpec(absl::Seconds(t))));
    EXPECT_EQ(0, gpr_time_cmp(gpr_time_from_minutes(t, GPR_TIMESPAN),
                              grpc_core::ToGprTimeSpec(absl::Minutes(t))));
    EXPECT_EQ(0, gpr_time_cmp(gpr_time_from_hours(t, GPR_TIMESPAN),
                              grpc_core::ToGprTimeSpec(absl::Hours(t))));
  }
}

TEST(TimeUtilTest, ToGprTimeSpecFromAbslDurationWithInfinites) {
  EXPECT_EQ(0,
            gpr_time_cmp(gpr_inf_past(GPR_TIMESPAN),
                         grpc_core::ToGprTimeSpec(-absl::InfiniteDuration())));
  EXPECT_EQ(0, gpr_time_cmp(gpr_time_0(GPR_TIMESPAN),
                            grpc_core::ToGprTimeSpec(absl::ZeroDuration())));
}

TEST(TimeUtilTest, ToGprTimeSpecFromAbslTimeWithRegularValues) {
  std::vector<int> times = {0, 10, 100000000};
  for (int t : times) {
    EXPECT_EQ(0,
              gpr_time_cmp(gpr_time_from_nanos(t, GPR_CLOCK_REALTIME),
                           grpc_core::ToGprTimeSpec(absl::FromUnixNanos(t))));
    EXPECT_EQ(0,
              gpr_time_cmp(gpr_time_from_micros(t, GPR_CLOCK_REALTIME),
                           grpc_core::ToGprTimeSpec(absl::FromUnixMicros(t))));
    EXPECT_EQ(0,
              gpr_time_cmp(gpr_time_from_millis(t, GPR_CLOCK_REALTIME),
                           grpc_core::ToGprTimeSpec(absl::FromUnixMillis(t))));
    EXPECT_EQ(0,
              gpr_time_cmp(gpr_time_from_seconds(t, GPR_CLOCK_REALTIME),
                           grpc_core::ToGprTimeSpec(absl::FromUnixSeconds(t))));
  }
}

TEST(TimeUtilTest, ToGprTimeSpecFromAbslTimeWithInfinites) {
  EXPECT_EQ(0, gpr_time_cmp(gpr_inf_future(GPR_CLOCK_REALTIME),
                            grpc_core::ToGprTimeSpec(absl::InfiniteFuture())));
  EXPECT_EQ(0, gpr_time_cmp(gpr_inf_past(GPR_CLOCK_REALTIME),
                            grpc_core::ToGprTimeSpec(absl::InfinitePast())));
}

TEST(TimeUtilTest, ToAbslDurationWithRegularValues) {
  std::vector<int> times = {-10, -1, 0, 1, 10};
  for (int t : times) {
    EXPECT_EQ(absl::Nanoseconds(t),
              grpc_core::ToAbslDuration(gpr_time_from_nanos(t, GPR_TIMESPAN)));
    EXPECT_EQ(absl::Microseconds(t),
              grpc_core::ToAbslDuration(gpr_time_from_micros(t, GPR_TIMESPAN)));
    EXPECT_EQ(absl::Milliseconds(t),
              grpc_core::ToAbslDuration(gpr_time_from_millis(t, GPR_TIMESPAN)));
    EXPECT_EQ(absl::Seconds(t), grpc_core::ToAbslDuration(
                                    gpr_time_from_seconds(t, GPR_TIMESPAN)));
    EXPECT_EQ(absl::Minutes(t), grpc_core::ToAbslDuration(
                                    gpr_time_from_minutes(t, GPR_TIMESPAN)));
    EXPECT_EQ(absl::Hours(t),
              grpc_core::ToAbslDuration(gpr_time_from_hours(t, GPR_TIMESPAN)));
  }
}

TEST(TimeUtilTest, ToAbslDurationWithInfinites) {
  EXPECT_EQ(absl::InfiniteDuration(),
            grpc_core::ToAbslDuration(gpr_inf_future(GPR_TIMESPAN)));
  EXPECT_EQ(-absl::InfiniteDuration(),
            grpc_core::ToAbslDuration(gpr_inf_past(GPR_TIMESPAN)));
}

TEST(TimeUtilTest, ToAbslTimeWithRegularValues) {
  std::vector<int> times = {0, 10, 100000000};
  for (int t : times) {
    EXPECT_EQ(absl::FromUnixNanos(t), grpc_core::ToAbslTime(gpr_time_from_nanos(
                                          t, GPR_CLOCK_REALTIME)));
    EXPECT_EQ(
        absl::FromUnixMicros(t),
        grpc_core::ToAbslTime(gpr_time_from_micros(t, GPR_CLOCK_REALTIME)));
    EXPECT_EQ(
        absl::FromUnixMillis(t),
        grpc_core::ToAbslTime(gpr_time_from_millis(t, GPR_CLOCK_REALTIME)));
    EXPECT_EQ(
        absl::FromUnixSeconds(t),
        grpc_core::ToAbslTime(gpr_time_from_seconds(t, GPR_CLOCK_REALTIME)));
  }
}

TEST(TimeUtilTest, ToAbslTimeWithInfinites) {
  EXPECT_EQ(absl::InfiniteFuture(),
            grpc_core::ToAbslTime(gpr_inf_future(GPR_CLOCK_REALTIME)));
  EXPECT_EQ(absl::InfinitePast(),
            grpc_core::ToAbslTime(gpr_inf_past(GPR_CLOCK_REALTIME)));
  EXPECT_EQ(absl::UnixEpoch(),
            grpc_core::ToAbslTime(gpr_time_0(GPR_CLOCK_REALTIME)));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
