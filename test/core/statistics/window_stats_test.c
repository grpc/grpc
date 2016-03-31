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

#include "src/core/ext/census/window_stats.h"
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdlib.h>
#include "test/core/util/test_config.h"

typedef struct test_stat {
  double value1;
  int value2;
} test_stat;

void add_test_stat(void *base, const void *addme) {
  test_stat *b = (test_stat *)base;
  const test_stat *a = (const test_stat *)addme;
  b->value1 += a->value1;
  b->value2 += a->value2;
}

void add_proportion_test_stat(double p, void *base, const void *addme) {
  test_stat *b = (test_stat *)base;
  const test_stat *a = (const test_stat *)addme;
  b->value1 += p * a->value1;
  b->value2 += p * a->value2 + 0.5; /* +0.5 is poor mans (no c99) round() */
}

const struct census_window_stats_stat_info kMyStatInfo = {
    sizeof(test_stat), NULL, add_test_stat, add_proportion_test_stat};

const gpr_timespec kMilliSecInterval = {0, 1000000};
const gpr_timespec kSecInterval = {1, 0};
const gpr_timespec kMinInterval = {60, 0};
const gpr_timespec kHourInterval = {3600, 0};
const gpr_timespec kPrimeInterval = {0, 101};

static int compare_double(double a, double b, double epsilon) {
  if (a >= b) {
    return (a > b + epsilon) ? 1 : 0;
  } else {
    return (b > a + epsilon) ? -1 : 0;
  }
}

void empty_test(void) {
  census_window_stats_sums result;
  const gpr_timespec zero = {0, 0};
  test_stat sum;
  struct census_window_stats *stats =
      census_window_stats_create(1, &kMinInterval, 5, &kMyStatInfo);
  GPR_ASSERT(stats != NULL);
  result.statistic = &sum;
  census_window_stats_get_sums(stats, zero, &result);
  GPR_ASSERT(result.count == 0 && sum.value1 == 0 && sum.value2 == 0);
  census_window_stats_get_sums(stats, gpr_now(GPR_CLOCK_REALTIME), &result);
  GPR_ASSERT(result.count == 0 && sum.value1 == 0 && sum.value2 == 0);
  census_window_stats_destroy(stats);
}

