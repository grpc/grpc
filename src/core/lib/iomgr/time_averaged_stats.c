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

void grpc_time_averaged_stats_init(grpc_time_averaged_stats* stats,
                                   double init_avg, double regress_weight,
                                   double persistence_factor) {
  stats->init_avg = init_avg;
  stats->regress_weight = regress_weight;
  stats->persistence_factor = persistence_factor;
  stats->batch_total_value = 0;
  stats->batch_num_samples = 0;
  stats->aggregate_total_weight = 0;
  stats->aggregate_weighted_avg = init_avg;
}

void grpc_time_averaged_stats_add_sample(grpc_time_averaged_stats* stats,
                                         double value) {
  stats->batch_total_value += value;
  ++stats->batch_num_samples;
}

double grpc_time_averaged_stats_update_average(
    grpc_time_averaged_stats* stats) {
  /* Start with the current batch: */
  double weighted_sum = stats->batch_total_value;
  double total_weight = stats->batch_num_samples;
  if (stats->regress_weight > 0) {
    /* Add in the regression towards init_avg_: */
    weighted_sum += stats->regress_weight * stats->init_avg;
    total_weight += stats->regress_weight;
  }
  if (stats->persistence_factor > 0) {
    /* Add in the persistence: */
    const double prev_sample_weight =
        stats->persistence_factor * stats->aggregate_total_weight;
    weighted_sum += prev_sample_weight * stats->aggregate_weighted_avg;
    total_weight += prev_sample_weight;
  }
  stats->aggregate_weighted_avg =
      (total_weight > 0) ? (weighted_sum / total_weight) : stats->init_avg;
  stats->aggregate_total_weight = total_weight;
  stats->batch_num_samples = 0;
  stats->batch_total_value = 0;
  return stats->aggregate_weighted_avg;
}
