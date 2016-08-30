/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
