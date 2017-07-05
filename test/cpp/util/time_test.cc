/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc++/support/time.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::system_clock;

namespace grpc {
namespace {

class TimeTest : public ::testing::Test {};

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
  // This will cause an overflow
  Timepoint2Timespec(
      std::chrono::time_point<system_clock, std::chrono::seconds>::max(),
      &from_time_point_max);
  EXPECT_EQ(
      0, gpr_time_cmp(gpr_inf_future(GPR_CLOCK_REALTIME), from_time_point_max));
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
