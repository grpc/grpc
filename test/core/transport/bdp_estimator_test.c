/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/transport/bdp_estimator.h"

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
  int min = INT_MAX;
  int max = 65535;  // Windows rand() has limited range, make sure the ASSERT
                    // passes
  for (size_t i = 0; i < n; i++) {
    int sample = rand();
    if (sample < min) min = sample;
    if (sample > max) max = sample;
    add_sample(&est, sample);
    if (i >= 3) {
      gpr_log(GPR_DEBUG, "est:%" PRId64 " min:%d max:%d", get_estimate(&est),
              min, max);
      GPR_ASSERT(get_estimate(&est) <= 2 * next_pow_2(max));
    }
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_noop();
  test_get_estimate_no_samples();
  test_get_estimate_1_sample();
  test_get_estimate_2_samples();
  test_get_estimate_3_samples();
  for (size_t i = 3; i < 1000; i = i * 3 / 2) {
    test_get_estimate_random_values(i);
  }
  return 0;
}