void one_interval_test(void) {
  const test_stat value = {0.1, 4};
  const double epsilon = 1e10 - 11;
  gpr_timespec when = {0, 0};
  census_window_stats_sums result;
  test_stat sum;
  /* granularity == 5 so width of internal windows should be 12s */
  struct census_window_stats *stats =
      census_window_stats_create(1, &kMinInterval, 5, &kMyStatInfo);
  GPR_ASSERT(stats != NULL);
  /* phase 1: insert a single value at t=0s, and check that various measurement
     times result in expected output values */
  census_window_stats_add(stats, when, &value);
  result.statistic = &sum;
  /* when = 0s, values extracted should be everything */
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 1, epsilon) == 0 &&
             compare_double(sum.value1, value.value1, epsilon) == 0 &&
             sum.value2 == value.value2);
  /* when = 6,30,60s, should be all of the data */
  when.tv_sec = 6;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 1.0, epsilon) == 0 &&
             compare_double(sum.value1, value.value1, epsilon) == 0 &&
             sum.value2 == value.value2);
  /* when == 30s,60s, should be all of the data */
  when.tv_sec = 30;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 1.0, epsilon) == 0 &&
             compare_double(sum.value1, value.value1, epsilon) == 0 &&
             sum.value2 == value.value2);
  when.tv_sec = 60;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 1.0, epsilon) == 0 &&
             compare_double(sum.value1, value.value1, epsilon) == 0 &&
             sum.value2 == value.value2);
  /* when = 66s, should be half (only take half of bottom bucket) */
  when.tv_sec = 66;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 0.5, epsilon) == 0 &&
             compare_double(sum.value1, value.value1 / 2, epsilon) == 0 &&
             sum.value2 == value.value2 / 2);
  /* when = 72s, should be completely out of window */
  when.tv_sec = 72;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 0, epsilon) == 0 &&
             compare_double(sum.value1, 0, epsilon) == 0 && sum.value2 == 0);

  /* phase 2: tear down and do as before, but inserting two values */
  census_window_stats_destroy(stats);
  stats = census_window_stats_create(1, &kMinInterval, 5, &kMyStatInfo);
  GPR_ASSERT(stats != NULL);
  when.tv_sec = 0;
  when.tv_nsec = 17;
  census_window_stats_add(stats, when, &value);
  when.tv_sec = 1;
  census_window_stats_add(stats, when, &value);
  when.tv_sec = 0;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 0, epsilon) == 0 &&
             compare_double(sum.value1, 0, epsilon) == 0 && sum.value2 == 0);
  /* time = 3s, 30s, should get all data */
  when.tv_sec = 3;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 2, epsilon) == 0 &&
             compare_double(sum.value1, 2 * value.value1, epsilon) == 0 &&
             sum.value2 == 2 * value.value2);
  when.tv_sec = 30;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(compare_double(result.count, 2, epsilon) == 0 &&
             compare_double(sum.value1, 2 * value.value1, epsilon) == 0 &&
             sum.value2 == 2 * value.value2);

  /* phase 3: insert into "middle" bucket, and force a shift, pushing out
     the two values in bottom bucket */
  when.tv_sec = 30;
  census_window_stats_add(stats, when, &value);
  when.tv_sec = 76;
  census_window_stats_add(stats, when, &value);
  when.tv_sec = 0;
  census_window_stats_get_sums(stats, when, &result);
  GPR_ASSERT(result.count == 0 && sum.value1 == 0 && sum.value2 == 0);
  when.tv_sec = 30;
  census_window_stats_get_sums(stats, when, &result);
  /* half of the single value in the 30 second bucket */
  GPR_ASSERT(compare_double(result.count, 0.5, epsilon) == 0 &&
             compare_double(sum.value1, value.value1 / 2, epsilon) == 0 &&
             sum.value2 == value.value2 / 2);
  when.tv_sec = 74;
  census_window_stats_get_sums(stats, when, &result);
  /* half of the 76 second bucket, all of the 30 second bucket */
  GPR_ASSERT(compare_double(result.count, 1.5, epsilon) == 0 &&
             compare_double(sum.value1, value.value1 * 1.5, epsilon) == 0 &&
             sum.value2 == value.value2 / 2 * 3);
  when.tv_sec = 76;
  census_window_stats_get_sums(stats, when, &result);
  /* >=76s, get all of the 76 second bucket, all of the 30 second bucket */
  GPR_ASSERT(compare_double(result.count, 2, epsilon) == 0 &&
             compare_double(sum.value1, value.value1 * 2, epsilon) == 0 &&
             sum.value2 == value.value2 * 2);
  when.tv_sec = 78;
  census_window_stats_get_sums(stats, when, &result);
  /* half of the 76 second bucket, all of the 30 second bucket */
  GPR_ASSERT(compare_double(result.count, 2, epsilon) == 0 &&
             compare_double(sum.value1, value.value1 * 2, epsilon) == 0 &&
             sum.value2 == value.value2 * 2);
  census_window_stats_destroy(stats);
}

