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

#include <grpc/support/useful.h>

void grpc_bdp_estimator_init(grpc_bdp_estimator *estimator) {
  estimator->num_samples = 0;
  estimator->first_sample_idx = 0;
  estimator->sampling = false;
}

static int compare_samples(const void *a, const void *b) {
  return GPR_ICMP(*(int64_t *)a, *(int64_t *)b);
}

bool grpc_bdp_estimator_get_estimate(grpc_bdp_estimator *estimator,
                                     int64_t *estimate) {
  if (estimator->num_samples < GRPC_BDP_MIN_SAMPLES_FOR_ESTIMATE) {
    return false;
  }

  int64_t samples[GRPC_BDP_SAMPLES];
  for (uint8_t i = 0; i < estimator->num_samples; i++) {
    samples[i] =
        estimator
            ->samples[(estimator->first_sample_idx + i) % GRPC_BDP_SAMPLES];
  }
  qsort(samples, estimator->num_samples, sizeof(*samples), compare_samples);

  if (estimator->num_samples & 1) {
  } else {
  }
}
