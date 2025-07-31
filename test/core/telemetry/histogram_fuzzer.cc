// Copyright 2025 gRPC authors.
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

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/telemetry/histogram.h"

namespace grpc_core {
namespace {

TEST(HistogramFuzzer, BucketInBoundsForIsCorrect) {
  EXPECT_EQ(BucketInBoundsFor({1, 2, 3, 4}, 0), 0);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 3, 4}, 1), 1);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 3, 4}, 2), 2);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 3, 4}, 3), 3);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 3, 4}, 4), 3);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 3, 4}, 5), 3);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 0), 0);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 1), 1);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 2), 2);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 3), 2);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 4), 3);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 5), 3);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 6), 3);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 7), 3);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 8), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 9), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 10), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 11), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 12), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 13), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 14), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 15), 4);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 16), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 17), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 18), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 19), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 20), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 21), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 22), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 23), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 24), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 25), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 26), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 27), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 28), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 29), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 30), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 31), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 32), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 33), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 34), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 35), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 36), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 37), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 38), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 39), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 40), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 41), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 42), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 43), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 44), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 45), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 46), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 47), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 48), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 49), 5);
  EXPECT_EQ(BucketInBoundsFor({1, 2, 4, 8, 16, 32}, 50), 5);
}

void ExponentialHistogramBasicsAreValid(int64_t max, size_t buckets) {
  ExponentialHistogramShape shape(max, buckets);
  if (max <= buckets) {
    EXPECT_EQ(shape.buckets(), max);
  } else {
    EXPECT_EQ(shape.buckets(), buckets);
  }
  EXPECT_GE(shape.bounds()[0], 1);
  for (size_t i = 1; i < shape.bounds().size(); ++i) {
    EXPECT_GT(shape.bounds()[i], shape.bounds()[i - 1]);
  }
  EXPECT_EQ(shape.bounds().size(), shape.buckets());
  EXPECT_EQ(shape.bounds().back(), max);
}
FUZZ_TEST(HistogramFuzzer, ExponentialHistogramBasicsAreValid)
    .WithDomains(fuzztest::InRange(2, 1000000000),
                 fuzztest::InRange(2, 100000));

TEST(HistogramFuzzer, ExponentialHistogramBasicsAreValidRegression) {
  ExponentialHistogramBasicsAreValid(591424425, 100000);
}

TEST(HistogramFuzzer, ExponentialHistogramBasicsAreValidRegression2) {
  ExponentialHistogramBasicsAreValid(2, 41438);
}

void ExponentialHistogramBucketForIsCorrect(int64_t max, size_t buckets,
                                            int64_t value) {
  ExponentialHistogramShape shape(max, buckets);
  size_t bucket = shape.BucketFor(value);
  size_t expected_bucket = BucketInBoundsFor(shape.bounds(), value);
  EXPECT_EQ(bucket, expected_bucket)
      << "max: " << max << " buckets: " << buckets << " value: " << value
      << "\n"
      << " bounds: " << absl::StrJoin(shape.bounds(), ",") << "\n"
      << " lookup_table: " << absl::StrJoin(shape.lookup_table(), ",");
}
FUZZ_TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrect)
    .WithDomains(fuzztest::InRange(2, 1000000000), fuzztest::InRange(2, 10000),
                 fuzztest::Arbitrary<int64_t>());

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression) {
  ExponentialHistogramBucketForIsCorrect(438734458, 17836, 7393050624709854766);
}

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression2) {
  ExponentialHistogramBucketForIsCorrect(1000000000, 2, 2);
}

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression3) {
  ExponentialHistogramBucketForIsCorrect(1000000000, 12407, 20726);
}

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression4) {
  ExponentialHistogramBucketForIsCorrect(2, 3, 0);
}

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression5) {
  ExponentialHistogramBucketForIsCorrect(2, 4, 2);
}

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression6) {
  ExponentialHistogramBucketForIsCorrect(2, 2, 3);
}

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression7) {
  ExponentialHistogramBucketForIsCorrect(2, 2, 2);
}

TEST(HistogramFuzzer, ExponentialHistogramBucketForIsCorrectRegression8) {
  ExponentialHistogramBucketForIsCorrect(389599954, 2, 2133);
}

}  // namespace
}  // namespace grpc_core