void many_interval_test(void) {
  gpr_timespec intervals[4];
  const test_stat value = {123.45, 8};
  const double epsilon = 1e10 - 11;
  gpr_timespec when = {3600, 0}; /* one hour */
  census_window_stats_sums result[4];
  test_stat sums[4];
  int i;
  struct census_window_stats *stats;
  intervals[0] = kMilliSecInterval;
  intervals[1] = kSecInterval;
  intervals[2] = kMinInterval;
  intervals[3] = kHourInterval;
  for (i = 0; i < 4; i++) {
    result[i].statistic = &sums[i];
  }
  stats = census_window_stats_create(4, intervals, 100, &kMyStatInfo);
  GPR_ASSERT(stats != NULL);
  /* add 10 stats within half of each time range */
  for (i = 0; i < 10; i++) {
    when.tv_sec += 180; /* covers 30 min of one hour range */
    census_window_stats_add(stats, when, &value);
  }
  when.tv_sec += 120;
  for (i = 0; i < 10; i++) {
    when.tv_sec += 3; /* covers 30 sec of one minute range */
    census_window_stats_add(stats, when, &value);
  }
  when.tv_sec += 2;
  for (i = 0; i < 10; i++) {
    when.tv_nsec += 50000000; /* covers 0.5s of 1s range */
    census_window_stats_add(stats, when, &value);
  }
  when.tv_nsec += 2000000;
  for (i = 0; i < 10; i++) {
    when.tv_nsec += 50000; /* covers 0.5 ms of 1 ms range */
    census_window_stats_add(stats, when, &value);
  }
  when.tv_nsec += 20000;
  census_window_stats_get_sums(stats, when, result);
  GPR_ASSERT(compare_double(result[0].count, 10, epsilon) == 0 &&
             compare_double(sums[0].value1, value.value1 * 10, epsilon) == 0 &&
             sums[0].value2 == value.value2 * 10);
  when.tv_nsec += 20000000;
  census_window_stats_get_sums(stats, when, result);
  GPR_ASSERT(compare_double(result[1].count, 20, epsilon) == 0 &&
             compare_double(sums[1].value1, value.value1 * 20, epsilon) == 0 &&
             sums[1].value2 == value.value2 * 20);
  when.tv_sec += 2;
  census_window_stats_get_sums(stats, when, result);
  GPR_ASSERT(compare_double(result[2].count, 30, epsilon) == 0 &&
             compare_double(sums[2].value1, value.value1 * 30, epsilon) == 0 &&
             sums[2].value2 == value.value2 * 30);
  when.tv_sec += 72;
  census_window_stats_get_sums(stats, when, result);
  GPR_ASSERT(compare_double(result[3].count, 40, epsilon) == 0 &&
             compare_double(sums[3].value1, value.value1 * 40, epsilon) == 0 &&
             sums[3].value2 == value.value2 * 40);
  census_window_stats_destroy(stats);
}

void rolling_time_test(void) {
  const test_stat value = {0.1, 4};
  gpr_timespec when = {0, 0};
  census_window_stats_sums result;
  test_stat sum;
  int i;
  gpr_timespec increment = {0, 0};
  struct census_window_stats *stats =
      census_window_stats_create(1, &kMinInterval, 7, &kMyStatInfo);
  GPR_ASSERT(stats != NULL);
  srand(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  for (i = 0; i < 100000; i++) {
    increment.tv_nsec = rand() % 100000000; /* up to 1/10th second */
    when = gpr_time_add(when, increment);
    census_window_stats_add(stats, when, &value);
  }
  result.statistic = &sum;
  census_window_stats_get_sums(stats, when, &result);
  /* With 1/20th second average between samples, we expect 20*60 = 1200
     samples on average. Make sure we are within 100 of that. */
  GPR_ASSERT(compare_double(result.count, 1200, 100) == 0);
  census_window_stats_destroy(stats);
}

#include <stdio.h>
void infinite_interval_test(void) {
  const test_stat value = {0.1, 4};
  gpr_timespec when = {0, 0};
  census_window_stats_sums result;
  test_stat sum;
  int i;
  const int count = 100000;
  gpr_timespec increment = {0, 0};
  struct census_window_stats *stats = census_window_stats_create(
      1, &gpr_inf_future(GPR_CLOCK_REALTIME), 10, &kMyStatInfo);
  srand(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  for (i = 0; i < count; i++) {
    increment.tv_sec = rand() % 21600; /* 6 hours */
    when = gpr_time_add(when, increment);
    census_window_stats_add(stats, when, &value);
  }
  result.statistic = &sum;
  census_window_stats_get_sums(stats, when, &result);
  /* The only thing it makes sense to compare for "infinite" periods is the
     total counts */
  GPR_ASSERT(result.count == count);
  census_window_stats_destroy(stats);
}

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);
  empty_test();
  one_interval_test();
  many_interval_test();
  rolling_time_test();
  infinite_interval_test();
  return 0;
}
