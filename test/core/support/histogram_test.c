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

#include <grpc/support/histogram.h>
#include <grpc/support/log.h>

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x);

static void test_no_op(void) {
  gpr_histogram_destroy(gpr_histogram_create(0.01, 60e9));
}

static void expect_percentile(gpr_histogram *h, double percentile,
                              double min_expect, double max_expect) {
  double got = gpr_histogram_percentile(h, percentile);
  gpr_log(GPR_INFO, "@%f%%, expect %f <= %f <= %f", percentile, min_expect, got,
          max_expect);
  GPR_ASSERT(min_expect <= got);
  GPR_ASSERT(got <= max_expect);
}

static void test_simple(void) {
  gpr_histogram *h;

  LOG_TEST("test_simple");

  h = gpr_histogram_create(0.01, 60e9);
  gpr_histogram_add(h, 10000);
  gpr_histogram_add(h, 10000);
  gpr_histogram_add(h, 11000);
  gpr_histogram_add(h, 11000);

  expect_percentile(h, 50, 10001, 10999);
  GPR_ASSERT(gpr_histogram_mean(h) == 10500);

  gpr_histogram_destroy(h);
}

static void test_percentile(void) {
  gpr_histogram *h;
  double last;
  double i;
  double cur;

  LOG_TEST("test_percentile");

  h = gpr_histogram_create(0.05, 1e9);
  gpr_histogram_add(h, 2.5);
  gpr_histogram_add(h, 2.5);
  gpr_histogram_add(h, 8);
  gpr_histogram_add(h, 4);

  GPR_ASSERT(gpr_histogram_count(h) == 4);
  GPR_ASSERT(gpr_histogram_minimum(h) == 2.5);
  GPR_ASSERT(gpr_histogram_maximum(h) == 8);
  GPR_ASSERT(gpr_histogram_sum(h) == 17);
  GPR_ASSERT(gpr_histogram_sum_of_squares(h) == 92.5);
  GPR_ASSERT(gpr_histogram_mean(h) == 4.25);
  GPR_ASSERT(gpr_histogram_variance(h) == 5.0625);
  GPR_ASSERT(gpr_histogram_stddev(h) == 2.25);

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

  /* test monotonicity */
  last = 0.0;
  for (i = 0; i < 100.0; i += 0.01) {
    cur = gpr_histogram_percentile(h, i);
    GPR_ASSERT(cur >= last);
    last = cur;
  }

  gpr_histogram_destroy(h);
}

static void test_merge(void) {
  gpr_histogram *h1, *h2;
  double last;
  double i;
  double cur;

  LOG_TEST("test_merge");

  h1 = gpr_histogram_create(0.05, 1e9);
  gpr_histogram_add(h1, 2.5);
  gpr_histogram_add(h1, 2.5);
  gpr_histogram_add(h1, 8);
  gpr_histogram_add(h1, 4);

  h2 = gpr_histogram_create(0.01, 1e9);
  GPR_ASSERT(gpr_histogram_merge(h1, h2) == 0);
  gpr_histogram_destroy(h2);

  h2 = gpr_histogram_create(0.05, 1e10);
  GPR_ASSERT(gpr_histogram_merge(h1, h2) == 0);
  gpr_histogram_destroy(h2);

  h2 = gpr_histogram_create(0.05, 1e9);
  GPR_ASSERT(gpr_histogram_merge(h1, h2) == 1);
  GPR_ASSERT(gpr_histogram_count(h1) == 4);
  GPR_ASSERT(gpr_histogram_minimum(h1) == 2.5);
  GPR_ASSERT(gpr_histogram_maximum(h1) == 8);
  GPR_ASSERT(gpr_histogram_sum(h1) == 17);
  GPR_ASSERT(gpr_histogram_sum_of_squares(h1) == 92.5);
  GPR_ASSERT(gpr_histogram_mean(h1) == 4.25);
  GPR_ASSERT(gpr_histogram_variance(h1) == 5.0625);
  GPR_ASSERT(gpr_histogram_stddev(h1) == 2.25);
  gpr_histogram_destroy(h2);

  h2 = gpr_histogram_create(0.05, 1e9);
  gpr_histogram_add(h2, 7.0);
  gpr_histogram_add(h2, 17.0);
  gpr_histogram_add(h2, 1.0);
  GPR_ASSERT(gpr_histogram_merge(h1, h2) == 1);
  GPR_ASSERT(gpr_histogram_count(h1) == 7);
  GPR_ASSERT(gpr_histogram_minimum(h1) == 1.0);
  GPR_ASSERT(gpr_histogram_maximum(h1) == 17.0);
  GPR_ASSERT(gpr_histogram_sum(h1) == 42.0);
  GPR_ASSERT(gpr_histogram_sum_of_squares(h1) == 431.5);
  GPR_ASSERT(gpr_histogram_mean(h1) == 6.0);

  /* test monotonicity */
  last = 0.0;
  for (i = 0; i < 100.0; i += 0.01) {
    cur = gpr_histogram_percentile(h1, i);
    GPR_ASSERT(cur >= last);
    last = cur;
  }

  gpr_histogram_destroy(h1);
  gpr_histogram_destroy(h2);
}

int main(void) {
  test_no_op();
  test_simple();
  test_percentile();
  test_merge();
  return 0;
}
