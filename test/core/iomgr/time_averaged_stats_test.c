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

#include "src/core/lib/iomgr/time_averaged_stats.h"

#include <math.h>

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

#define EXPECT_EQ(a, b) GPR_ASSERT((a) == (b))
#define EXPECT_DOUBLE_EQ(a, b) GPR_ASSERT(fabs((a) - (b)) < 1e-9)

static void no_regress_no_persist_test_1(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 0, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight);

  /* Should have no effect */
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight);

  /* Should replace old average */
  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight);
}

static void no_regress_no_persist_test_2(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 0, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg);
  /* Should replace init value */
  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 3000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(3000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight);
}

static void no_regress_no_persist_test_3(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 0, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg);
  /* Should replace init value */
  grpc_time_averaged_stats_add_sample(&tas, 2500);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(2500, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 3500);
  grpc_time_averaged_stats_add_sample(&tas, 4500);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(4000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight);
}

static void some_regress_no_persist_test(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 0.5, 0.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight);
  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  /* (2 * 2000 + 0.5 * 1000) / 2.5 */
  EXPECT_DOUBLE_EQ(1800, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2.5, tas.aggregate_total_weight);
}

static void some_decay_test(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 1, 0.0);
  EXPECT_EQ(1000, tas.aggregate_weighted_avg);
  /* Should avg with init value */
  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(1500, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(1500, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(1500, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2, tas.aggregate_total_weight);
}

static void no_regress_full_persist_test(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 0, 1.0);
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(0, tas.aggregate_total_weight);

  /* Should replace init value */
  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_EQ(2000, tas.aggregate_weighted_avg);
  EXPECT_EQ(1, tas.aggregate_total_weight);

  /* Will result in average of the 3 samples. */
  grpc_time_averaged_stats_add_sample(&tas, 2300);
  grpc_time_averaged_stats_add_sample(&tas, 2300);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(2200, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(3, tas.aggregate_total_weight);
}

static void no_regress_some_persist_test(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 0, 0.5);
  /* Should replace init value */
  grpc_time_averaged_stats_add_sample(&tas, 2000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(1, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 2500);
  grpc_time_averaged_stats_add_sample(&tas, 4000);
  grpc_time_averaged_stats_update_average(&tas);
  EXPECT_DOUBLE_EQ(3000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2.5, tas.aggregate_total_weight);
}

static void some_regress_some_persist_test(void) {
  grpc_time_averaged_stats tas;
  grpc_time_averaged_stats_init(&tas, 1000, 0.4, 0.6);
  /* Sample weight = 0 */
  EXPECT_EQ(1000, tas.aggregate_weighted_avg);
  EXPECT_EQ(0, tas.aggregate_total_weight);

  grpc_time_averaged_stats_update_average(&tas);
  /* (0.6 * 0 * 1000 + 0.4 * 1000 / 0.4) */
  EXPECT_DOUBLE_EQ(1000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(0.4, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 2640);
  grpc_time_averaged_stats_update_average(&tas);
  /* (1 * 2640 + 0.6 * 0.4 * 1000 + 0.4 * 1000 / (1 + 0.6 * 0.4 + 0.4) */
  EXPECT_DOUBLE_EQ(2000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(1.64, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 2876.8);
  grpc_time_averaged_stats_update_average(&tas);
  /* (1 * 2876.8 + 0.6 * 1.64 * 2000 + 0.4 * 1000 / (1 + 0.6 * 1.64 + 0.4) */
  EXPECT_DOUBLE_EQ(2200, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2.384, tas.aggregate_total_weight);

  grpc_time_averaged_stats_add_sample(&tas, 4944.32);
  grpc_time_averaged_stats_update_average(&tas);
  /* (1 * 4944.32 + 0.6 * 2.384 * 2200 + 0.4 * 1000) /
     (1 + 0.6 * 2.384 + 0.4) */
  EXPECT_DOUBLE_EQ(3000, tas.aggregate_weighted_avg);
  EXPECT_DOUBLE_EQ(2.8304, tas.aggregate_total_weight);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  no_regress_no_persist_test_1();
  no_regress_no_persist_test_2();
  no_regress_no_persist_test_3();
  some_regress_no_persist_test();
  some_decay_test();
  no_regress_full_persist_test();
  no_regress_some_persist_test();
  some_regress_some_persist_test();
  return 0;
}
