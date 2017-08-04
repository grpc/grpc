/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/transport/bdp_estimator.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include <limits.h>
#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

static void test_noop(void) {
  gpr_log(GPR_INFO, "test_noop");
  grpc_bdp_estimator est;
  grpc_bdp_estimator_init(&est, "test");
}

static void test_get_estimate_no_samples(void) {
  gpr_log(GPR_INFO, "test_get_estimate_no_samples");
  grpc_bdp_estimator est;
  grpc_bdp_estimator_init(&est, "test");
  int64_t estimate;
  grpc_bdp_estimator_get_estimate(&est, &estimate);
}

static void add_samples(grpc_bdp_estimator *estimator, int64_t *samples,
                        size_t n) {
  GPR_ASSERT(grpc_bdp_estimator_add_incoming_bytes(estimator, 1234567) == true);
  grpc_bdp_estimator_schedule_ping(estimator);
  grpc_bdp_estimator_start_ping(estimator);
  for (size_t i = 0; i < n; i++) {
    GPR_ASSERT(grpc_bdp_estimator_add_incoming_bytes(estimator, samples[i]) ==
               false);
  }
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(1, GPR_TIMESPAN)));
  grpc_bdp_estimator_complete_ping(estimator);
}

static void add_sample(grpc_bdp_estimator *estimator, int64_t sample) {
  add_samples(estimator, &sample, 1);
}

static void test_get_estimate_1_sample(void) {
  gpr_log(GPR_INFO, "test_get_estimate_1_sample");
  grpc_bdp_estimator est;
  grpc_bdp_estimator_init(&est, "test");
  add_sample(&est, 100);
  int64_t estimate;
  grpc_bdp_estimator_get_estimate(&est, &estimate);
}

static void test_get_estimate_2_samples(void) {
  gpr_log(GPR_INFO, "test_get_estimate_2_samples");
  grpc_bdp_estimator est;
  grpc_bdp_estimator_init(&est, "test");
  add_sample(&est, 100);
  add_sample(&est, 100);
  int64_t estimate;
  grpc_bdp_estimator_get_estimate(&est, &estimate);
}

static int64_t get_estimate(grpc_bdp_estimator *estimator) {
  int64_t out;
  GPR_ASSERT(grpc_bdp_estimator_get_estimate(estimator, &out));
  return out;
}

static void test_get_estimate_3_samples(void) {
  gpr_log(GPR_INFO, "test_get_estimate_3_samples");
  grpc_bdp_estimator est;
  grpc_bdp_estimator_init(&est, "test");
  add_sample(&est, 100);
  add_sample(&est, 100);
  add_sample(&est, 100);
  int64_t estimate;
  grpc_bdp_estimator_get_estimate(&est, &estimate);
}

static int64_t next_pow_2(int64_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;
  v++;
  return v;
}

static void test_get_estimate_random_values(size_t n) {
  gpr_log(GPR_INFO, "test_get_estimate_random_values(%" PRIdPTR ")", n);
  grpc_bdp_estimator est;
  grpc_bdp_estimator_init(&est, "test");
  const int kMaxSample = 65535;
  int min = kMaxSample;
  int max = 0;
  for (size_t i = 0; i < n; i++) {
    int sample = rand() % (kMaxSample + 1);
    if (sample < min) min = sample;
    if (sample > max) max = sample;
    add_sample(&est, sample);
    if (i >= 3) {
      gpr_log(GPR_DEBUG, "est:%" PRId64 " min:%d max:%d", get_estimate(&est),
              min, max);
      GPR_ASSERT(get_estimate(&est) <= GPR_MAX(65536, 2 * next_pow_2(max)));
    }
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_noop();
  test_get_estimate_no_samples();
  test_get_estimate_1_sample();
  test_get_estimate_2_samples();
  test_get_estimate_3_samples();
  for (size_t i = 3; i < 1000; i = i * 3 / 2) {
    test_get_estimate_random_values(i);
  }
  grpc_shutdown();
  return 0;
}
