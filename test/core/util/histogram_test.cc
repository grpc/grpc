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

#include "test/core/util/histogram.h"

#include "gtest/gtest.h"

#include <grpc/support/log.h>

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x);

TEST(HistogramTest, NoOp) {
  grpc_histogram_destroy(grpc_histogram_create(0.01, 60e9));
}

static void expect_percentile(grpc_histogram* h, double percentile,
                              double min_expect, double max_expect) {
  double got = grpc_histogram_percentile(h, percentile);
  gpr_log(GPR_INFO, "@%f%%, expect %f <= %f <= %f", percentile, min_expect, got,
          max_expect);
  ASSERT_LE(min_expect, got);
  ASSERT_LE(got, max_expect);
}

TEST(HistogramTest, Simple) {
  grpc_histogram* h;

  LOG_TEST("test_simple");

  h = grpc_histogram_create(0.01, 60e9);
  grpc_histogram_add(h, 10000);
  grpc_histogram_add(h, 10000);
  grpc_histogram_add(h, 11000);
  grpc_histogram_add(h, 11000);

  expect_percentile(h, 50, 10001, 10999);
  ASSERT_EQ(grpc_histogram_mean(h), 10500);

  grpc_histogram_destroy(h);
}

TEST(HistogramTest, Percentile) {
  grpc_histogram* h;
  double last;
  double i;
  double cur;

  LOG_TEST("test_percentile");

  h = grpc_histogram_create(0.05, 1e9);
  grpc_histogram_add(h, 2.5);
  grpc_histogram_add(h, 2.5);
  grpc_histogram_add(h, 8);
  grpc_histogram_add(h, 4);

  ASSERT_EQ(grpc_histogram_count(h), 4);
  ASSERT_EQ(grpc_histogram_minimum(h), 2.5);
  ASSERT_EQ(grpc_histogram_maximum(h), 8);
  ASSERT_EQ(grpc_histogram_sum(h), 17);
  ASSERT_EQ(grpc_histogram_sum_of_squares(h), 92.5);
  ASSERT_EQ(grpc_histogram_mean(h), 4.25);
  ASSERT_EQ(grpc_histogram_variance(h), 5.0625);
  ASSERT_EQ(grpc_histogram_stddev(h), 2.25);

  expect_percentile(h, -10, 2.5, 2.5);
  expect_percentile(h, 0, 2.5, 2.5);
  expect_percentile(h, 12.5, 2.5, 2.5);
  expect_percentile(h, 25, 2.5, 2.5);
  expect_percentile(h, 37.5, 2.5, 2.8);
  expect_percentile(h, 50, 3.0, 3.5);
  expect_percentile(h, 62.5, 3.5, 4.5);
  expect_percentile(h, 75, 5, 7.9);
  expect_percentile(h, 100, 8, 8);
  expect_percentile(h, 110, 8, 8);

  // test monotonicity
  last = 0.0;
  for (i = 0; i < 100.0; i += 0.01) {
    cur = grpc_histogram_percentile(h, i);
    ASSERT_GE(cur, last);
    last = cur;
  }

  grpc_histogram_destroy(h);
}

TEST(HistogramTest, Merge) {
  grpc_histogram *h1, *h2;
  double last;
  double i;
  double cur;

  LOG_TEST("test_merge");

  h1 = grpc_histogram_create(0.05, 1e9);
  grpc_histogram_add(h1, 2.5);
  grpc_histogram_add(h1, 2.5);
  grpc_histogram_add(h1, 8);
  grpc_histogram_add(h1, 4);

  h2 = grpc_histogram_create(0.01, 1e9);
  ASSERT_EQ(grpc_histogram_merge(h1, h2), 0);
  grpc_histogram_destroy(h2);

  h2 = grpc_histogram_create(0.05, 1e10);
  ASSERT_EQ(grpc_histogram_merge(h1, h2), 0);
  grpc_histogram_destroy(h2);

  h2 = grpc_histogram_create(0.05, 1e9);
  ASSERT_EQ(grpc_histogram_merge(h1, h2), 1);
  ASSERT_EQ(grpc_histogram_count(h1), 4);
  ASSERT_EQ(grpc_histogram_minimum(h1), 2.5);
  ASSERT_EQ(grpc_histogram_maximum(h1), 8);
  ASSERT_EQ(grpc_histogram_sum(h1), 17);
  ASSERT_EQ(grpc_histogram_sum_of_squares(h1), 92.5);
  ASSERT_EQ(grpc_histogram_mean(h1), 4.25);
  ASSERT_EQ(grpc_histogram_variance(h1), 5.0625);
  ASSERT_EQ(grpc_histogram_stddev(h1), 2.25);
  grpc_histogram_destroy(h2);

  h2 = grpc_histogram_create(0.05, 1e9);
  grpc_histogram_add(h2, 7.0);
  grpc_histogram_add(h2, 17.0);
  grpc_histogram_add(h2, 1.0);
  ASSERT_EQ(grpc_histogram_merge(h1, h2), 1);
  ASSERT_EQ(grpc_histogram_count(h1), 7);
  ASSERT_EQ(grpc_histogram_minimum(h1), 1.0);
  ASSERT_EQ(grpc_histogram_maximum(h1), 17.0);
  ASSERT_EQ(grpc_histogram_sum(h1), 42.0);
  ASSERT_EQ(grpc_histogram_sum_of_squares(h1), 431.5);
  ASSERT_EQ(grpc_histogram_mean(h1), 6.0);

  // test monotonicity
  last = 0.0;
  for (i = 0; i < 100.0; i += 0.01) {
    cur = grpc_histogram_percentile(h1, i);
    ASSERT_GE(cur, last);
    last = cur;
  }

  grpc_histogram_destroy(h1);
  grpc_histogram_destroy(h2);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
