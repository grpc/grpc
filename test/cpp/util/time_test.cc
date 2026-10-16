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

#include "absl/time/time.h"

#include <grpc/support/time.h>
#include <grpcpp/client_context.h>
#include <grpcpp/support/time.h>

#include "src/core/util/time_precise.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/time/clock.h"

using std::chrono::microseconds;
using std::chrono::system_clock;

namespace grpc {
namespace {

#ifdef GPR_CYCLE_COUNTER_CUSTOM
using absl::base_internal::CycleClock;
#endif  // GPR_CYCLE_COUNTER_CUSTOM

class TimeTest : public ::testing::Test {};

void VerifyGprTimespecToTimeAndBack(gpr_timespec time) {
  time = gpr_convert_clock_type(time, GPR_CLOCK_REALTIME);
  absl::Time base_time = grpc::TimeFromGprTimespec(time);
  grpc::TimePoint<absl::Time> tp(base_time);
  // TimePoint will only generates GPR_CLOCK_REALTIME type.
  EXPECT_EQ(GPR_CLOCK_REALTIME, tp.raw_time().clock_type);
  EXPECT_EQ(time.tv_sec, tp.raw_time().tv_sec);
  EXPECT_EQ(time.tv_nsec, tp.raw_time().tv_nsec);
}

TEST_F(TimeTest, ClientContextSetDeadline) {
  grpc::ClientContext context;
  absl::Time time = absl::Now();
  context.set_deadline(time);
}

struct TimeFromTimespecTest : ::testing::TestWithParam<gpr_clock_type> {};

TEST_P(TimeFromTimespecTest, Infinity) {
  EXPECT_EQ(absl::InfiniteFuture(),
            grpc::TimeFromGprTimespec(gpr_inf_future(GetParam())));
  EXPECT_EQ(absl::InfinitePast(),
            grpc::TimeFromGprTimespec(gpr_inf_past(GetParam())));
}

TEST_P(TimeFromTimespecTest, ConvertToTimeAndBack) {
  gpr_time_init();
  VerifyGprTimespecToTimeAndBack(gpr_inf_future(GetParam()));
  VerifyGprTimespecToTimeAndBack(gpr_inf_past(GetParam()));
  VerifyGprTimespecToTimeAndBack({0, 0, GetParam()});
  VerifyGprTimespecToTimeAndBack({100000, 0, GetParam()});
  VerifyGprTimespecToTimeAndBack({100000000, 10000000, GetParam()});
  VerifyGprTimespecToTimeAndBack(gpr_time_from_hours(100, GetParam()));
  VerifyGprTimespecToTimeAndBack(gpr_time_from_micros(10000000, GetParam()));
}

INSTANTIATE_TEST_SUITE_P(All, TimeFromTimespecTest,
                         ::testing::Values(GPR_CLOCK_MONOTONIC,
                                           GPR_CLOCK_REALTIME,
                                           GPR_CLOCK_PRECISE));

#ifdef GPR_CYCLE_COUNTER_CUSTOM
TEST(PreciseIsCycleClock, CheckNow) {
  gpr_time_init();
  gpr_timespec precise_now = gpr_now(GPR_CLOCK_PRECISE);
  int64_t cycle_count = CycleClock::Now();
  gpr_timespec cycle_now = gpr_cycle_counter_to_time(cycle_count);
  EXPECT_EQ(GPR_CLOCK_PRECISE, cycle_now.clock_type);
  EXPECT_NEAR(precise_now.tv_sec, precise_now.tv_sec, 1);
}

TEST(PreciseIsCycleClock, CheckCycleCount) {
  gpr_time_init();
  int64_t cycle_count_direct = CycleClock::Now();
  int64_t cycle_count = gpr_get_cycle_counter();
  EXPECT_NEAR(cycle_count / CycleClock::Frequency(),
              cycle_count_direct / CycleClock::Frequency(), 1);
}
#endif

TEST_F(TimeTest, AbsolutePointTest) {
  int64_t us = 10000000L;
  gpr_timespec ts = gpr_time_from_micros(us, GPR_TIMESPAN);
  ts.clock_type = GPR_CLOCK_REALTIME;
  system_clock::time_point tp{microseconds(us)};
  system_clock::time_point tp_converted = Timespec2Timepoint(ts);
  gpr_timespec ts_converted;
  Timepoint2Timespec(tp_converted, &ts_converted);
  EXPECT_TRUE(ts.tv_sec == ts_converted.tv_sec);
  EXPECT_TRUE(ts.tv_nsec == ts_converted.tv_nsec);
  system_clock::time_point tp_converted_2 = Timespec2Timepoint(ts_converted);
  EXPECT_TRUE(tp == tp_converted);
  EXPECT_TRUE(tp == tp_converted_2);
}

// gpr_inf_future is treated specially and mapped to/from time_point::max()
TEST_F(TimeTest, InfFuture) {
  EXPECT_EQ(system_clock::time_point::max(),
            Timespec2Timepoint(gpr_inf_future(GPR_CLOCK_REALTIME)));
  gpr_timespec from_time_point_max;
  Timepoint2Timespec(system_clock::time_point::max(), &from_time_point_max);
  EXPECT_EQ(
      0, gpr_time_cmp(gpr_inf_future(GPR_CLOCK_REALTIME), from_time_point_max));
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
