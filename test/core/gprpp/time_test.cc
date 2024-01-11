// Copyright 2021 gRPC authors.
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

#include "src/core/lib/gprpp/time.h"

#include <limits>

#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {

TEST(TimestampTest, Empty) {
  EXPECT_EQ(Timestamp(), Timestamp::ProcessEpoch());
}

TEST(TimestampTest, Infinities) {
  EXPECT_EQ(Timestamp::InfFuture() - Duration::Milliseconds(1),
            Timestamp::InfFuture());
  EXPECT_EQ(Timestamp::InfPast() + Duration::Milliseconds(1),
            Timestamp::InfPast());
}

TEST(TimestampTest, ToString) {
  EXPECT_EQ(Timestamp::FromMillisecondsAfterProcessEpoch(42).ToString(),
            "@42ms");
  EXPECT_EQ(Timestamp::InfFuture().ToString(), "@∞");
  EXPECT_EQ(Timestamp::InfPast().ToString(), "@-∞");
}

TEST(DurationTest, Empty) { EXPECT_EQ(Duration(), Duration::Zero()); }

TEST(DurationTest, Scales) {
  EXPECT_EQ(Duration::Milliseconds(1000), Duration::Seconds(1));
  EXPECT_EQ(Duration::Seconds(60), Duration::Minutes(1));
  EXPECT_EQ(Duration::Minutes(60), Duration::Hours(1));
  EXPECT_EQ(Duration::FromSecondsAsDouble(1.2), Duration::Milliseconds(1200));
  EXPECT_EQ(Duration::FromSecondsAndNanoseconds(1, 300000000),
            Duration::Milliseconds(1300));
}

TEST(DurationTest, Epsilon) {
  EXPECT_LE(Duration::Epsilon(), Duration::Milliseconds(1));
}

TEST(DurationTest, Infinities) {
  EXPECT_EQ(Duration::Infinity() - Duration::Milliseconds(1),
            Duration::Infinity());
  EXPECT_EQ(Duration::Infinity() + Duration::Milliseconds(1),
            Duration::Infinity());
  EXPECT_EQ(Duration::Infinity() * 2, Duration::Infinity());
  EXPECT_EQ(Duration::Infinity() * -1, Duration::NegativeInfinity());
  EXPECT_EQ(Duration::Infinity() / 3, Duration::Infinity());
  EXPECT_EQ(Duration::NegativeInfinity() / -3, Duration::Infinity());
  EXPECT_EQ(Duration::NegativeInfinity() + Duration::Milliseconds(1),
            Duration::NegativeInfinity());
  EXPECT_EQ(Duration::NegativeInfinity() - Duration::Milliseconds(1),
            Duration::NegativeInfinity());
  EXPECT_EQ(Duration::NegativeInfinity() / 3, Duration::NegativeInfinity());
  EXPECT_EQ(Duration::Hours(std::numeric_limits<int64_t>::max()),
            Duration::Infinity());
  EXPECT_EQ(Duration::FromSecondsAsDouble(1e100), Duration::Infinity());
  EXPECT_EQ(Duration::FromSecondsAsDouble(-1e100),
            Duration::NegativeInfinity());
}

TEST(DurationTest, Multiplication) {
  Duration d = Duration::Seconds(5);
  EXPECT_EQ(d * 2, Duration::Seconds(10));
  d *= 3;
  EXPECT_EQ(d, Duration::Seconds(15));
}

TEST(DurationTest, FromTimespan) {
  EXPECT_EQ(Duration::FromTimespec(gpr_time_from_millis(1234, GPR_TIMESPAN)),
            Duration::Milliseconds(1234));
}

TEST(DurationTest, ToString) {
  EXPECT_EQ(Duration::Milliseconds(42).ToString(), "42ms");
  EXPECT_EQ(Duration::Infinity().ToString(), "∞");
  EXPECT_EQ(Duration::NegativeInfinity().ToString(), "-∞");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
