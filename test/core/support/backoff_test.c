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

#include "src/core/lib/support/backoff.h"

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void test_constant_backoff(void) {
  gpr_backoff backoff;
  gpr_backoff_init(&backoff, 1.0, 0.0, 1000, 1000);

  gpr_timespec now = gpr_time_0(GPR_TIMESPAN);
  gpr_timespec next = gpr_backoff_begin(&backoff, now);
  GPR_ASSERT(gpr_time_to_millis(gpr_time_sub(next, now)) == 1000);
  for (int i = 0; i < 10000; i++) {
    next = gpr_backoff_step(&backoff, now);
    GPR_ASSERT(gpr_time_to_millis(gpr_time_sub(next, now)) == 1000);
    now = next;
  }
}

static void test_no_jitter_backoff(void) {
  gpr_backoff backoff;
  gpr_backoff_init(&backoff, 2.0, 0.0, 1, 513);

  gpr_timespec now = gpr_time_0(GPR_TIMESPAN);
  gpr_timespec next = gpr_backoff_begin(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(1, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(3, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(7, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(15, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(31, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(63, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(127, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(255, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(511, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(1023, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(1536, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(2049, GPR_TIMESPAN), next) == 0);
  now = next;
  next = gpr_backoff_step(&backoff, now);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_millis(2562, GPR_TIMESPAN), next) == 0);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  gpr_time_init();

  test_constant_backoff();
  test_no_jitter_backoff();

  return 0;
}
